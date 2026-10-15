#!/usr/bin/env bash
#
# Clean copy of the repository into $WORKSPACE: tracked and new files plus the
# git metadata, without anything ignored (build trees, results, downloaded
# chart dependencies). Third-party chart archives are carried over when present
# so that the chart stage works offline.
#
# The workspace must not exist yet: ci/e2e.sh gives every run its own
# timestamped directory, so runs never overwrite each other. Disk usage of the
# accumulated runs: tools/workspace-usage.sh.

source "$(dirname "$(realpath "$0")")/lib.sh"
stage "Prepare"

[[ "${WORKSPACE}" != "${PROJECT_DIR}" ]] || die "WORKSPACE is the repository itself; set it to a scratch directory"

[[ ! -e "${WORKSPACE}" ]] || die "${WORKSPACE} already exists; every run needs a fresh workspace"

info "Clean copy of ${PROJECT_DIR} into ${WORKSPACE}"
mkdir -p "${WORKSPACE}"
# shellcheck disable=SC2119  # no path filter: every file
WORKSPACE="${PROJECT_DIR}" source_files | tar -C "${PROJECT_DIR}" --null -T - -cf - | tar -xf - -C "${WORKSPACE}"
cp -a "${PROJECT_DIR}/.git" "${WORKSPACE}/.git"

shopt -s nullglob
for tgz in "${PROJECT_DIR}"/deploy/helm/deps/charts/*.tgz; do
    mkdir -p "${WORKSPACE}/deploy/helm/deps/charts"
    cp "${tgz}" "${WORKSPACE}/deploy/helm/deps/charts/"
    info "carried over $(basename "${tgz}")"
done
