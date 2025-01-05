#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(realpath "${0%%/*}")"
PROJECT_DIR=$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)
BUILD_TYPE=Release
BUILD_DIR="${PROJECT_DIR}/build/${BUILD_TYPE}"
JOBS=$(($(nproc) / 2))
LOG_DIR=/tmp
LOG_SVC="${LOG_DIR}/dsp-ft-svc.log"
LOG_SIM="${LOG_DIR}/dsp-ft-sim.log"

function usage() {
    >&2 cat << EOF
Usage: $0 [-b|--build]

Functional testing... in-development.

Dependencies:
- cmake
- baldr (cargo install baldr)
- test tools (TODO: name them)
EOF
}

f_build=false

function parse_args() {
    while [[ $# -gt 0 ]]; do
      case "$1" in
        -b|--build)     f_build=true;                       shift;;
        -h|--help)      usage;                              shift;      exit 0 ;;
        *)              msg "Invalid option: $1"; usage;    shift;      exit 1 ;;
      esac
    done
}

function msg() {
    >&2 echo "$*"
}

function main() {
    if [[ $f_build == true ]]; then
        msg "Building svc target..."
        baldr --project "${PROJECT_DIR}" -b "${BUILD_TYPE}" -t svc -j "${JOBS}"
    fi

    # TODO(feat): Call Docker compose for dependencies.
    # TODO(feat): Support for different `dsp.yaml` configs.

    "${BUILD_DIR,,}/src/svc/svc" > "${LOG_SVC}" 2>&1&
    sleep 1     # TODO(feat): Reconnect support in simulator.
    "${BUILD_DIR,,}/src/svc/sim" > "${LOG_SIM}" 2>&1&
    "${BUILD_DIR,,}/src/tools/tcp-client" --address localhost:7200 --count 20000000 --size 200 --batch 10

    metrics=$(curl --silent localhost:9555/metrics | ag -v '^#')

    pkill --signal SIGINT --exact sim
    pkill --signal SIGINT --exact svc
    msg "Testing has finished"

    sleep 2
    msg "Test results"

    msg "Speed test:"
    ag --nocolor --only-matching "Summary:.*" "${LOG_SVC}"

    msg "Metrics:"
    echo "${metrics}"
}

parse_args "$@"
main
