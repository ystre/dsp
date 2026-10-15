#!/usr/bin/env bash
#
# Runs the `smoke` scenario through the benchmarking framework (tools/bench)
# against the release build, and compares it with bench/baselines/smoke.json
# when there is one. Exercises the whole pipeline: scenario -> runner -> load
# generator -> server metrics -> summary -> compare. Warning only unless
# CI_PERF_GATE=1 (numbers depend on the host).

source "$(dirname "$(realpath "$0")")/lib.sh"
stage "Performance smoke"

run_id="perf-smoke-$(date +%Y%m%d-%H%M%S)"
"${PY[@]}" "${WORKSPACE}/tools/bench/run.py" --scenario smoke --target local \
    --bin-dir "${BUILD_RELEASE}/bin" --out "${RESULTS}" --run-id "${run_id}"

baseline="${WORKSPACE}/bench/baselines/smoke.json"
if [[ -f "${baseline}" ]]; then
    # Smoke runs are short and the host is shared: a loose default threshold (the scenario's own
    # thresholds still apply to the figures it names).
    compare_args=(--threshold "${CI_SMOKE_THRESHOLD:-0.30}")
    [[ "${CI_PERF_GATE:-0}" == 1 ]] || compare_args+=(--warn-only)
    "${PY[@]}" "${WORKSPACE}/tools/bench/compare.py" "${baseline}" "${RESULTS}/${run_id}" "${compare_args[@]}"
else
    info "no baseline (bench/baselines/smoke.json); recorded only"
fi
"${PY[@]}" "${WORKSPACE}/tools/bench/report.py" "${RESULTS}/${run_id}" --title "Performance smoke" \
    --output "${RESULTS}/${run_id}/report.adoc"
