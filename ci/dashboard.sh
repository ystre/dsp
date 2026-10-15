#!/usr/bin/env bash
#
# The committed Grafana dashboard is generated from contract/metrics.toml and
# references contract metrics only.

source "$(dirname "$(realpath "$0")")/lib.sh"
stage "Dashboard"

"${PY[@]}" "${WORKSPACE}/deploy/grafana/build_dashboard.py" --check
