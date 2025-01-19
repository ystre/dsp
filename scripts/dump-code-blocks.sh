#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(realpath "${0%%/*}")"
PROJECT_DIR=$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)
AWK_SCRIPT="${PROJECT_DIR}/scripts/dump-code-blocks.awk"

function main() {
    for doc in "${PROJECT_DIR}/doc/"*; do
        awk -f "${AWK_SCRIPT}" "${doc}"
    done
}

main
