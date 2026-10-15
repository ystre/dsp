#!/usr/bin/env bash
#
# Functional tests (tests/run.sh) against the sanitizer build: end-to-end
# message accounting, metrics contract, protocol error handling.

source "$(dirname "$(realpath "$0")")/lib.sh"
stage "Function test"

BIN_DIR="${BUILD_DEBUG}/bin" "${WORKSPACE}/tests/run.sh"
