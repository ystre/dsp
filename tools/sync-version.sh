#!/usr/bin/env bash
#
# Propagates the root `VERSION` to the places that cannot read it: the Helm
# charts' `version` / `appVersion` and the umbrella chart's dependency pins.
#
# Usage:
#   tools/sync-version.sh           # rewrite the charts
#   tools/sync-version.sh --check   # fail if any of them differs (ci/pre-checks.sh)

set -euo pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
PROJECT_DIR="$(realpath "${SCRIPT_DIR}/..")"
VERSION="$(< "${PROJECT_DIR}/VERSION")"
CHARTS=("${PROJECT_DIR}"/deploy/helm/*/Chart.yaml)

if [[ "${1:-}" == "--check" ]]; then
    bad=0
    for chart in "${CHARTS[@]}"; do
        # Own version and appVersion, and file:// dependency pins (the project's own charts).
        while read -r line; do
            echo "${chart#"${PROJECT_DIR}"/}: ${line} (VERSION is ${VERSION})" >&2
            bad=1
        done < <(awk -v v="${VERSION}" '
            /^version:/                         { if ($2 != v) print $0 }
            /^appVersion:/                      { gsub(/"/, "", $2); if ($2 != v) print $0 }
            /^ *repository: file:/              { if (pin != "" && pin != v) print "dependency version " pin }
            /^ *version:/                       { pin = $2 }
            /^ *- name:/                        { pin = "" }
        ' "${chart}")
    done
    (( bad == 0 )) && echo "Chart versions match VERSION (${VERSION})."
    exit "${bad}"
fi

for chart in "${CHARTS[@]}"; do
    # Own version / appVersion: top-level keys only.
    sed -i -E "s/^version: .*/version: ${VERSION}/; s/^appVersion: .*/appVersion: \"${VERSION}\"/" "${chart}"
    # Pins of the project's own subcharts: a `version:` line followed by a file:// repository.
    awk -v v="${VERSION}" '
        { lines[NR] = $0 }
        END {
            for (i = 1; i <= NR; i++) {
                if (lines[i] ~ /^ +version:/ && lines[i + 1] ~ /^ +repository: file:/) {
                    sub(/version: .*/, "version: " v, lines[i])
                }
                print lines[i]
            }
        }' "${chart}" > "${chart}.tmp" && mv "${chart}.tmp" "${chart}"
done
echo "Charts set to ${VERSION}."
