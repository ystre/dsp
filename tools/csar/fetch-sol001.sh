#!/usr/bin/env bash
#
# Vendors the ETSI GS NFV-SOL 001 TOSCA type definitions imported by the VNFD
# (csar/Definitions/vnfd.yaml.tpl) into csar/Definitions/, so that packages
# are self-contained and validation works offline.
#
# Usage: tools/csar/fetch-sol001.sh [<version>]   (default: SOL001_VERSION)
#
# Bumping the version: run this, then update `vnfm_info` /
# `compatible_specification_versions` (tools/csar/build.py) if the VNFD
# needs features of the new version, and commit the files.

set -euo pipefail

# 4.4.1 is the newest release whose files are valid YAML: in v5.1.1 and v5.2.1,
# etsi_nfv_sol001_vnfd_types.yaml has a property key without its colon
# (change_selected_deployable_modules_op), which strict parsers reject.
SOL001_VERSION="${1:-4.4.1}"
SCRIPT_DIR="$(dirname "$(realpath "$0")")"
DEST="$(realpath "${SCRIPT_DIR}/../../csar/Definitions")"
BASE="https://forge.etsi.org/rep/nfv/SOL001/-/raw/v${SOL001_VERSION}"

for f in etsi_nfv_sol001_common_types.yaml etsi_nfv_sol001_vnfd_types.yaml; do
    echo "Fetching ${BASE}/${f}"
    curl --fail --silent --show-error --location -o "${DEST}/${f}.tmp" "${BASE}/${f}"
    grep -q "template_version: ${SOL001_VERSION}" "${DEST}/${f}.tmp" \
        || { echo "unexpected content in ${f} (no template_version ${SOL001_VERSION})" >&2; rm -f "${DEST}/${f}.tmp"; exit 1; }
    # Must parse (stray tabs in trailing whitespace are tolerated, see tools/csar/validate.py).
    python3 -I -c 'import sys, yaml; yaml.safe_load("\n".join(l.rstrip(" \t") for l in open(sys.argv[1]).read().splitlines()))' \
        "${DEST}/${f}.tmp" || { echo "${f} v${SOL001_VERSION} is not valid YAML" >&2; rm -f "${DEST}"/*.tmp; exit 1; }
done
# All files checked: replace them together, never a mix of versions.
for f in etsi_nfv_sol001_common_types.yaml etsi_nfv_sol001_vnfd_types.yaml; do
    mv "${DEST}/${f}.tmp" "${DEST}/${f}"
done
# BSD-3-Clause: redistribution must retain the notice; it ships in the package's Licenses.
curl --fail --silent --show-error --location -o "${DEST}/../Files/Licenses/ETSI-SOL001-LICENSE.txt" "${BASE}/LICENSE"
echo "${SOL001_VERSION}" > "${DEST}/SOL001_VERSION"
echo "Vendored SOL001 v${SOL001_VERSION} into ${DEST}"
