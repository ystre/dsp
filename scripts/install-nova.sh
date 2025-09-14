#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(realpath "${0%%/*}")"
PROJECT_DIR=$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)

BUILD_DIR="${PROJECT_DIR}/.tmp/libnova"

mkdir --parent "${BUILD_DIR}"

cmake -S "${PROJECT_DIR}/deps/nova-cpp/libnova" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="${PROJECT_DIR}/conan_provider.cmake"

cmake --build "${BUILD_DIR}"
cmake --install "${BUILD_DIR}" --prefix ~/.local
