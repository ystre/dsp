#!/usr/bin/env bash
#
# Server + load generator end to end, for every message type: the load
# generator must report a clean run, and the server's counters must match
# exactly what the generator sent (no loss, no duplication).

set -euo pipefail
source "$(dirname "$(realpath "$0")")/lib.sh"

COUNT="${SMOKE_COUNT:-1000000}"

start_server
declare -A rx=( [data]=0 [echo_req]=0 [req]=0 )
for type in data echo req; do
    out="${TEST_TMP}/${type}.json"
    "${LOADGEN}" --target "127.0.0.1:${PORT}" --type "${type}" --count "${COUNT}" --payload fixed:200 --aux 64 \
        --scenario "smoke-${type}" --output "${out}" || fail "loadgen ${type} exited with $?"

    sent=$(json_get "${out}" 'd["sent"]["messages"]')
    received=$(json_get "${out}" 'd["received"]["messages"]')
    rate=$(json_get "${out}" 'round(d["throughput"]["messages_per_s"])')
    [[ "${sent}" == "${COUNT}" ]] || fail "${type}: sent ${sent} != ${COUNT}"
    if [[ "${type}" != data ]]; then
        [[ "${received}" == "${COUNT}" ]] || fail "${type}: received ${received} replies != ${COUNT}"
    fi
    key=$([[ "${type}" == echo ]] && echo echo_req || echo "${type}")
    rx[${key}]=$(( rx[${key}] + sent ))
    msg "${type}: ${sent} sent, ${received} replies, ${rate} msg/s"
done

scrape > "${TEST_TMP}/metrics.prom"
for type in data echo_req req; do
    got=$(metric_value "${TEST_TMP}/metrics.prom" "${PROJECT_NAME}_rx_messages_total" "type=\"${type}\"")
    [[ "${got}" == "${rx[${type}]}" ]] || fail "server rx_messages{type=${type}} = ${got}, loadgen sent ${rx[${type}]}"
done
accepted=$(metric_value "${TEST_TMP}/metrics.prom" "${PROJECT_NAME}_connections_accepted_total")
[[ "${accepted}" == 3 ]] || fail "accepted connections = ${accepted}, expected 3"

stop_server
msg "smoke: OK"
