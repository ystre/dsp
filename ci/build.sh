#!/usr/bin/env bash
#
# Debug build with AddressSanitizer + UndefinedBehaviorSanitizer; the unit and
# functional tests run on it.

source "$(dirname "$(realpath "$0")")/lib.sh"
stage "Build (debug, asan)"

"${WORKSPACE}/tools/configure.sh" "${BUILD_DEBUG}" Debug -DSANITIZERS=asan
cmake --build "${BUILD_DEBUG}"
