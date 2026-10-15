#!/usr/bin/env bash
#
# Runs every micro-benchmark (bench-*) of the release build briefly, stores the
# Google Benchmark JSON in results/micro/, and compares it with the baseline in
# bench/baselines/micro/ when there is one (warning only: shared CI hosts are
# noisy; CI_PERF_GATE=1 turns regressions into failures).

source "$(dirname "$(realpath "$0")")/lib.sh"
stage "Micro-benchmarks"

OUT="${RESULTS}/micro"
mkdir -p "${OUT}"
compare_args=(--threshold "${CI_MICRO_THRESHOLD:-0.15}")
[[ "${CI_PERF_GATE:-0}" == 1 ]] || compare_args+=(--warn-only)

shopt -s nullglob
benches=("${BUILD_RELEASE}"/bin/bench-*)
(( ${#benches[@]} )) || die "no bench-* executables in ${BUILD_RELEASE}/bin"
for bench in "${benches[@]}"; do
    name="$(basename "${bench}")"
    "${bench}" --benchmark_min_time=0.2s --benchmark_repetitions=3 --benchmark_report_aggregates_only=true \
        --benchmark_out="${OUT}/${name}.json" --benchmark_out_format=json
    baseline="${WORKSPACE}/bench/baselines/micro/${name}.json"
    if [[ -f "${baseline}" ]]; then
        "${PY[@]}" "${WORKSPACE}/tools/bench/compare.py" "${baseline}" "${OUT}/${name}.json" "${compare_args[@]}"
    else
        info "${name}: no baseline (bench/baselines/micro/${name}.json); recorded only"
    fi
done
