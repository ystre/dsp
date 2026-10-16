#!/usr/bin/env python3
"""Run one benchmark scenario and write results/<run-id>/.

    run.py --scenario smoke [--target local|k8s] [--bin-dir build/release/bin] [--out results]
    run.py --list

Targets:
  local   Starts the server and the load generator as host processes, pinned with
          taskset to the scenario's CPU sets ([scenario.local]); the server is
          restarted for every repeat. With --server-addr/--server-admin, an
          already running server is used instead (no CPU/RSS figures then).
  k8s     Installs the load generator chart (deploy/helm/loadgen) as a Job
          against a deployed server release, collects the generator's summary
          from the pod logs and the server metrics through the API server's pod
          proxy. Needs kubectl and helm pointing at the cluster. --loadgen-nodes
          confines the generator to the load generator nodes (deploy/lab/nodes.sh).

Output (doc/benchmarking.adoc, "Results"):
  env.json                 host snapshot (tools/bench/sysinfo.py)
  loadgen-<n>.json         load generator summary of repeat n (contract schema)
  server-<n>-{before,after}.prom
  summary.json             per-repeat figures, medians, spread, validity

Exit code: 0 ok, 1 error, 2 the run completed but is not clean or not valid,
3 skipped (the load generator does not support the scenario).
"""

import argparse
import json
import os
import pathlib
import shutil
import signal
import socket
import statistics
import subprocess
import sys
import time
import urllib.request

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import project  # noqa: E402
import scenarios  # noqa: E402
import sysinfo  # noqa: E402

EXIT_NOT_CLEAN, EXIT_SKIPPED = 2, 3
# The lab's load generator node role (deploy/lab/nodes.sh; the loadgen chart's placement.dedicatedNodes).
LOADGEN_ROLE = "node-role.kubernetes.io/loadgen"

# Figures compared across runs (tools/bench/compare.py): name -> (direction, aggregate).
# direction: +1 higher is better, -1 lower is better.
FIGURES = {
    "throughput.messages_per_s": (+1, "median"),
    "throughput.bytes_per_s": (+1, "median"),
    "throughput.connections_per_s": (+1, "median"),
    "latency_ns.p50": (-1, "median"),
    "latency_ns.p99": (-1, "median"),
    "latency_ns.p999": (-1, "median"),
    "latency_ns.p9999": (-1, "median"),
    "latency_ns.max": (-1, "max"),
    "server.ns_per_message": (-1, "median"),
    "server.cpu_cores": (-1, "median"),
    "server.max_rss_kib": (-1, "max"),
}


def log(text: str) -> None:
    print(f"[bench] {text}", file=sys.stderr, flush=True)


# ------------------------------------------------------------------------------
# Prometheus exposition
# ------------------------------------------------------------------------------

def parse_prom(text: str) -> dict[str, float]:
    series = {}
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        key, _, value = line.rpartition(" ")
        try:
            series[key] = float(value)
        except ValueError:
            continue
    return series


def family_sum(series: dict[str, float], family: str) -> float:
    return sum(v for k, v in series.items() if k == family or k.startswith(family + "{"))


def delta(before: dict[str, float], after: dict[str, float]) -> dict[str, float]:
    return {k: after[k] - before.get(k, 0.0) for k in after}


# ------------------------------------------------------------------------------
# Local target
# ------------------------------------------------------------------------------

def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def http_get(url: str, timeout: float = 2.0) -> str:
    with urllib.request.urlopen(url, timeout=timeout) as rsp:
        return rsp.read().decode()


def pinned(cmd: list[str], cpus: str) -> list[str]:
    if cpus and shutil.which("taskset"):
        return ["taskset", "-c", cpus, *cmd]
    return cmd


def proc_cpu_s(pid: int) -> float:
    """CPU time of all live threads of `pid`, in nanosecond resolution (schedstat).

    /proc/<pid>/stat counts in clock ticks (10 ms), too coarse for short runs. Threads
    that already exited are not counted; the server's threads live as long as it does.
    """
    try:
        return sum(int(t.joinpath("schedstat").read_text().split()[0])
                   for t in pathlib.Path(f"/proc/{pid}/task").iterdir()) / 1e9
    except (OSError, ValueError, IndexError):
        fields = pathlib.Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


def proc_hwm_kib(pid: int) -> int | None:
    for line in pathlib.Path(f"/proc/{pid}/status").read_text().splitlines():
        if line.startswith("VmHWM:"):
            return int(line.split()[1])
    return None


class LocalServer:
    def __init__(self, binary: pathlib.Path, cpus: str, extra: list[str], logfile: pathlib.Path):
        self.port, self.admin = free_port(), free_port()
        self.log = logfile.open("w")
        cmd = pinned([str(binary), "--port", str(self.port), "--admin-port", str(self.admin), *extra], cpus)
        self.proc = subprocess.Popen(cmd, stdout=self.log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                http_get(f"http://127.0.0.1:{self.admin}/readyz")
                return
            except OSError:
                if self.proc.poll() is not None:
                    raise RuntimeError(f"server exited with {self.proc.returncode}, see {logfile}")
                time.sleep(0.05)
        raise RuntimeError("server did not become ready")

    @property
    def addr(self) -> str:
        return f"127.0.0.1:{self.port}"

    def metrics(self) -> str:
        return http_get(f"http://127.0.0.1:{self.admin}/metrics")

    def stop(self) -> None:
        self.proc.send_signal(signal.SIGTERM)
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        self.log.close()


def run_local(args, sc: dict, out: pathlib.Path) -> list[dict]:
    bin_dir = pathlib.Path(args.bin_dir)
    loadgen = bin_dir / f"{project.name()}-loadgen"
    server_bin = bin_dir / f"{project.name()}-{args.server}"
    repeats = []
    for n in range(sc["repeat"]):
        server = None
        if args.server_addr:
            addr, admin_url = args.server_addr, f"http://{args.server_admin}/metrics"
            scrape = (lambda url=admin_url: http_get(url)) if args.server_admin else (lambda: "")
        else:
            server = LocalServer(server_bin, sc["local"]["server_cpus"], sc["server_args"], out / f"server-{n}.log")
            addr, scrape = server.addr, server.metrics
        try:
            before = scrape()
            cpu0 = proc_cpu_s(server.proc.pid) if server else None
            summary_file = out / f"loadgen-{n}.json"
            cmd = pinned([str(loadgen), "--target", addr, *scenarios.loadgen_args(sc), "--output", str(summary_file)],
                         sc["local"]["loadgen_cpus"])
            log(f"repeat {n + 1}/{sc['repeat']}: {' '.join(cmd)}")
            rc = subprocess.run(cmd, check=False).returncode
            after = scrape()
            cpu1 = proc_cpu_s(server.proc.pid) if server else None
            hwm = proc_hwm_kib(server.proc.pid) if server else None
        finally:
            if server:
                server.stop()
        (out / f"server-{n}-before.prom").write_text(before)
        (out / f"server-{n}-after.prom").write_text(after)
        if not summary_file.exists():
            raise RuntimeError(f"load generator exited with {rc} without a summary")
        repeats.append(repeat_figures(json.loads(summary_file.read_text()), rc, before, after,
                                      None if cpu0 is None else cpu1 - cpu0, hwm))
    return repeats


# ------------------------------------------------------------------------------
# Kubernetes target
# ------------------------------------------------------------------------------

def kubectl(*argv: str, check: bool = True) -> str:
    return subprocess.run(["kubectl", *argv], capture_output=True, text=True, check=check).stdout


def k8s_server_metrics(ns: str, release: str) -> str:
    pods = kubectl("-n", ns, "get", "pods", "-o", "name",
                   "-l", f"app.kubernetes.io/instance={release},app.kubernetes.io/component=server").split()
    if not pods:
        raise RuntimeError(f"no server pods for release {release!r} in namespace {ns!r}")
    # One exposition per pod; series of different pods are summed by family_sum() through
    # distinct keys (the pod name is appended as a pseudo label).
    out = []
    for pod in pods:
        name = pod.split("/", 1)[1]
        # The pod proxy needs the port number (named ports are resolved for Services only).
        port = kubectl("-n", ns, "get", pod, "-o",
                       "jsonpath={.spec.containers[*].ports[?(@.name=='admin')].containerPort}").split()[0]
        text = kubectl("get", "--raw", f"/api/v1/namespaces/{ns}/pods/{name}:{port}/proxy/metrics")
        out += [line if line.startswith("#") else f"{line.split(' ')[0]}#pod={name} {line.rsplit(' ', 1)[1]}"
                for line in text.splitlines() if line]
    return "\n".join(out) + "\n"


def run_k8s(args, sc: dict, out: pathlib.Path) -> list[dict]:
    ns, release = args.namespace, args.release
    target = args.server_addr or f"{release}-server.{ns}.svc:7200"
    repeats = []
    for n in range(sc["repeat"]):
        job_release = f"{release}-bench"
        lg = sc["loadgen"]
        run_values = {**{k: lg[k] for k in ("transport", "mode", "type", "connections", "threads",
                                              "rate", "payload", "aux", "batch")},
                      "count": sc["count"], "duration": sc["duration_s"], "warmup": sc["warmup_s"],
                      "scenario": sc["name"]}
        subprocess.run(["helm", "uninstall", job_release, "-n", ns, "--wait"], capture_output=True, check=False)
        before = k8s_server_metrics(ns, release)
        cmd = ["helm", "install", job_release, str(project.ROOT / "deploy" / "helm" / "loadgen"), "-n", ns,
               "--set", f"target={target}", "--set-json", f"run={json.dumps(run_values)}",
               "--set", f"image.registry={args.image_registry}", "--set", f"image.tag={args.image_tag}",
               "--set", f"image.pullPolicy={args.image_pull_policy}",
               "--set", f"placement.dedicatedNodes={str(args.loadgen_nodes).lower()}"]
        log(f"repeat {n + 1}/{sc['repeat']}: {' '.join(cmd)}")
        subprocess.run(cmd, check=True, capture_output=True)
        job = f"job/{job_release}-loadgen"
        timeout = int(sc["duration_s"] or 600) + 300
        rc = subprocess.run(["kubectl", "-n", ns, "wait", "--for=condition=complete", job, f"--timeout={timeout}s"],
                            capture_output=True, check=False).returncode
        logs = kubectl("-n", ns, "logs", job, check=False)
        after = k8s_server_metrics(ns, release)
        subprocess.run(["helm", "uninstall", job_release, "-n", ns], capture_output=True, check=False)
        (out / f"server-{n}-before.prom").write_text(before)
        (out / f"server-{n}-after.prom").write_text(after)
        summary = next((json.loads(line) for line in logs.splitlines() if line.startswith("{")), None)
        if summary is None:
            raise RuntimeError(f"no summary in the load generator logs:\n{logs}")
        (out / f"loadgen-{n}.json").write_text(json.dumps(summary, indent=2) + "\n")
        repeats.append(repeat_figures(summary, 0 if rc == 0 else 2, before, after, None, None))
    return repeats


# ------------------------------------------------------------------------------
# Figures
# ------------------------------------------------------------------------------

def repeat_figures(lg: dict, rc: int, before: str, after: str, cpu_s: float | None, hwm: int | None) -> dict:
    prefix = project.metric_prefix()
    d = delta(parse_prom(before), parse_prom(after))
    messages = family_sum(d, f"{prefix}_rx_messages_total")
    elapsed = lg["elapsed_s"] or None
    clean = (rc == 0 and not any(lg["integrity"].values()) and not any(lg["errors"].values()))
    figures = {
        "throughput.messages_per_s": lg["throughput"]["messages_per_s"],
        "throughput.bytes_per_s": lg["throughput"]["bytes_per_s"],
        "throughput.connections_per_s": lg["throughput"].get("connections_per_s"),
        "server.messages": messages,
        "server.cpu_s": cpu_s,
        "server.cpu_cores": cpu_s / elapsed if cpu_s is not None and elapsed else None,
        "server.ns_per_message": cpu_s * 1e9 / messages if cpu_s is not None and messages else None,
        "server.max_rss_kib": hwm,
    }
    for q, v in (lg.get("latency_ns") or {}).items():
        figures[f"latency_ns.{q}"] = v
    return {"figures": figures, "valid": lg["valid"], "invalid_reasons": lg["invalid_reasons"],
            "clean": clean, "loadgen_exit": rc, "elapsed_s": lg["elapsed_s"]}


def aggregate(repeats: list[dict]) -> tuple[dict, dict]:
    figures, spread = {}, {}
    for name, (_, how) in FIGURES.items():
        values = [r["figures"].get(name) for r in repeats]
        values = [v for v in values if v is not None]
        if not values:
            figures[name] = None
            continue
        figures[name] = max(values) if how == "max" else statistics.median(values)
        mid = statistics.median(values)
        spread[name] = (max(values) - min(values)) / mid if mid else 0.0
    return figures, spread


# ------------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scenario")
    ap.add_argument("--list", action="store_true", help="list scenarios and whether the load generator supports them")
    ap.add_argument("--target", choices=("local", "k8s"), default="local")
    ap.add_argument("--bin-dir", default=str(project.ROOT / "build" / "release" / "bin"))
    ap.add_argument("--server", default="server", help="server binary suffix: <project>-<server> (local target)")
    ap.add_argument("--server-addr", help="use a running server (local) / override the service address (k8s)")
    ap.add_argument("--server-admin", help="admin HOST:PORT of a running server (local target)")
    ap.add_argument("--namespace", default=project.name())
    ap.add_argument("--release", default=project.name(), help="Helm release of the deployed server (k8s)")
    ap.add_argument("--image-registry", default="oci-registry.internal.ystre.org:5000",
                    help="registry of the load generator image <project>-loadgen (k8s)")
    ap.add_argument("--image-tag", default=project.version())
    ap.add_argument("--image-pull-policy", default="IfNotPresent", choices=("Always", "IfNotPresent", "Never"))
    ap.add_argument("--loadgen-nodes", action="store_true",
                    help="run the load generator on the load generator nodes only (k8s)")
    ap.add_argument("--capabilities", help="load generator capabilities JSON file (k8s; default: ask the local binary)")
    ap.add_argument("--repeat", type=int, help="override the scenario's repeat count")
    ap.add_argument("--out", default=str(project.ROOT / "results"))
    ap.add_argument("--run-id")
    args = ap.parse_args()

    loadgen = pathlib.Path(args.bin_dir) / f"{project.name()}-loadgen"
    if args.capabilities:
        caps = json.loads(pathlib.Path(args.capabilities).read_text())
    else:
        caps = json.loads(subprocess.run([str(loadgen), "--capabilities"], capture_output=True,
                                         text=True, check=True).stdout)

    if args.list:
        for name, sc in scenarios.load_all().items():
            missing = scenarios.unsupported(sc, caps)
            status = "runnable" if not missing else "needs " + ", ".join(missing)
            print(f"{name:24} {status}")
        return 0
    if not args.scenario:
        ap.error("--scenario or --list is required")

    sc = scenarios.get(args.scenario)
    if args.repeat:
        sc["repeat"] = args.repeat
    missing = scenarios.unsupported(sc, caps)
    if missing:
        log(f"SKIP {sc['name']}: the load generator lacks {', '.join(missing)}")
        return EXIT_SKIPPED

    if args.target == "k8s" and args.loadgen_nodes \
            and not kubectl("get", "nodes", "-l", f"{LOADGEN_ROLE}=true", "-o", "name").split():
        # The job would stay Pending until its timeout.
        raise RuntimeError(f"--loadgen-nodes: no node is labelled {LOADGEN_ROLE}=true "
                           "(deploy/lab/nodes.sh loadgen <node>)")

    run_id = args.run_id or f"{sc['name']}-{args.target}-{time.strftime('%Y%m%d-%H%M%S')}"
    out = pathlib.Path(args.out) / run_id
    out.mkdir(parents=True, exist_ok=False)
    env = sysinfo.snapshot()
    (out / "env.json").write_text(json.dumps(env, indent=2) + "\n")

    repeats = (run_local if args.target == "local" else run_k8s)(args, sc, out)
    figures, spread = aggregate(repeats)
    result = {
        "schema": "bench-result/1",
        "run_id": run_id,
        "target": args.target,
        "scenario": {k: v for k, v in sc.items() if k != "file"} | {"file": sc["file"]},
        "loadgen_capabilities": caps,
        "env": {"hostname": env["hostname"], "kernel": env["kernel"], "cpu": env["cpu"]["model"],
                "revision": env["project"]["revision"], "dirty": env["project"]["dirty"]},
        "valid": all(r["valid"] for r in repeats),
        "clean": all(r["clean"] for r in repeats),
        "figures": figures,
        "spread": spread,
        "repeats": repeats,
    }
    (out / "summary.json").write_text(json.dumps(result, indent=2) + "\n")

    log(f"results: {out}")
    for name, value in figures.items():
        if value is not None:
            log(f"  {name:30} {value:>16,.1f}   spread {spread[name]:.1%}")
    if not result["clean"] or not result["valid"]:
        log("run is NOT clean/valid, see summary.json")
        return EXIT_NOT_CLEAN
    print(out)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, subprocess.CalledProcessError) as ex:
        log(f"error: {ex}")
        sys.exit(1)
