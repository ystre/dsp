#!/usr/bin/env python3
"""Compare a benchmark run against a baseline.

    compare.py BASELINE CANDIDATE [--threshold 0.05] [--warn-only]

BASELINE and CANDIDATE are either
  * bench results: a results/<run-id> directory or its summary.json
    (tools/bench/run.py), or
  * Google Benchmark JSON (--benchmark_out_format=json, ci/micro-bench.sh).

For bench results, each figure (run.py FIGURES) is compared with the
threshold of the candidate's scenario ([scenario.thresholds]) or --threshold.
A change beyond the threshold in the bad direction is a regression; beyond it
in the good direction an improvement. For Google Benchmark, items_per_second
(or else real_time) of every benchmark present in both files is compared.

Exit code: 0 no regression (or --warn-only), 1 regression, 2 incomparable input.
"""

import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from run import FIGURES  # noqa: E402


def load(path: str) -> dict:
    p = pathlib.Path(path)
    if p.is_dir():
        p = p / "summary.json"
    return json.loads(p.read_text())


def kind(doc: dict) -> str:
    if doc.get("schema") == "bench-result/1":
        return "bench"
    if "benchmarks" in doc and "context" in doc:
        return "gbench"
    raise SystemExit(f"unrecognised input (schema {doc.get('schema')!r})")


def bench_rows(base: dict, cand: dict, default: float) -> list[tuple]:
    if base["scenario"]["name"] != cand["scenario"]["name"]:
        print(f"warning: different scenarios ({base['scenario']['name']} vs {cand['scenario']['name']})",
              file=sys.stderr)
    thresholds = cand["scenario"].get("thresholds", {})
    rows = []
    for name, (direction, _) in FIGURES.items():
        a, b = base["figures"].get(name), cand["figures"].get(name)
        if a is None or b is None:
            continue
        rows.append((name, a, b, direction, thresholds.get(name, default)))
    return rows


def gbench_rows(base: dict, cand: dict, default: float) -> list[tuple]:
    def index(doc):
        # Repeated runs (--benchmark_repetitions): compare the medians; otherwise the single runs.
        medians = {b["run_name"]: b for b in doc["benchmarks"]
                   if b.get("run_type") == "aggregate" and b.get("aggregate_name") == "median"}
        if medians:
            return medians
        return {b["name"]: b for b in doc["benchmarks"] if b.get("run_type", "iteration") == "iteration"}

    a_idx, b_idx = index(base), index(cand)
    rows = []
    for name in sorted(a_idx.keys() & b_idx.keys()):
        a, b = a_idx[name], b_idx[name]
        if "items_per_second" in a and "items_per_second" in b:
            rows.append((f"{name} items/s", a["items_per_second"], b["items_per_second"], +1, default))
        else:
            rows.append((f"{name} real_time", a["real_time"], b["real_time"], -1, default))
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("baseline")
    ap.add_argument("candidate")
    ap.add_argument("--threshold", type=float, default=0.05, help="default relative threshold [0.05]")
    ap.add_argument("--warn-only", action="store_true", help="report regressions but exit 0")
    args = ap.parse_args()

    base, cand = load(args.baseline), load(args.candidate)
    if kind(base) != kind(cand):
        print("cannot compare a bench result with Google Benchmark output", file=sys.stderr)
        return 2
    rows = (bench_rows if kind(base) == "bench" else gbench_rows)(base, cand, args.threshold)
    if not rows:
        print("nothing to compare", file=sys.stderr)
        return 2

    if kind(cand) == "bench" and not (cand["valid"] and cand["clean"]):
        print("warning: the candidate run is not clean/valid", file=sys.stderr)

    regressions = 0
    print(f"{'figure':40} {'baseline':>16} {'candidate':>16} {'change':>8}  verdict (threshold)")
    for name, a, b, direction, threshold in rows:
        change = (b - a) / a if a else 0.0
        better = change * direction
        if better < -threshold:
            verdict = "REGRESSION"
            regressions += 1
        elif better > threshold:
            verdict = "improvement"
        else:
            verdict = "same"
        print(f"{name:40} {a:>16,.1f} {b:>16,.1f} {change:>+8.1%}  {verdict} ({threshold:.0%})")

    if regressions:
        print(f"{regressions} regression(s)" + (" (warn only)" if args.warn_only else ""), file=sys.stderr)
        return 0 if args.warn_only else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
