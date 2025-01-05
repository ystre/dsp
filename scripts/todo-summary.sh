#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(realpath "${0%%/*}")"
PROJECT_DIR=$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)

function usage() {
    >&2 cat << EOF
Usage: $0

Summary of todos.
EOF
}

function parse_args() {
    while [[ $# -gt 0 ]]; do
      case "$1" in
        -h|--help)      usage;                              shift;      exit 0 ;;
        *)              msg "Invalid option: $1"; usage;    shift;      exit 1 ;;
      esac
    done
}

function msg() {
    >&2 echo "$*"
}

function main() {
    for f in $(ag --files-with-matches "TODO" "${PROJECT_DIR}"); do
        echo "${f}"
    done
}

parse_args "$@"
main
