# shellcheck shell=bash
# shellcheck disable=SC2034  # variables are used by the stage scripts that source this file
#
# Shared by the CI stage scripts. Sourced, not executed.
#
# Environment:
#   WORKSPACE        Directory the stage works in (default: the repository).
#                    ci/e2e.sh points it at a clean copy.
#   CI_STRICT_TOOLS  1 = a missing tool fails the stage (set inside the build
#                    container, where everything must be present); 0 = the
#                    check is skipped with a warning (developer hosts).

set -euo pipefail

CI_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"
PROJECT_DIR="$(realpath "${CI_DIR}/..")"
export WORKSPACE="${WORKSPACE:-${PROJECT_DIR}}"
export CI_STRICT_TOOLS="${CI_STRICT_TOOLS:-0}"

# shellcheck source=../project.env
source "${PROJECT_DIR}/project.env"
VERSION="$(< "${PROJECT_DIR}/VERSION")"
export PROJECT_NAME METRIC_PREFIX VENDOR VERSION

BUILD_DEBUG="${WORKSPACE}/build/debug"
BUILD_RELEASE="${WORKSPACE}/build/release"
DIST="${WORKSPACE}/dist"
RESULTS="${WORKSPACE}/results"
IMAGE_REGISTRY="${IMAGE_REGISTRY:-oci-registry.internal.ystre.org:5000}"
IMAGES=(server loadgen)
CHARTS=(server loadgen kafka deps umbrella)
PY=(python3 -I)

stage() { printf '\n\033[1;34m=== Stage: %s ===\033[0m\n' "$*"; }
info()  { printf '  %s\n' "$*"; }
warn()  { printf '\033[33m  warning: %s\033[0m\n' "$*" >&2; }
die()   { printf '\033[31m  error: %s\033[0m\n' "$*" >&2; exit 1; }

# image_ref <component>: the image of a component (server, loadgen).
image_ref() {
    printf '%s/%s-%s:%s\n' "${IMAGE_REGISTRY}" "${PROJECT_NAME}" "$1" "${VERSION}";
}

# have <tool>: true if available; otherwise fails (strict) or warns (lenient) and returns 1.
have() {
    command -v "$1" >/dev/null 2>&1 && return 0
    if [[ "${CI_STRICT_TOOLS}" == 1 ]]; then
        die "required tool '$1' not found"
    fi
    warn "'$1' not found; skipping what needs it"
    return 1
}

# Files of the source tree (tracked and new, without ignored ones).
source_files() {
    git -C "${WORKSPACE}" ls-files -z --cached --others --exclude-standard "$@" \
        | while IFS= read -r -d '' f; do [[ -f "${WORKSPACE}/${f}" ]] && printf '%s\0' "${f}"; done
}
