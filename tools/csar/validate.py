#!/usr/bin/env python3
"""Validate an ETSI NFV SOL004 VNF package built by tools/csar/build.py.

    validate.py PACKAGE.csar [--ca-file CA.pem] [--no-helm]

Checks:
  * TOSCA-Metadata/TOSCA.meta: required keys; every referenced entry exists.
  * Manifest: the metadata section has every SOL004 key; every file of the
    package (except the manifest and the certificate) is listed exactly once
    and its SHA-256 digest matches; no listed file is missing.
  * Signature (if the manifest carries a CMS block): it verifies against the
    manifest content (and the CA file, if given; integrity-only otherwise).
  * VNFD (PyYAML): parses; every import is in the package; every node
    template has a known type; requirement targets and artifact files exist;
    the VNF node type derives from tosca.nodes.nfv.VNF.
  * Helm chart artifacts: `helm lint` passes (unless --no-helm or no helm).

Exit code 0 when valid, 1 otherwise (each problem is printed).
"""

import argparse
import hashlib
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

import yaml

META_KEYS = ["TOSCA-Meta-File-Version", "CSAR-Version", "Created-By", "Entry-Definitions",
             "ETSI-Entry-Manifest", "ETSI-Entry-Change-Log", "ETSI-Entry-Licenses"]
MANIFEST_KEYS = ["vnfd_id", "vnf_provider_id", "vnf_product_name", "vnf_release_date_time",
                 "vnf_package_version", "compatible_specification_versions", "vnfm_info"]
CMS = re.compile(r"-----BEGIN CMS-----.*?-----END CMS-----\n?", re.S)


class Report:
    def __init__(self):
        self.problems: list[str] = []
        self.checked: list[str] = []

    def fail(self, text: str) -> None:
        self.problems.append(text)

    def ok(self, text: str) -> None:
        self.checked.append(text)


def parse_meta(text: str) -> dict[str, str]:
    meta = {}
    for line in text.splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            meta[k.strip()] = v.strip()
    return meta


def parse_manifest(text: str) -> tuple[dict, list[dict]]:
    body = CMS.sub("", text)
    blocks = [b for b in re.split(r"\n\s*\n", body.strip()) if b.strip()]
    metadata, entries = {}, []
    for block in blocks:
        fields = dict(l.split(":", 1) for l in block.splitlines() if ":" in l)
        fields = {k.strip(): v.strip() for k, v in fields.items()}
        if "metadata" in fields:
            metadata = {k: v for k, v in fields.items() if k != "metadata"}
        elif "Source" in fields:
            entries.append(fields)
    return metadata, entries


def check_meta(root: pathlib.Path, r: Report) -> dict:
    path = root / "TOSCA-Metadata" / "TOSCA.meta"
    if not path.exists():
        r.fail("TOSCA-Metadata/TOSCA.meta is missing")
        return {}
    meta = parse_meta(path.read_text())
    for key in META_KEYS:
        if key not in meta:
            r.fail(f"TOSCA.meta: missing {key}")
    for key in ("Entry-Definitions", "ETSI-Entry-Manifest", "ETSI-Entry-Change-Log", "ETSI-Entry-Licenses",
                "ETSI-Entry-Tests", "ETSI-Entry-Certificate"):
        if key in meta and not (root / meta[key]).exists():
            r.fail(f"TOSCA.meta: {key} -> {meta[key]} does not exist")
    r.ok(f"TOSCA.meta: CSAR-Version {meta.get('CSAR-Version')}, entry {meta.get('Entry-Definitions')}")
    return meta


def check_manifest(root: pathlib.Path, meta: dict, ca_file: str | None, r: Report) -> None:
    name = meta.get("ETSI-Entry-Manifest")
    if not name or not (root / name).exists():
        return
    text = (root / name).read_text()
    metadata, entries = parse_manifest(text)
    for key in MANIFEST_KEYS:
        if not metadata.get(key):
            r.fail(f"manifest: metadata {key} is missing")

    listed = {}
    for e in entries:
        src = e["Source"]
        if src in listed:
            r.fail(f"manifest: {src} listed twice")
        listed[src] = e
        path = root / src
        if not path.is_file():
            r.fail(f"manifest: {src} does not exist")
            continue
        if e.get("Algorithm", "").upper() != "SHA-256":
            r.fail(f"manifest: {src}: algorithm {e.get('Algorithm')} (expected SHA-256)")
            continue
        if hashlib.sha256(path.read_bytes()).hexdigest() != e.get("Hash", "").lower():
            r.fail(f"manifest: {src}: digest mismatch")

    exempt = {name, meta.get("ETSI-Entry-Certificate")}
    for path in root.rglob("*"):
        rel = path.relative_to(root).as_posix()
        if path.is_file() and rel not in exempt and rel not in listed:
            r.fail(f"manifest: {rel} is in the package but not in the manifest")
    r.ok(f"manifest: {len(entries)} digests verified, package version {metadata.get('vnf_package_version')}")

    sig = CMS.search(text)
    if sig:
        if not shutil.which("openssl"):
            r.fail("manifest is signed but openssl is not available to verify it")
            return
        with tempfile.TemporaryDirectory() as tmp:
            content, signature = pathlib.Path(tmp) / "content", pathlib.Path(tmp) / "sig.pem"
            content.write_text(CMS.sub("", text))
            signature.write_text(sig.group(0))
            cmd = ["openssl", "cms", "-verify", "-binary", "-inform", "PEM", "-in", str(signature),
                   "-content", str(content), "-out", "/dev/null"]
            cmd += ["-CAfile", ca_file] if ca_file else ["-noverify"]
            res = subprocess.run(cmd, capture_output=True, text=True, check=False)
            if res.returncode != 0:
                r.fail(f"manifest signature does not verify: {res.stderr.strip()}")
            else:
                r.ok("manifest signature verified" + ("" if ca_file else " (integrity only, no CA given)"))


def load_types(definitions: pathlib.Path, entry: dict) -> set[str]:
    types = set()
    for imp in entry.get("imports", []):
        f = imp if isinstance(imp, str) else imp.get("file")
        # The ETSI files carry stray tabs in trailing whitespace, which strict YAML rejects.
        text = "\n".join(line.rstrip(" \t") for line in (definitions / f).read_text().splitlines())
        doc = yaml.safe_load(text)
        for section in ("node_types", "artifact_types", "policy_types", "data_types"):
            types |= set((doc.get(section) or {}).keys())
    return types


def check_vnfd(root: pathlib.Path, meta: dict, r: Report) -> list[str]:
    entry_path = root / meta.get("Entry-Definitions", "")
    if not entry_path.is_file():
        return []
    try:
        vnfd = yaml.safe_load(entry_path.read_text())
    except yaml.YAMLError as ex:
        r.fail(f"VNFD does not parse: {ex}")
        return []
    definitions = entry_path.parent
    for imp in vnfd.get("imports", []):
        f = imp if isinstance(imp, str) else imp.get("file")
        if not (definitions / f).is_file():
            r.fail(f"VNFD: import {f} is not in the package")
            return []

    known = load_types(definitions, vnfd) | set((vnfd.get("node_types") or {}).keys())
    for name, nt in (vnfd.get("node_types") or {}).items():
        if nt.get("derived_from") == "tosca.nodes.nfv.VNF":
            r.ok(f"VNFD: VNF node type {name}")
            break
    else:
        r.fail("VNFD: no node type derived from tosca.nodes.nfv.VNF")

    topo = vnfd.get("topology_template") or {}
    templates = topo.get("node_templates") or {}
    helm_charts = []
    for tname, t in templates.items():
        if t.get("type") not in known:
            r.fail(f"VNFD: node template {tname}: unknown type {t.get('type')}")
        for req in t.get("requirements", []):
            for rname, target in req.items():
                target_node = target if isinstance(target, str) else target.get("node")
                if target_node not in templates:
                    r.fail(f"VNFD: {tname}.{rname} -> {target_node}: no such node template")
        for aname, art in (t.get("artifacts") or {}).items():
            f = art.get("file") if isinstance(art, dict) else art
            if not (root / f).is_file():
                r.fail(f"VNFD: artifact {tname}.{aname} -> {f} is not in the package")
            elif isinstance(art, dict) and art.get("type") == "tosca.artifacts.nfv.HelmChart":
                helm_charts.append(f)
    sub = topo.get("substitution_mappings", {})
    if sub.get("node_type") not in (vnfd.get("node_types") or {}):
        r.fail("VNFD: substitution_mappings.node_type is not defined in the VNFD")
    for policy in topo.get("policies", []):
        for pname, p in policy.items():
            if p.get("type") not in known:
                r.fail(f"VNFD: policy {pname}: unknown type {p.get('type')}")
            for target in p.get("targets", []):
                if target not in templates:
                    r.fail(f"VNFD: policy {pname} targets unknown {target}")
    if not helm_charts:
        r.fail("VNFD: no Helm chart (MCIOP) artifact")
    r.ok(f"VNFD: {len(templates)} node templates, {len(helm_charts)} Helm chart artifact(s)")
    return helm_charts


def check_charts(root: pathlib.Path, charts: list[str], r: Report) -> None:
    if not shutil.which("helm"):
        r.ok("helm not available: chart lint skipped")
        return
    for chart in charts:
        res = subprocess.run(["helm", "lint", "--strict", str(root / chart)], capture_output=True, text=True, check=False)
        if res.returncode != 0:
            r.fail(f"helm lint {chart}:\n{res.stdout}{res.stderr}")
        else:
            r.ok(f"helm lint {chart}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package")
    ap.add_argument("--ca-file", help="CA certificate(s) to verify the manifest signature against")
    ap.add_argument("--no-helm", action="store_true")
    args = ap.parse_args()

    r = Report()
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        with zipfile.ZipFile(args.package) as zf:
            for member in zf.namelist():
                if member.startswith("/") or ".." in pathlib.PurePosixPath(member).parts:
                    print(f"unsafe path in archive: {member}", file=sys.stderr)
                    return 1
            zf.extractall(root)
        meta = check_meta(root, r)
        check_manifest(root, meta, args.ca_file, r)
        charts = check_vnfd(root, meta, r)
        if not args.no_helm:
            check_charts(root, charts, r)

    for line in r.checked:
        print(f"  ok    {line}")
    for line in r.problems:
        print(f"  FAIL  {line}")
    print(f"{args.package}: {'VALID' if not r.problems else f'{len(r.problems)} problem(s)'}")
    return 0 if not r.problems else 1


if __name__ == "__main__":
    sys.exit(main())
