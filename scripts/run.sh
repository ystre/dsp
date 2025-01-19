#!/usr/bin/env bash

export COLOR_DEF='\033[0m'
export COLOR_RED='\033[1;31m'
export COLOR_GREEN='\033[1;32m'

set -euo pipefail

G_SCRIPT_DIR="$(realpath "${0%/*}")"
G_PROJECT_DIR=$(git -C "${G_SCRIPT_DIR}" rev-parse --show-toplevel)
G_BUILD_DIR="${G_PROJECT_DIR}/build"

STATUS_CODE=0

function usage() {
    >&2 cat << EOF
Usage: $0 STAGE ...

Run the given stages.

Stages are defined in <NAME>.stage.sh scripts. Entrypoint for stages is
\`stage-entry()\`. They are sourced into this script, so all variables are
avaliable for the stages; they are also shared between stages.
EOF
}

f_verbose=false
stages=""

function parse_args() {
    while [[ $# -gt 0 ]]; do
      case "$1" in
        -v|--verbose)       f_verbose=true;                     shift;;
        -h|--help)          usage;                              shift;      exit 0 ;;
        -l|--list-stages)   list-stages;                        shift;      exit 0 ;;
        -*)                 msg "Invalid option: $1"; usage;    shift;      exit 1 ;;
        *)                  stages=$*;                          break;
      esac
    done
}

function msg() {
    >&2 echo -e "[$(date --rfc-3339=ns) run.sh] $*"
}

function list-stages() {
    local stages
    mapfile -t stages < <(find "${G_PROJECT_DIR}/scripts" -name '*.stage.sh')

    # shellcheck disable=SC2068 # Commands are single "words", so they can be safely split
    for stage in ${stages[@]}; do
        msg "${stage##*/}"
    done
}

function main() {
    # shellcheck disable=SC2068 # Commands are single "words", so they can be safely split
    for stage in ${stages[@]}; do
        # shellcheck source=/dev/null
        if ! source "${G_PROJECT_DIR}/scripts/${stage}.stage.sh"; then
            msg "Failed to load stage \`${stage}\`"!
            exit 1
        fi

        if ! stage-entry; then
            msg "${COLOR_RED}Stage \`${stage}\` failed${COLOR_DEF}"
            STATUS_CODE=1
        fi
        trap - exit
    done

    return ${STATUS_CODE}
}

parse_args "$@"
main
