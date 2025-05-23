#!/usr/bin/env bash

# TODO: Several errors are swallowed inside the stage functions.

set -euo pipefail

export COLOR_DEF='\033[0m'
export COLOR_RED='\033[1;31m'
export COLOR_GREEN='\033[1;32m'
export COLOR_YELLOW='\033[1;33m'
export COLOR_LIGHT_BLUE='\033[1;34m'
export COLOR_BLUE='\033[0;34m'

G_SCRIPT_DIR="$(realpath "${0%/*}")"
G_PROJECT_DIR=$(git -C "${G_SCRIPT_DIR}" rev-parse --show-toplevel)
G_BUILD_DIR="${G_PROJECT_DIR}/build"
G_ARTIFACT_DIR="${G_PROJECT_DIR}/.tmp"
G_REPORT_DIR="${G_ARTIFACT_DIR}/reports"
G_INSTALL_DIR="${G_ARTIFACT_DIR}/install"

STATUS_CODE=0
INTERRUPTED=0

trap on-interrupt SIGINT

function on-interrupt() {
    INTERRUPTED=1
}

function usage() {
    >&2 cat << EOF
Usage: $0 STAGE ...

Run the given stages.

Stages are defined in <NAME>.stage.sh scripts. Entrypoint for stages is
\`stage-entry()\`. They are sourced into this script, so all variables are
avaliable for the stages; they are also shared between stages.

Exit codes:
1   Failed to load/run stage
2   Interrupted
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
    >&2 echo -e "[$(date '+%F %T.%6N %:z')] [run.sh @$$] $*"
}

function list-stages() {
    local stages
    mapfile -t stages < <(find "${G_PROJECT_DIR}/scripts" -name '*.stage.sh')

    local stage
    # shellcheck disable=SC2068 # Commands are single "words", so they can be safely split
    for stage in ${stages[@]}; do
        msg "${stage##*/}"
    done
}

function init-stage() {
    mkdir --parent \
        "${G_ARTIFACT_DIR}" \
        "${G_REPORT_DIR}" \
        "${G_BUILD_DIR}"
}

function run-stage() {
    local stage="${1}"

    local start_time
    local finish_time
    local stage_time
    local exit_code

    start_time=$(date +%s.%N)

    # shellcheck source=/dev/null
    if ! source "${G_PROJECT_DIR}/scripts/${stage}.stage.sh"; then
        msg "Failed to load stage \`${stage}\`"!
        exit 1
    fi

    msg "${COLOR_LIGHT_BLUE}Stage \`${stage}\`${COLOR_DEF}"

    if ! stage-entry; then
        exit_code=1
    else
        exit_code=0
    fi

    finish_time=$(date +%s.%N)
    stage_time="$(bc -l <<< "scale=3; (${finish_time} - ${start_time}) / 1") sec(s)"

    if [[ ${exit_code} -ne 0 ]]; then
        msg "${COLOR_RED}Stage \`${stage}\` failed${COLOR_DEF} after ${stage_time}"
        STATUS_CODE=1
    else
        if [[ "${INTERRUPTED}" -eq 1 ]]; then
            msg "${COLOR_YELLOW}Stage \`${stage}\` interrupted${COLOR_DEF} after ${stage_time}"
            exit 2
        else
            msg "${COLOR_BLUE}Stage \`${stage}\` finished successfully${COLOR_DEF} after ${stage_time}"
        fi
    fi
    trap - exit
}

function main() {
    init-stage

    # shellcheck disable=SC2068 # Commands are single "words", so they can be safely split
    for stage in ${stages[@]}; do
        run-stage "${stage}"
    done

    return ${STATUS_CODE}
}

parse_args "$@"
main
