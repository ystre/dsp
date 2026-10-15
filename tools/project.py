"""Project identity (project.env) and repository paths, for the Python tools.

Scripts are run with `python3 -I`, so they import this module after adding the
tools directory to sys.path explicitly:

    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[N]))
    import project
"""

import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent


def env() -> dict[str, str]:
    values = {}
    for line in (ROOT / "project.env").read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key] = value
    return values


def name() -> str:
    return env()["PROJECT_NAME"]


def metric_prefix() -> str:
    return env()["METRIC_PREFIX"]


def version() -> str:
    return (ROOT / "VERSION").read_text().strip()
