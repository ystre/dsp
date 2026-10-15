#!/usr/bin/env bash
#
# The metrics exported by the binaries conform to contract/metrics.toml: no
# undocumented family, every Phase 1 family present with the contract labels.

set -euo pipefail
source "$(dirname "$(realpath "$0")")/lib.sh"

start_server
LG_PORT="$(free_port)"
"${LOADGEN}" --target "127.0.0.1:${PORT}" --type echo --count 1000 \
    --metrics-port "${LG_PORT}" --linger 2 --output /dev/null &
LG_PID=$!
for _ in $(seq 50); do
    curl --silent --fail "http://127.0.0.1:${LG_PORT}/metrics" > "${TEST_TMP}/loadgen.prom" 2>/dev/null && break
    sleep 0.1
done
wait "${LG_PID}" || fail "loadgen exited with $?"

scrape > "${TEST_TMP}/server.prom"
${PY} "${PROJECT_DIR}/tools/contract.py" check --component server "${TEST_TMP}/server.prom" || fail "server metrics"
${PY} "${PROJECT_DIR}/tools/contract.py" check --component loadgen "${TEST_TMP}/loadgen.prom" || fail "loadgen metrics"

stop_server
msg "metrics-contract: OK"
