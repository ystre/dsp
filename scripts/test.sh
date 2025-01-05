#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(realpath "${0%%/*}")"
PROJECT_DIR=$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)
BUILD_DIR="${PROJECT_DIR}/build"
JOBS=$(($(nproc) / 2))

function usage() {
    >&2 cat << EOF
Usage: $0 [-c|--clean]

Compiles all targets in debug and release mode and runs unit tests.

Build matrix:
- Debug, Release

Dependencies:
- cmake
- baldr (cargo install baldr)
EOF
}

f_clean=false

function parse_args() {
    while [[ $# -gt 0 ]]; do
      case "$1" in
        -c|--clean)     f_clean=true;                       shift;;
        -h|--help)      usage;                              shift;      exit 0 ;;
        *)              msg "Invalid option: $1"; usage;    shift;      exit 1 ;;
      esac
    done
}

function msg() {
    >&2 echo -e "$*"
}

function build-and-test() {
    local build_type="$1"

    baldr --project "${PROJECT_DIR}" -b "${build_type}" -t all -j "${JOBS}"
    ctest --output-on-failure --test-dir "${BUILD_DIR}/${build_type,,}"
}

function main() {
    if [[ $f_clean == true ]]; then
        msg "Clean build"
        rm -rf "${PROJECT_DIR}/build"
    fi

    for type in {Debug,Release}; do
        build-and-test "${type}"
    done

    msg "\nRunning AsciiDoc..."

    asciidoc "${PROJECT_DIR}/doc/dsp.adoc"
}

parse_args "$@"
main
