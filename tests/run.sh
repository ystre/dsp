#!/usr/bin/env bash
#
# Runs every functional test.
#
# Usage: BIN_DIR=<dir with the binaries> tests/run.sh [test-name...]

set -euo pipefail

TESTS_DIR="$(dirname "$(realpath "$0")")"
TESTS=("$@")
[[ ${#TESTS[@]} -eq 0 ]] && TESTS=(smoke metrics-contract protocol-errors)

failed=0
for t in "${TESTS[@]}"; do
    echo "--- ${t}"
    "${TESTS_DIR}/${t}.sh" || failed=$((failed + 1))
done

if (( failed )); then
    echo "${failed} functional test(s) failed" >&2
    exit 1
fi
echo "All ${#TESTS[@]} functional tests passed"
