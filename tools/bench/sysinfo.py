#!/usr/bin/env python3
"""Environment snapshot of the benchmark host, as JSON (results/<run>/env.json).

Everything that is known to move networking benchmark numbers: kernel, CPU
model and frequency governor, SMT, THP, isolated CPUs, the relevant sysctls,
and the NIC offload/queue configuration (if ethtool is available). Missing
sources are reported as null, never fatal.
"""

import json
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import project  # noqa: E402

SYSCTLS = [
    "net/core/somaxconn",
    "net/core/rmem_max",
    "net/core/wmem_max",
    "net/core/netdev_max_backlog",
    "net/core/busy_poll",
    "net/core/busy_read",
    "net/ipv4/tcp_rmem",
    "net/ipv4/tcp_wmem",
    "net/ipv4/tcp_congestion_control",
    "net/ipv4/tcp_max_syn_backlog",
    "net/ipv4/ip_local_port_range",
    "net/ipv4/tcp_tw_reuse",
    "net/ipv4/tcp_fastopen",
    "fs/file-max",
    "fs/nr_open",
    "kernel/io_uring_disabled",
    "kernel/numa_balancing",
    "vm/nr_hugepages",
]


def read(path: str) -> str | None:
    try:
        return pathlib.Path(path).read_text().strip()
    except OSError:
        return None


def run(*cmd: str) -> str | None:
    if not shutil.which(cmd[0]):
        return None
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=10, check=False).stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        return None


def cpu_model() -> str | None:
    for line in (read("/proc/cpuinfo") or "").splitlines():
        if line.startswith("model name"):
            return line.split(":", 1)[1].strip()
    return None


def governors() -> dict[str, int]:
    counts: dict[str, int] = {}
    for gov in pathlib.Path("/sys/devices/system/cpu").glob("cpu[0-9]*/cpufreq/scaling_governor"):
        g = read(str(gov)) or "unknown"
        counts[g] = counts.get(g, 0) + 1
    return counts


def nics() -> list[dict]:
    out = []
    for dev in sorted(pathlib.Path("/sys/class/net").glob("*")):
        if dev.name == "lo" or not (dev / "device").exists():
            continue    # Physical NICs only.
        info = {
            "name": dev.name,
            "speed_mbps": read(str(dev / "speed")),
            "mtu": read(str(dev / "mtu")),
            "rx_queues": len(list((dev / "queues").glob("rx-*"))),
            "tx_queues": len(list((dev / "queues").glob("tx-*"))),
            "driver": os.path.basename(os.path.realpath(dev / "device" / "driver")),
        }
        features = run("ethtool", "-k", dev.name)
        if features:
            wanted = ("generic-receive-offload", "large-receive-offload", "tcp-segmentation-offload",
                      "generic-segmentation-offload", "rx-gro-hw", "rx-udp-gro-forwarding")
            info["offloads"] = {
                k.strip(): v.split()[0]
                for k, v in (line.split(":", 1) for line in features.splitlines()[1:] if ":" in line)
                if k.strip() in wanted
            }
        out.append(info)
    return out


def snapshot() -> dict:
    uname = platform.uname()
    git = run("git", "-C", str(project.ROOT), "rev-parse", "--short", "HEAD")
    dirty = run("git", "-C", str(project.ROOT), "status", "--porcelain")
    return {
        "schema": "bench-env/1",
        "time_unix_s": round(time.time(), 3),
        "hostname": uname.node,
        "project": {"name": project.name(), "version": project.version(),
                    "revision": git or None, "dirty": bool(dirty) if dirty is not None else None},
        "kernel": uname.release,
        "cpu": {
            "model": cpu_model(),
            "online": read("/sys/devices/system/cpu/online"),
            "isolated": read("/sys/devices/system/cpu/isolated"),
            "smt_active": read("/sys/devices/system/cpu/smt/active"),
            "governors": governors(),
            "boost": read("/sys/devices/system/cpu/cpufreq/boost"),
        },
        "memory_kib": next((int(line.split()[1]) for line in (read("/proc/meminfo") or "").splitlines()
                            if line.startswith("MemTotal:")), None),
        "thp": read("/sys/kernel/mm/transparent_hugepage/enabled"),
        "sysctl": {s.replace("/", "."): read(f"/proc/sys/{s}") for s in SYSCTLS},
        "nics": nics(),
        "virtualization": run("systemd-detect-virt") or None,
        "kubernetes_node": os.environ.get("NODE_NAME"),
    }


if __name__ == "__main__":
    json.dump(snapshot(), sys.stdout, indent=2)
    print()
