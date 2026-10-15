#!/usr/bin/env python3
"""Extract the deployment artifacts of a VNF package built by tools/csar/build.py.

    extract.py PACKAGE.csar chart OUT_DIR   writes the Helm chart (MCIOP) into OUT_DIR, prints its path
    extract.py PACKAGE.csar image           writes the image archive to stdout (for `docker load`)
    extract.py PACKAGE.csar image --exists  only the exit code: does the package carry an image

Validate the package first (validate.py): this tool only copies bytes.
Exit code 0 on success, 3 if the package has no such artifact, 1 on errors.
"""

import argparse
import pathlib
import shutil
import sys
import zipfile

CHART_DIR = "Artifacts/Deployment/HELM/"
IMAGE_DIR = "Artifacts/Images/"


def members(zf: zipfile.ZipFile, prefix: str, suffix: str) -> list[zipfile.ZipInfo]:
    return [m for m in zf.infolist()
            if m.filename.startswith(prefix) and m.filename.endswith(suffix) and "/" not in m.filename[len(prefix):]]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package", type=pathlib.Path)
    ap.add_argument("artifact", choices=["chart", "image"])
    ap.add_argument("out_dir", type=pathlib.Path, nargs="?")
    ap.add_argument("--exists", action="store_true", help="image: report presence by exit code only")
    args = ap.parse_args()

    with zipfile.ZipFile(args.package) as zf:
        if args.artifact == "chart":
            if args.out_dir is None:
                ap.error("chart needs OUT_DIR")
            found = members(zf, CHART_DIR, ".tgz")
            if len(found) != 1:
                print(f"{args.package}: expected one chart in {CHART_DIR}, found {len(found)}", file=sys.stderr)
                return 3 if not found else 1
            # The basename only: nothing is written outside OUT_DIR.
            target = args.out_dir / pathlib.PurePosixPath(found[0].filename).name
            with zf.open(found[0]) as src, open(target, "wb") as dst:
                shutil.copyfileobj(src, dst)
            print(target)
        else:
            found = members(zf, IMAGE_DIR, ".tar")
            if not found:
                print(f"{args.package}: no image in {IMAGE_DIR}", file=sys.stderr)
                return 3
            if len(found) > 1:
                print(f"{args.package}: more than one image in {IMAGE_DIR}", file=sys.stderr)
                return 1
            if args.exists:
                return 0
            with zf.open(found[0]) as src:
                shutil.copyfileobj(src, sys.stdout.buffer)
    return 0


if __name__ == "__main__":
    sys.exit(main())
