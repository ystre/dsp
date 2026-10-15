#!/usr/bin/env python3
"""Build the ETSI NFV SOL004 VNF package (CSAR) of the project.

    build.py --chart dist/charts/<umbrella>-<ver>.tgz [--out dist]
             [--image-tar image.tar] [--sign-key key.pem --sign-cert cert.pem]

Layout (doc/packaging.adoc): TOSCA-Metadata/TOSCA.meta, Definitions/ (the
VNFD rendered from csar/Definitions/vnfd.yaml.tpl plus the vendored SOL001
types), Artifacts/Deployment/HELM/ (the MCIOP), optionally
Artifacts/Images/, Files/ (change log, licenses, tests) and the manifest
<project>.mf with a SHA-256 digest of every file.

Signing (optional): SOL004 option 1 - a CMS signature of the manifest,
appended to it as a PEM block, with the signing certificate in the package
(ETSI-Entry-Certificate). Needs the openssl CLI.

The archive is reproducible: entries are sorted and carry the time of
SOURCE_DATE_EPOCH (default: the last commit, else now).
"""

import argparse
import datetime
import hashlib
import os
import pathlib
import shutil
import string
import subprocess
import sys
import tempfile
import uuid
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import project  # noqa: E402

CSAR_SRC = project.ROOT / "csar"


def sol001_version() -> str:
    return (CSAR_SRC / "Definitions" / "SOL001_VERSION").read_text().strip()


def source_date() -> datetime.datetime:
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    if not epoch:
        res = subprocess.run(["git", "-C", str(project.ROOT), "log", "-1", "--format=%ct"],
                             capture_output=True, text=True, check=False)
        epoch = res.stdout.strip() or None
    ts = int(epoch) if epoch else int(datetime.datetime.now(datetime.timezone.utc).timestamp())
    return datetime.datetime.fromtimestamp(ts, datetime.timezone.utc)


def render(template: pathlib.Path, values: dict) -> str:
    text = string.Template(template.read_text()).substitute(values)
    return "\n".join(line.rstrip() for line in text.splitlines()) + "\n"


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def sw_image_artifact(image: pathlib.Path, target: str) -> str:
    """OsContainer `sw_image` artifact for an image bundled in the package."""
    size_mib = max(1, image.stat().st_size >> 20)
    return f"""      artifacts:
        sw_image:
          type: tosca.artifacts.nfv.SwImage
          file: {target}
          properties:
            name: {project.name()}
            version: "{project.version()}"
            checksum:
              algorithm: sha-256
              hash: {sha256(image)}
            container_format: docker
            disk_format: raw
            min_disk: 0 B
            size: {size_mib} MiB
"""


def manifest(stage: pathlib.Path, date: datetime.datetime, descriptor_id: str, exclude: set[str]) -> str:
    lines = [
        "metadata:",
        f"vnfd_id: {descriptor_id}",
        f"vnf_provider_id: {project.env()['VENDOR']}",
        f"vnf_product_name: {project.name()}",
        f"vnf_release_date_time: {date.strftime('%Y-%m-%dT%H:%M:%S+00:00')}",
        f"vnf_package_version: {project.version()}",
        # SOL004 and SOL001 are released in lockstep; the package claims the version of the types it ships.
        f"compatible_specification_versions: {sol001_version()}",
        f"vnfm_info: etsivnfm:v{sol001_version()}",
        "",
    ]
    for path in sorted(p for p in stage.rglob("*") if p.is_file()):
        rel = path.relative_to(stage).as_posix()
        if rel in exclude:
            continue
        lines += [f"Source: {rel}", "Algorithm: SHA-256", f"Hash: {sha256(path)}", ""]
    return "\n".join(lines)


def sign(manifest_text: str, key: pathlib.Path, cert: pathlib.Path) -> str:
    res = subprocess.run(
        ["openssl", "cms", "-sign", "-binary", "-outform", "PEM", "-signer", str(cert), "-inkey", str(key),
         "-md", "sha256", "-noattr"],
        input=manifest_text.encode(), capture_output=True, check=True)
    return manifest_text + res.stdout.decode()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--chart", required=True, help="packaged integration chart (.tgz)")
    ap.add_argument("--out", default=str(project.ROOT / "dist"))
    ap.add_argument("--image-tar", help="container image archive (docker save) to bundle")
    ap.add_argument("--sign-key")
    ap.add_argument("--sign-cert")
    ap.add_argument("--suffix", default="", help="appended to the file name, e.g. '-signed'")
    args = ap.parse_args()
    if bool(args.sign_key) != bool(args.sign_cert):
        ap.error("--sign-key and --sign-cert go together")

    name, version, vendor = project.name(), project.version(), project.env()["VENDOR"]
    chart = pathlib.Path(args.chart)
    date = source_date()
    # Stable per product and version, so the same release always gets the same VNFD id.
    descriptor_id = str(uuid.uuid5(uuid.NAMESPACE_URL, f"urn:{vendor}:{name}:vnfd:{version}"))
    manifest_name = f"{name}.mf"
    cert_name = f"Files/Certificates/{name}.cert"

    with tempfile.TemporaryDirectory() as tmp:
        stage = pathlib.Path(tmp) / "csar"
        (stage / "TOSCA-Metadata").mkdir(parents=True)
        (stage / "Definitions").mkdir()
        (stage / "Artifacts" / "Deployment" / "HELM").mkdir(parents=True)

        shutil.copy2(chart, stage / "Artifacts" / "Deployment" / "HELM" / chart.name)
        sw_image = ""
        if args.image_tar:
            image = pathlib.Path(args.image_tar)
            target = f"Artifacts/Images/{name}-{version}.tar"
            (stage / "Artifacts" / "Images").mkdir(parents=True)
            shutil.copy2(image, stage / target)
            sw_image = sw_image_artifact(image, target)

        values = {
            "PROJECT_NAME": name, "VENDOR": vendor, "VERSION": version,
            "DESCRIPTOR_ID": descriptor_id, "HELM_CHART": chart.name,
            "VNFM_INFO": f"etsivnfm:v{sol001_version()}", "SW_IMAGE_ARTIFACT": sw_image.rstrip("\n"),
            "CERTIFICATE_ENTRY": f"ETSI-Entry-Certificate: {cert_name}" if args.sign_key else "",
        }
        (stage / "Definitions" / "vnfd.yaml").write_text(render(CSAR_SRC / "Definitions" / "vnfd.yaml.tpl", values))
        for types in sorted((CSAR_SRC / "Definitions").glob("etsi_nfv_sol001_*.yaml")):
            shutil.copy2(types, stage / "Definitions" / types.name)
        meta = render(CSAR_SRC / "TOSCA-Metadata" / "TOSCA.meta.tpl", values)
        (stage / "TOSCA-Metadata" / "TOSCA.meta").write_text("\n".join(l for l in meta.splitlines() if l) + "\n")

        shutil.copytree(CSAR_SRC / "Files", stage / "Files")
        if (project.ROOT / "LICENSE").exists():
            shutil.copy2(project.ROOT / "LICENSE", stage / "Files" / "Licenses" / "LICENSE.txt")
        if args.sign_cert:
            (stage / cert_name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(args.sign_cert, stage / cert_name)

        text = manifest(stage, date, descriptor_id, exclude={manifest_name, cert_name})
        if args.sign_key:
            text = sign(text, pathlib.Path(args.sign_key), pathlib.Path(args.sign_cert))
        (stage / manifest_name).write_text(text)

        out = pathlib.Path(args.out)
        out.mkdir(parents=True, exist_ok=True)
        csar = out / f"{name}-{version}{args.suffix}.csar"
        stamp = date.timetuple()[:6]
        # TOSCA.meta first (some readers expect it), then everything else sorted.
        files = sorted((p for p in stage.rglob("*") if p.is_file()),
                       key=lambda p: (p.relative_to(stage).parts[0] != "TOSCA-Metadata", p.relative_to(stage).as_posix()))
        with zipfile.ZipFile(csar, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            for path in files:
                info = zipfile.ZipInfo(path.relative_to(stage).as_posix(), date_time=stamp)
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                zf.writestr(info, path.read_bytes())
    print(csar)
    return 0


if __name__ == "__main__":
    sys.exit(main())
