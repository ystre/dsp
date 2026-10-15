# shellcheck shell=bash
# shellcheck disable=SC2034  # used by the scripts that source this file
#
# Shared list of paths whose changes are "release-relevant", i.e. they change
# what is shipped (binaries, image, charts, CSAR), and so warrant a version
# bump and count towards "is there something new to release?".
# Sourced by ci/check-version-bump.sh and ci/tag-release.sh.

RELEASE_RELEVANT_PATHS=(
    'libproto'
    'libadmin'
    'server'
    'loadgen'
    'cmake'
    'contract'
    'csar'
    'deploy/helm'
    'env/runtime.Dockerfile'
    'CMakeLists.txt'
    'conanfile.py'
    'dependencies.cmake'
    'project.env'
    'settings.cmake'
)
