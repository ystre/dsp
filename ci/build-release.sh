#!/usr/bin/env bash
#
# Optimised build that is benchmarked, packaged and shipped in the image.

source "$(dirname "$(realpath "$0")")/lib.sh"
stage "Build (release)"

"${WORKSPACE}/tools/configure.sh" "${BUILD_RELEASE}" Release
cmake --build "${BUILD_RELEASE}"
