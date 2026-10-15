#!/bin/bash
#
# Bumps the version in the root `VERSION` file (and, through
# tools/sync-version.sh, in every place that has to repeat it: Helm charts).
#
# Usage:
#   tools/bump-version.sh [major|minor|patch|auto]
#       Bump the version (default: patch). 'auto' inspects the latest commit
#       message (Conventional Commits) to pick the level: a '!' after the type
#       or a 'BREAKING CHANGE' footer bumps major, a 'feat:' tag bumps minor,
#       anything else bumps patch.
#
#   tools/bump-version.sh --check
#       Sanity-check that the version was bumped compared to the previous git
#       revision, without modifying anything.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
VERSION_FILE="${PROJECT_DIR}/VERSION"

current_version() {
    cat "$1"
}

# Determine the bump level ('major'/'minor'/'patch') from the latest commit
# message, following Conventional Commits: a '!' right after the type/scope,
# or a 'BREAKING CHANGE' footer, means 'major'; a 'feat:' tag means 'minor';
# anything else defaults to 'patch'.
auto_bump_level() {
    local message
    message=$(git log -1 --pretty=%B)

    if grep -qP '^\w+(\([^)]*\))?!:' <<< "${message}" || grep -qP 'BREAKING CHANGE' <<< "${message}"; then
        echo "major"
    elif grep -qiP '^feat(\([^)]*\))?:' <<< "${message}"; then
        echo "minor"
    else
        echo "patch"
    fi
}

if [[ "${1:-}" == "--check" ]]; then
    OLD_VERSION=$(git show "HEAD:VERSION" 2>/dev/null || true)
    NEW_VERSION=$(current_version "${VERSION_FILE}")

    if [[ -z "${OLD_VERSION}" || -z "${NEW_VERSION}" ]]; then
        echo "Could not determine version to compare." >&2
        exit 1
    fi

    if [[ "${OLD_VERSION}" == "${NEW_VERSION}" ]]; then
        echo "Version was not bumped (still ${NEW_VERSION})." >&2
        exit 1
    fi

    echo "Version bumped: ${OLD_VERSION} -> ${NEW_VERSION}"
    exit 0
fi

PART="${1:-patch}"
if [[ "${PART}" == "auto" ]]; then
    PART=$(auto_bump_level)
    echo "Auto-detected bump level: ${PART}"
fi

VERSION=$(current_version "${VERSION_FILE}")
IFS='.' read -r MAJOR MINOR PATCH <<< "${VERSION}"

case "${PART}" in
    major)
        MAJOR=$((MAJOR + 1))
        MINOR=0
        PATCH=0
        ;;
    minor)
        MINOR=$((MINOR + 1))
        PATCH=0
        ;;
    patch)
        PATCH=$((PATCH + 1))
        ;;
    *)
        echo "Usage: $0 [major|minor|patch|auto] | --check" >&2
        exit 1
        ;;
esac

NEW_VERSION="${MAJOR}.${MINOR}.${PATCH}"
echo "${NEW_VERSION}" > "${VERSION_FILE}"
"${SCRIPT_DIR}/sync-version.sh"

echo "Version bumped: ${VERSION} -> ${NEW_VERSION}"
