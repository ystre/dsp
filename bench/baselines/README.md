Reference results that CI compares against (`ci/micro-bench.sh`,
`ci/perf-smoke.sh`, via `tools/bench/compare.py`). They are host-specific: the
`env` section of each file names the machine they were recorded on. Refresh
them deliberately, after an intended performance change, by copying a run's
`summary.json` (or Google Benchmark JSON) here; see doc/benchmarking.adoc,
"Baselines".
