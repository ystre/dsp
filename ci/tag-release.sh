#!/usr/bin/env bash
#
# Tags a commit as a release: creates an annotated git tag `v<version>` from
# the version currently set in the root `VERSION` file.
#
# Usage:
#   ci/tag-release.sh [--push] [--force] [<ref>]
#       Tag <ref> (default: HEAD) as "v<version>".
#       --push   Also push the created tag to 'origin'.
#       --force  Tag even if nothing release-relevant changed since the
#                previous release tag.
#
#   ci/tag-release.sh --check
#       Report whether anything release-relevant changed since the previous
#       release tag, without tagging. Exits non-zero if not.

source "$(dirname "$(realpath "$0")")/lib.sh"
stage "Tag release"

source "${WORKSPACE}/tools/release-paths.sh"

# Version in the `VERSION` file as of $1 (a ref, default: the ref being tagged).
current_version() {
    local ref="${1:-${REF}}"
    git -C "${WORKSPACE}" show "${ref}:VERSION"
}

# Most recent release tag, or empty if there isn't one yet.
last_release_tag() {
    git -C "${WORKSPACE}" describe --tags --abbrev=0 --match 'v*' 2>/dev/null || true
}

# Whether any release-relevant path differs between $1 and $2.
release_relevant_changed() {
    local from="$1"
    local to="$2"
    ! git -C "${WORKSPACE}" diff --quiet "${from}" "${to}" -- "${RELEASE_RELEVANT_PATHS[@]}"
}

PUSH=false
FORCE=false
CHECK_ONLY=false
REF="HEAD"

for arg in "$@"; do
    case "${arg}" in
        --push)  PUSH=true ;;
        --force) FORCE=true ;;
        --check) CHECK_ONLY=true ;;
        -*)
            echo "Usage: $0 [--push] [--force] [<ref>] | --check" >&2
            exit 1
            ;;
        *) REF="${arg}" ;;
    esac
done

PREV_TAG=$(last_release_tag)

if [[ -n "${PREV_TAG}" ]] && ! release_relevant_changed "${PREV_TAG}" "${REF}"; then
    if [[ "${CHECK_ONLY}" == true ]]; then
        echo "Nothing release-relevant changed since ${PREV_TAG}." >&2
        exit 1
    fi
    if [[ "${FORCE}" != true ]]; then
        echo "Nothing release-relevant changed since ${PREV_TAG}; nothing to release (use --force to tag anyway)." >&2
        exit 1
    fi
fi

if [[ "${CHECK_ONLY}" == true ]]; then
    echo "Release-relevant changes found since ${PREV_TAG:-the beginning of history}."
    exit 0
fi

VERSION=$(current_version)
TAG="v${VERSION}"

if git -C "${WORKSPACE}" rev-parse "${TAG}" >/dev/null 2>&1; then
    echo "Tag '${TAG}' already exists." >&2
    exit 1
fi

git -C "${WORKSPACE}" tag -a "${TAG}" -m "Release ${TAG}" "${REF}"
echo "Tagged ${REF} as ${TAG}."

if [[ "${PUSH}" == true ]]; then
    git -C "${WORKSPACE}" push origin "${TAG}"
    echo "Pushed ${TAG} to origin."
fi

# Trivial hook for a future workflow step to pick up the tag name.
if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    echo "tag=${TAG}" >> "${GITHUB_OUTPUT}"
fi
