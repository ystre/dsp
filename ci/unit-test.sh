#!/usr/bin/env bash

source "$(dirname "$(realpath "$0")")/lib.sh"
stage "Unit test"

ctest --test-dir "${BUILD_DEBUG}" --output-on-failure --no-tests=error
