#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(realpath "${0%%/*}")"
PROJECT_DIR=$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)
AWK_SCRIPT="${PROJECT_DIR}/scripts/rundoc.awk"

cmd_name=$1

if [[ ${2:-""} == "echo" ]]; then
    echo_only=1
else
    echo_only=0
fi

doc=$(ag --files-with-matches "${cmd_name}" "${PROJECT_DIR}/doc" --extension=adoc)

# TODO(safety): check for only one command occurence and if it exists
cmd=$(awk -v cmd="${cmd_name}" -f "${AWK_SCRIPT}" "${doc}")

# TODO(feat): log line number also
echo "Running command: \`${cmd_name}\` (from document ${doc})"
echo -e "Command details:\n${cmd}\n"

if [[ ${echo_only} == 0 ]]; then
    eval "${cmd}"
fi
