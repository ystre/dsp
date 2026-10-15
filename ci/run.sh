#!/usr/bin/env bash
#
# Runs selected stages, by default in the repository itself (developer loop):
#
#   ci/run.sh build unit-test function-test
#   WORKSPACE=/tmp/ws ci/run.sh prepare build

set -euo pipefail

CI_DIR="$(dirname "$(realpath "$0")")"
if (( $# == 0 )); then
    stages=()
    for script in "${CI_DIR}"/*.sh; do
        name="$(basename "${script}" .sh)"
        [[ "${name}" =~ ^(lib|run|e2e|in-container)$ ]] || stages+=("${name}")
    done
    echo "usage: ci/run.sh <stage>...  (stages: ${stages[*]})" >&2
    exit 1
fi
for stage in "$@"; do
    "${CI_DIR}/${stage}.sh"
done
