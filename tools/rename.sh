#!/usr/bin/env bash
#
# Renames the project, or checks that the name is used only where allowed.
#
# Usage:
#   tools/rename.sh <new-name> [<new-metric-prefix>]
#       Rename: rewrites project.env and the few files that must hold the
#       name literally (ALLOWED below), then regenerates derived files
#       (dashboard, generated docs). The metric prefix defaults to the new
#       name with '-' replaced by '_'.
#
#   tools/rename.sh --check
#       Fail if the project name (or the metric prefix) appears as a word in
#       any file outside ALLOWED (ci/pre-checks.sh). Everything else must
#       derive the name from project.env (CMake, scripts, Python tools,
#       generated files) or use role-based names (server, loadgen, ...).
#
# After a rename, reconfigure build directories (binary names change) and
# rebuild the image (repository name changes).

set -euo pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
PROJECT_DIR="$(realpath "${SCRIPT_DIR}/..")"
cd "${PROJECT_DIR}"

# shellcheck source=../project.env
source project.env

# Files that hold the name literally because they cannot read project.env.
ALLOWED=(
    'project.env'
    'README.md'                                 # title
    'doc/book.adoc'                             # :project: attribute, used by every chapter
    'doc/generated/*'                           # generated from the contract (prefix)
    'deploy/helm/*/Chart.yaml'                  # chart names and dependency names
    'deploy/helm/*/values.yaml'                 # image repository, binary names, metric prefix
    'deploy/helm/server/dashboards/server.json' # generated (prefix, uid, title)
)

is_allowed() {
    local f="$1" pattern
    for pattern in "${ALLOWED[@]}"; do
        # shellcheck disable=SC2053  # glob match intended
        [[ "${f}" == ${pattern} ]] && return 0
    done
    return 1
}

if [[ "${1:-}" == "--check" ]]; then
    words=("${PROJECT_NAME}")
    [[ "${METRIC_PREFIX}" != "${PROJECT_NAME}" ]] && words+=("${METRIC_PREFIX}")
    found=0
    while IFS= read -r -d '' f; do
        [[ -f "${f}" ]] || continue
        is_allowed "${f}" && continue
        for w in "${words[@]}"; do
            if hits=$(grep -nwI --color=never -e "${w}" "${f}" 2>/dev/null); then
                while IFS= read -r hit; do printf '%s:%s\n' "${f}" "${hit}" >&2; done <<< "${hits}"
                found=1
            fi
        done
    done < <(git ls-files -z --cached --others --exclude-standard)
    if (( found )); then
        echo "The project name '${PROJECT_NAME}' must come from project.env (or tools/rename.sh ALLOWED)." >&2
        exit 1
    fi
    echo "Project name '${PROJECT_NAME}' is used only in allowed places."
    exit 0
fi

NEW_NAME="${1:?usage: rename.sh <new-name> [<new-metric-prefix>] | --check}"
NEW_PREFIX="${2:-${NEW_NAME//-/_}}"
[[ "${NEW_NAME}" =~ ^[a-z][a-z0-9-]{0,30}[a-z0-9]$ ]] || { echo "invalid name (DNS label, max 32 chars): ${NEW_NAME}" >&2; exit 1; }
[[ "${NEW_PREFIX}" =~ ^[a-z_][a-z0-9_]*$ ]] || { echo "invalid metric prefix: ${NEW_PREFIX}" >&2; exit 1; }

OLD_NAME="${PROJECT_NAME}"
OLD_PREFIX="${METRIC_PREFIX}"
echo "Renaming '${OLD_NAME}' -> '${NEW_NAME}' (metric prefix '${OLD_PREFIX}' -> '${NEW_PREFIX}')"

sed -i -E "s/^PROJECT_NAME=.*/PROJECT_NAME=${NEW_NAME}/; s/^METRIC_PREFIX=.*/METRIC_PREFIX=${NEW_PREFIX}/" project.env

while IFS= read -r -d '' f; do
    [[ -f "${f}" && "${f}" != project.env ]] || continue
    is_allowed "${f}" || continue
    sed -i -E "s/^(metricPrefix: ).*/\1${NEW_PREFIX}/" "${f}"
    sed -i -E "s/\b${OLD_NAME}\b/${NEW_NAME}/g" "${f}"
    echo "  rewrote ${f}"
done < <(git ls-files -z --cached --others --exclude-standard)

echo "Regenerating derived files"
python3 -I deploy/grafana/build_dashboard.py
python3 -I tools/contract.py adoc > doc/generated/metrics.adoc

"${SCRIPT_DIR}/rename.sh" --check
echo "Done. Reconfigure build directories and rebuild the image."
