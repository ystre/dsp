#!/usr/bin/env bash
#
# Malformed input closes the connection and is counted by reason; the server
# keeps serving other connections.

set -euo pipefail
source "$(dirname "$(realpath "$0")")/lib.sh"

start_server

# Sends one 32-byte header built by Python's struct (independent of libproto),
# then reports whether the server closed the connection.
send_header() {
    ${PY} - "${PORT}" "$@" <<'PY'
import socket, struct, sys
port, magic, version, typ, length = (int(x, 0) for x in sys.argv[1:])
s = socket.create_connection(("127.0.0.1", port))
s.sendall(struct.pack("<HBBIQQII", magic, version, typ, length, 0, 0, 0, 0))
s.settimeout(2)
print("closed" if s.recv(1) == b"" else "open")
PY
}

[[ $(send_header 0x1234 1 1 0) == closed ]]         || fail "bad magic: connection not closed"
[[ $(send_header 0xD5F0 9 1 0) == closed ]]         || fail "bad version: connection not closed"
[[ $(send_header 0xD5F0 1 1 0x7fffffff) == closed ]] || fail "too large: connection not closed"
[[ $(send_header 0xD5F0 1 99 0) == closed ]]        || fail "bad type: connection not closed"

scrape > "${TEST_TMP}/metrics.prom"
for reason in bad_magic bad_version too_large bad_type; do
    got=$(metric_value "${TEST_TMP}/metrics.prom" "${PROJECT_NAME}_protocol_errors_total" "reason=\"${reason}\"")
    [[ "${got}" == 1 ]] || fail "protocol_errors_total{reason=${reason}} = ${got}, expected 1"
done

# Still serving.
"${LOADGEN}" --target "127.0.0.1:${PORT}" --type echo --count 1000 --output /dev/null || fail "server stopped serving"

stop_server
msg "protocol-errors: OK"
