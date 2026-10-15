# shellcheck shell=bash
# shellcheck disable=SC2034  # variables are used by the scripts that source this file
#
# Shared by the deploy/lab scripts. Sourced, not executed.

set -euo pipefail

LAB_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"
PROJECT_DIR="$(realpath "${LAB_DIR}/../..")"
# shellcheck source=../../project.env
source "${PROJECT_DIR}/project.env"
VERSION="$(< "${PROJECT_DIR}/VERSION")"
IMAGE_REPOSITORY="${IMAGE_REPOSITORY:-localhost:5000/${PROJECT_NAME}}"
PY=(python3 -I)

log() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
die() { printf '\033[31merror: %s\033[0m\n' "$*" >&2; exit 1; }

need() {
    local tool
    for tool in "$@"; do command -v "${tool}" >/dev/null || die "${tool} is required"; done
}

# chart_field <chart dir|.tgz> <field>: a top-level Chart.yaml field (version, appVersion, name).
chart_field() {
    helm show chart "$1" | awk -v k="$2:" '/^[^ -]/ && $1 == k { gsub(/"/, "", $2); print $2; exit }'
}

# kafka_topics: the KafkaTopic documents of a rendered manifest (stdin).
kafka_topics() {
    awk 'BEGIN { RS = "\n---\n" } /(^|\n)kind: KafkaTopic\n/ { printf "---\n%s\n", $0 }'
}

# package_own_subcharts <chart dir>: the project's own (file://) subcharts,
# packaged from the working tree into <chart dir>/charts without touching the
# third-party ones; then the third-party ones are downloaded only if missing
# (offline runs).
package_own_subcharts() {
    local chart="$1" sub
    mkdir -p "${chart}/charts"
    while IFS= read -r sub; do
        helm package "${chart}/${sub}" --destination "${chart}/charts" >/dev/null
    done < <(awk '/repository: file:\/\// { sub(/.*file:\/\//, ""); print }' "${chart}/Chart.yaml")
    if helm dependency list "${chart}" 2>/dev/null | awk 'NR > 1 && NF && $4 != "ok" { bad = 1 } END { exit !bad }'; then
        helm dependency build "${chart}"
    fi
}
