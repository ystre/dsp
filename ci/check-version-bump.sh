#!/usr/bin/env bash
#
# Checks that the root `VERSION` file was bumped if a release-relevant path
# changed since the base branch.
#
# The base to compare against is, in order: $CI_BASE_REF (manual override),
# 'origin/<GITHUB_BASE_REF>' (what a future GitHub Actions pull_request
# workflow would set), 'origin/master', then 'master'. If none resolves
# (e.g. running against a lone 'master' checkout), the check is skipped,
# not failed - there is nothing meaningful to compare against.

source "$(dirname "$(realpath "$0")")/lib.sh"
cd "${WORKSPACE}"
info "Version bump check"

source "${WORKSPACE}/tools/release-paths.sh"

resolve_base() {
    local candidates=(
        "${CI_BASE_REF:-}"
        "${GITHUB_BASE_REF:+origin/${GITHUB_BASE_REF}}"
        "origin/master"
        "master"
    )
    for ref in "${candidates[@]}"; do
        [[ -z "${ref}" ]] && continue
        if git rev-parse --verify --quiet "${ref}" >/dev/null; then
            echo "${ref}"
            return 0
        fi
    done
    return 1
}

if ! BASE="$(resolve_base)"; then
    info "No base ref found (checked \$CI_BASE_REF, origin/\$GITHUB_BASE_REF, origin/master, master); skipping."
    exit 0
fi

if ! MERGE_BASE=$(git merge-base "${BASE}" HEAD 2>/dev/null); then
    info "No common history with ${BASE} (or no commits yet); skipping."
    exit 0
fi

if git diff --quiet "${MERGE_BASE}" HEAD -- "${RELEASE_RELEVANT_PATHS[@]}"; then
    echo "No release-relevant changes since ${BASE} (${MERGE_BASE:0:12}); no version bump required."
    exit 0
fi

if git diff --quiet "${MERGE_BASE}" HEAD -- VERSION; then
    echo "Release-relevant changes found since ${BASE} (${MERGE_BASE:0:12}), but 'VERSION' was not bumped." >&2
    echo "Run tools/bump-version.sh [patch|minor|major|auto] and commit the result." >&2
    exit 1
fi

echo "Version bumped since ${BASE} (${MERGE_BASE:0:12})."
