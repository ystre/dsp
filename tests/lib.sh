# shellcheck shell=bash
# shellcheck disable=SC2034  # variables are used by the tests that source this file
#
# Shared helpers for the functional tests. Sourced, not executed.
#
# Expects BIN_DIR (directory with the built binaries) in the environment; the
# binaries are named after project.env.

TESTS_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"
PROJECT_DIR="$(realpath "${TESTS_DIR}/..")"
# shellcheck source=../project.env
source "${PROJECT_DIR}/project.env"

: "${BIN_DIR:?BIN_DIR must point to the directory containing ${PROJECT_NAME}-server and ${PROJECT_NAME}-loadgen}"
SERVER="${BIN_DIR}/${PROJECT_NAME}-server"
LOADGEN="${BIN_DIR}/${PROJECT_NAME}-loadgen"
PY="python3 -I"

TEST_TMP="$(mktemp -d)"
SERVER_PID=""

cleanup() {
    [[ -n "${SERVER_PID}" ]] && kill "${SERVER_PID}" 2>/dev/null && wait "${SERVER_PID}" 2>/dev/null
    rm -rf "${TEST_TMP}"
}
trap cleanup EXIT

msg()  { printf '  %s\n' "$*"; }
fail() { printf 'FAIL: %s\n' "$*" >&2; [[ -f "${TEST_TMP}/server.log" ]] && sed 's/^/  server| /' "${TEST_TMP}/server.log" >&2; exit 1; }

free_port() {
    ${PY} -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])'
}

# Starts the server on free ports; sets PORT, ADMIN_PORT, SERVER_PID.
start_server() {
    PORT="$(free_port)"
    ADMIN_PORT="$(free_port)"
    "${SERVER}" --port "${PORT}" --admin-port "${ADMIN_PORT}" > "${TEST_TMP}/server.log" 2>&1 &
    SERVER_PID=$!
    for _ in $(seq 50); do
        curl --silent --fail "http://127.0.0.1:${ADMIN_PORT}/readyz" >/dev/null 2>&1 && return 0
        sleep 0.1
    done
    fail "server did not become ready"
}

stop_server() {
    kill -TERM "${SERVER_PID}"
    wait "${SERVER_PID}" || fail "server exited with $? after SIGTERM"
    SERVER_PID=""
}

scrape() { curl --silent --fail "http://127.0.0.1:${ADMIN_PORT}/metrics"; }

# metric_value <exposition-file> <name> [label-substring]: sum of matching samples.
metric_value() {
    local file="$1" name="$2" labels="${3:-}"
    awk -v n="$name" -v l="$labels" '
        $0 !~ /^#/ { split($1, a, "{"); if (a[1] == n && index($1, l) > 0) s += $2 }
        END { printf "%d\n", s }' "${file}"
}

# json_get <file> <python-expression on d>
json_get() { ${PY} -c "import json,sys; d=json.load(open(sys.argv[1])); print($2)" "$1"; }
