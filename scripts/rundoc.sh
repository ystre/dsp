#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(realpath "${0%%/*}")"
PROJECT_DIR=$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)
AWK_SCRIPT="${PROJECT_DIR}/scripts/rundoc.awk"

COLOR_DEF='\033[0m'
COLOR_PURPLE='\033[1;35m'
COLOR_GRAY='\033[1;30m'
COLOR_ITALIC='\033[3m'

function usage() {
    >&2 cat << EOF
Usage: $0 [OPTIONS] CMD ...

Run the given commands from the AsciiDoc. Commands are defined in code blocks
that are tagged as commands.

For example:

// CMD: cmd-echo
[source,bash]
----
echo example
----

Note: this script looks for \`adoc\` files directly in \`doc\` directory and will
not descend into subdirectories.
EOF
}

f_echo_only=false
cmds=""

function parse_args() {
    while [[ $# -gt 0 ]]; do
      case "$1" in
        -e|--echo-only)     f_echo_only=true;                   shift;;
        -h|--help)          usage;                              shift;      exit 0 ;;
        -l|--list-cmds)     list-cmds;                          shift;      exit 0 ;;
        -*)                 msg "Invalid option: $1"; usage;    shift;      exit 1 ;;
        *)                  cmds=$*;                            break;
      esac
    done
}

function msg() {
    >&2 echo -e "$*"
}

function list-cmds() {
    local cmds
    mapfile -t cmds < <(sed --quiet 's#^// CMD: \(.*\)#\1#p' "${PROJECT_DIR}/doc/"*.adoc | sort)

    # shellcheck disable=SC2068 # Commands are single "words", so they can be safely split
    for cmd in ${cmds[@]}; do
        msg "${cmd##*/}"
    done
}

function main() {
    # shellcheck disable=SC2068 # Commands are single "words", so they can be safely split
    for cmd_name in ${cmds[@]}; do
        doc=$(grep --files-with-matches -E "^// CMD: ${cmd_name}$" "${PROJECT_DIR}/doc/"*.adoc)

        # TODO(safety): check for only one command occurence and if it exists
        cmd=$(awk -v cmd="${cmd_name}" -f "${AWK_SCRIPT}" "${doc}")

        # TODO(feat): log line number also
        msg "[$(date --rfc-3339=ns) rundoc.sh] Running command: ${COLOR_PURPLE}${cmd_name}${COLOR_DEF} (from document ${doc})"
        msg "${COLOR_GRAY}${COLOR_ITALIC}Command details:\n${cmd}\n${COLOR_DEF}"

        if [[ ${f_echo_only} == false ]]; then
            eval "${cmd}"
        fi
    done
}

parse_args "$@"
main
