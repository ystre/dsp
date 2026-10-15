"""Scenario catalog (bench/scenarios/*.toml): loading, defaults, capability checks."""

import pathlib
import sys
import tomllib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import project  # noqa: E402

SCENARIO_DIR = project.ROOT / "bench" / "scenarios"

# Load generator CLI defaults (doc/loadgen.adoc, "CLI"). A scenario overrides any of them.
LOADGEN_DEFAULTS = {
    "transport": "tcp",
    "mode": "closed-loop",
    "type": "data",
    "connections": 1,
    "threads": 1,
    "rate": 0,
    "payload": "fixed:200",
    "aux": 64,
    "batch": 64,
}

SCENARIO_DEFAULTS = {
    "description": "",
    "count": 0,
    "duration_s": 0,
    "warmup_s": 0,
    "repeat": 1,
    "server_args": [],
    "thresholds": {},
}


def load_all() -> dict[str, dict]:
    scenarios = {}
    for path in sorted(SCENARIO_DIR.glob("*.toml")):
        for raw in tomllib.loads(path.read_text()).get("scenario", []):
            sc = {**SCENARIO_DEFAULTS, **raw}
            sc["loadgen"] = {**LOADGEN_DEFAULTS, **raw.get("loadgen", {})}
            sc["local"] = {"server_cpus": "", "loadgen_cpus": "", **raw.get("local", {})}
            sc["file"] = str(path.relative_to(project.ROOT))
            if sc["name"] in scenarios:
                raise ValueError(f"duplicate scenario name {sc['name']!r} in {path}")
            if not sc["count"] and not sc["duration_s"]:
                raise ValueError(f"{sc['name']}: needs count or duration_s")
            scenarios[sc["name"]] = sc
    return scenarios


def get(name: str) -> dict:
    scenarios = load_all()
    if name not in scenarios:
        raise SystemExit(f"unknown scenario {name!r}; known: {', '.join(scenarios)}")
    return scenarios[name]


def unsupported(sc: dict, caps: dict) -> list[str]:
    """Reasons why the load generator described by `caps` cannot run `sc` (empty: it can)."""
    lg = sc["loadgen"]
    reasons = []
    if lg["transport"] not in caps.get("transports", []):
        reasons.append(f"transport {lg['transport']}")
    if lg["mode"] not in caps.get("modes", []):
        reasons.append(f"mode {lg['mode']}")
    if lg["type"] not in caps.get("types", []):
        reasons.append(f"type {lg['type']}")
    if lg["payload"].split(":", 1)[0] not in caps.get("payload", []):
        reasons.append(f"payload {lg['payload']}")
    if lg["connections"] > caps.get("max_connections", 1):
        reasons.append(f"{lg['connections']} connections")
    if lg["threads"] > caps.get("max_threads", 1):
        reasons.append(f"{lg['threads']} threads")
    if lg["rate"] and not caps.get("rate"):
        reasons.append("rate limiting")
    if sc["warmup_s"] and not caps.get("warmup"):
        reasons.append("warm-up exclusion")
    if any(k.startswith("latency_ns.") for k in sc["thresholds"]) and not caps.get("latency"):
        reasons.append("latency measurement")
    return reasons


def loadgen_args(sc: dict) -> list[str]:
    """The full contract CLI for one run of `sc` (target and output added by the caller)."""
    lg = sc["loadgen"]
    return [
        "--transport", lg["transport"],
        "--mode", lg["mode"],
        "--type", lg["type"],
        "--connections", str(lg["connections"]),
        "--threads", str(lg["threads"]),
        "--rate", str(lg["rate"]),
        "--payload", lg["payload"],
        "--aux", str(lg["aux"]),
        "--batch", str(lg["batch"]),
        "--count", str(sc["count"]),
        "--duration", str(sc["duration_s"]),
        "--warmup", str(sc["warmup_s"]),
        "--scenario", sc["name"],
    ]
