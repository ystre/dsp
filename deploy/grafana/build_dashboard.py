#!/usr/bin/env python3
"""Generate the server Grafana dashboard from the metrics contract.

Kept as code (o1 pattern) so panels stay consistent. Every server/loadgen
metric referenced by a query must exist in contract/metrics.toml; the
generator fails otherwise. The prefix comes from project.env.

    python3 -I deploy/grafana/build_dashboard.py           # (re)write the dashboard
    python3 -I deploy/grafana/build_dashboard.py --check   # fail if the committed file is stale

Output: deploy/helm/server/dashboards/server.json (shipped by the server chart
as a ConfigMap picked up by the Grafana sidecar).
"""

import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import contract  # noqa: E402
import project  # noqa: E402

OUT = ROOT / "deploy" / "helm" / "server" / "dashboards" / "server.json"

P = project.metric_prefix()
NAME = project.name()
DS = {"type": "prometheus", "uid": "${datasource}"}
R = "$__rate_interval"
RANGE = "$__range"
# cAdvisor samples carry their own (10-20 s apart) timestamps; use a fixed window (o1 finding).
CADVISOR_R = "1m"

# Selectors. Server metrics: the chosen namespace and server pods; reactor filter where labelled.
S = 'namespace=~"$namespace", pod=~"$pod"'
SR = S + ', reactor=~"$reactor"'
LG = 'namespace=~"$namespace"'
CONTAINER = 'namespace=~"$namespace", pod=~"$pod", container="server"'

_next_id = 0


def _id() -> int:
    global _next_id
    _next_id += 1
    return _next_id


def target(expr, legend="", ref="A", instant=False, fmt=None):
    t = {"datasource": DS, "expr": expr, "legendFormat": legend, "refId": ref, "range": not instant}
    if instant:
        t["instant"] = True
    if fmt:
        t["format"] = fmt
    return t


def timeseries(title, targets, unit="short", w=12, h=8, desc="", stack=False, min0=True):
    p = {
        "id": _id(), "type": "timeseries", "title": title, "description": desc, "datasource": DS,
        "gridPos": {"w": w, "h": h},
        "targets": [target(e, l, chr(65 + i)) for i, (e, l) in enumerate(targets)],
        "fieldConfig": {
            "defaults": {
                "unit": unit,
                "custom": {"lineWidth": 2, "fillOpacity": 8, "showPoints": "never",
                           "stacking": {"mode": "normal" if stack else "none"}},
            },
            "overrides": [],
        },
        "options": {"legend": {"displayMode": "table", "placement": "bottom", "calcs": ["mean", "max", "lastNotNull"]},
                    "tooltip": {"mode": "multi", "sort": "desc"}},
    }
    if min0:
        p["fieldConfig"]["defaults"]["min"] = 0
    return p


def stat(title, expr, unit="short", w=3, h=4, desc="", thresholds=None, decimals=None, color_mode="value"):
    p = {
        "id": _id(), "type": "stat", "title": title, "description": desc, "datasource": DS,
        "gridPos": {"w": w, "h": h},
        "targets": [target(expr, "", "A", instant=True)],
        "fieldConfig": {"defaults": {"unit": unit, "thresholds": {
            "mode": "absolute", "steps": thresholds or [{"color": "green", "value": None}]}}, "overrides": []},
        "options": {"reduceOptions": {"calcs": ["lastNotNull"], "fields": "", "values": False},
                    "colorMode": color_mode, "graphMode": "area", "textMode": "value", "justifyMode": "center"},
    }
    if decimals is not None:
        p["fieldConfig"]["defaults"]["decimals"] = decimals
    return p


def info(title, expr, legend, w=6, h=4, desc=""):
    """Stat panel showing a label value (e.g. version) instead of the sample value."""
    p = stat(title, expr, w=w, h=h, desc=desc)
    p["targets"][0]["legendFormat"] = legend
    p["options"]["textMode"] = "name"
    p["options"]["graphMode"] = "none"
    return p


def heatmap(title, expr, w=12, h=8, desc=""):
    return {
        "id": _id(), "type": "heatmap", "title": title, "description": desc, "datasource": DS,
        "gridPos": {"w": w, "h": h},
        "targets": [target(expr, "{{le}}", "A", fmt="heatmap")],
        "options": {"calculate": False, "yAxis": {"unit": "s"}, "cellGap": 1,
                    "color": {"mode": "scheme", "scheme": "Oranges", "steps": 64},
                    "tooltip": {"show": True, "yHistogram": True}},
    }


def row(title, collapsed=False):
    return {"id": _id(), "type": "row", "title": title, "collapsed": collapsed,
            "gridPos": {"w": 24, "h": 1}, "panels": []}


def layout(panels):
    """Assign x/y positions left to right, wrapping at 24 columns."""
    x, y, row_h = 0, 0, 0
    for p in panels:
        w, h = p["gridPos"]["w"], p["gridPos"]["h"]
        if p["type"] == "row":
            if x:
                y += row_h
            p["gridPos"].update(x=0, y=y)
            y += 1
            x, row_h = 0, 0
            continue
        if x + w > 24:
            y += row_h
            x, row_h = 0, 0
        p["gridPos"].update(x=x, y=y)
        x += w
        row_h = max(row_h, h)
    return panels


def q(qtl, metric, sel, by="le"):
    return f"histogram_quantile({qtl}, sum(rate({metric}_bucket{{{sel}}}[{R}])) by ({by}))"


RED_IF_ANY = [{"color": "green", "value": None}, {"color": "red", "value": 1}]

panels = [
    row("Overview"),
    stat("Messages/s in", f"sum(rate({P}_rx_messages_total{{{S}}}[{R}]))", desc="Frames received by the server, all types"),
    stat("Bytes/s in", f"sum(rate({P}_rx_bytes_total{{{S}}}[{R}]))", "Bps"),
    stat("Bytes/s out", f"sum(rate({P}_tx_bytes_total{{{S}}}[{R}]))", "Bps"),
    stat("Connections", f"sum({P}_connections{{{S}}})", desc="Open connections, all transports"),
    stat("Accepts/s", f"sum(rate({P}_connections_accepted_total{{{S}}}[{R}]))"),
    stat("RTT p99", q(0.99, "loadgen_rtt_seconds", LG), "s", desc="Load generator round trip (echo/req), from the intended send time",
         thresholds=[{"color": "green", "value": None}, {"color": "orange", "value": 0.001}, {"color": "red", "value": 0.01}]),
    stat("Server CPU (cores)", f"sum(rate(container_cpu_usage_seconds_total{{{CONTAINER}}}[{CADVISOR_R}]))", decimals=2),
    stat("CPU per message", f"sum(rate(container_cpu_usage_seconds_total{{{CONTAINER}}}[{CADVISOR_R}])) "
                            f"/ sum(rate({P}_rx_messages_total{{{S}}}[{CADVISOR_R}]))", "s",
         desc="Server CPU-seconds per received message (cAdvisor); the efficiency figure of the design"),

    info("Build", f"{P}_build_info{{{S}}}", "{{version}} ({{revision}}, {{backend}})", w=6),
    stat("Reactors", f"sum({P}_reactors{{{S}}})", w=3),
    stat("Integrity errors (range)", f"sum(increase(loadgen_integrity_errors_total{{{LG}}}[{RANGE}]))", w=3,
         thresholds=RED_IF_ANY, color_mode="background", decimals=0,
         desc="Missing / duplicate / out-of-order / corrupt replies seen by the load generator"),
    stat("Protocol errors (range)", f"sum(increase({P}_protocol_errors_total{{{S}}}[{RANGE}]))", w=3,
         thresholds=RED_IF_ANY, color_mode="background", decimals=0),
    stat("Rejected connections (range)", f"sum(increase({P}_connections_rejected_total{{{S}}}[{RANGE}]))", w=3,
         thresholds=RED_IF_ANY, color_mode="background", decimals=0),
    stat("UDP drops (range)", f"sum(increase({P}_udp_dropped_total{{{S}}}[{RANGE}]))", w=3,
         thresholds=RED_IF_ANY, color_mode="background", decimals=0),
    stat("Generator saturated", f"max(loadgen_thread_busy_ratio{{{LG}}})", "percentunit", w=3,
         thresholds=[{"color": "green", "value": None}, {"color": "red", "value": 0.9}], color_mode="background",
         desc="Busiest load generator thread; above 90 % the measurement is invalid (doc/loadgen.adoc, self-check)"),

    row("Pipeline"),
    timeseries("Messages/s end to end", [
        (f"sum(rate(loadgen_tx_messages_total{{{LG}}}[{R}]))", "1 generator sent"),
        (f"sum(rate({P}_rx_messages_total{{{S}}}[{R}]))", "2 server received"),
        (f"sum(rate({P}_tx_messages_total{{{S}}}[{R}]))", "3 server replied"),
        (f"sum(rate(loadgen_rx_messages_total{{{LG}}}[{R}]))", "4 generator received"),
    ], w=12, h=9, desc="In steady state 1 = 2 and 3 = 4; a gap shows where messages queue up"),
    timeseries("Bytes/s", [
        (f"sum(rate(loadgen_tx_bytes_total{{{LG}}}[{R}]))", "generator sent"),
        (f"sum(rate({P}_rx_bytes_total{{{S}}}[{R}]))", "server in"),
        (f"sum(rate({P}_tx_bytes_total{{{S}}}[{R}]))", "server out"),
        (f"sum(rate(loadgen_rx_bytes_total{{{LG}}}[{R}]))", "generator received"),
    ], "Bps", w=12, h=9),
    timeseries("Messages/s by type", [
        (f"sum(rate({P}_rx_messages_total{{{SR}}}[{R}])) by (type)", "rx {{type}}"),
        (f"sum(rate({P}_tx_messages_total{{{SR}}}[{R}])) by (type)", "tx {{type}}"),
    ], w=12, h=8, stack=True),
    timeseries("Messages/s by reactor", [
        (f"sum(rate({P}_rx_messages_total{{{SR}}}[{R}])) by (pod, reactor)", "{{pod}} r{{reactor}}"),
    ], w=12, h=8, desc="Imbalance means the kernel's SO_REUSEPORT / CBPF steering spreads connections unevenly"),

    row("Latency"),
    timeseries("Round trip (load generator)", [
        (q(0.5, "loadgen_rtt_seconds", LG), "p50"),
        (q(0.99, "loadgen_rtt_seconds", LG), "p99"),
        (q(0.999, "loadgen_rtt_seconds", LG), "p99.9"),
        (q(0.9999, "loadgen_rtt_seconds", LG), "p99.99"),
    ], "s", w=12, h=8, desc="Power-of-two buckets: quantiles have ±50 % resolution; use the bench JSON (HDR histogram) for exact values"),
    heatmap("Round trip distribution", f"sum(rate(loadgen_rtt_seconds_bucket{{{LG}}}[{R}])) by (le)", w=12, h=8),
    timeseries("Handler time per receive (server)", [
        (q(0.5, f"{P}_handler_duration_seconds", SR), "p50"),
        (q(0.99, f"{P}_handler_duration_seconds", SR), "p99"),
        (q(0.99, f"{P}_handler_duration_seconds", SR, "le, reactor"), "p99 r{{reactor}}"),
    ], "s", w=8, h=8),
    timeseries("Message age at receive", [
        (q(0.5, f"{P}_message_age_seconds", SR), "p50"),
        (q(0.99, f"{P}_message_age_seconds", SR), "p99"),
    ], "s", w=8, h=8, desc="Receive time minus the sender's timestamp; only meaningful when sender and server share a clock"),
    timeseries("Generator send lag", [
        (q(0.99, "loadgen_send_lag_seconds", LG), "p99"),
        (q(0.999, "loadgen_send_lag_seconds", LG), "p99.9"),
    ], "s", w=8, h=8, desc="Open loop: how late messages left versus their schedule. Growing lag = the generator or the network saturates"),

    row("Connections"),
    timeseries("Open connections", [
        (f"sum({P}_connections{{{SR}}}) by (transport)", "{{transport}}"),
        (f"sum(loadgen_connections{{{LG}}})", "generator"),
    ], w=8, h=8),
    timeseries("Accepted / closed per second", [
        (f"sum(rate({P}_connections_accepted_total{{{SR}}}[{R}])) by (transport)", "accepted {{transport}}"),
        (f"sum(rate({P}_connections_closed_total{{{SR}}}[{R}])) by (reason)", "closed {{reason}}"),
        (f"sum(rate({P}_connections_rejected_total{{{SR}}}[{R}])) by (reason)", "rejected {{reason}}"),
    ], w=8, h=8),
    timeseries("Errors per second", [
        (f"sum(rate({P}_protocol_errors_total{{{SR}}}[{R}])) by (reason)", "protocol {{reason}}"),
        (f"sum(rate(loadgen_connect_errors_total{{{LG}}}[{R}]))", "generator connect"),
        (f"sum(rate(loadgen_integrity_errors_total{{{LG}}}[{R}])) by (kind)", "integrity {{kind}}"),
    ], w=8, h=8),

    row("Reactors"),
    timeseries("Reactor utilisation", [
        (f"sum(rate({P}_loop_busy_seconds_total{{{SR}}}[{R}])) by (pod, reactor)", "{{pod}} r{{reactor}}"),
    ], "percentunit", w=12, h=8, desc="Share of time outside the kernel wait. 100 % = the reactor is the bottleneck"),
    timeseries("Loop iterations/s", [
        (f"sum(rate({P}_loop_iterations_total{{{SR}}}[{R}])) by (pod, reactor)", "{{pod}} r{{reactor}}"),
    ], w=12, h=8),
    timeseries("Messages per loop iteration", [
        (f"sum(rate({P}_rx_messages_total{{{SR}}}[{R}])) by (reactor) / sum(rate({P}_loop_iterations_total{{{SR}}}[{R}])) by (reactor)",
         "r{{reactor}}"),
    ], w=8, h=8, desc="Amortisation: how much work one wake-up does. Higher is better under load"),
    timeseries("Syscalls/s", [
        (f"sum(rate({P}_syscalls_total{{{SR}}}[{R}])) by (op)", "{{op}}"),
    ], w=8, h=8, stack=True),
    timeseries("io_uring batching", [
        (f'sum(rate({P}_io_uring_cqes_total{{{SR}}}[{R}])) / sum(rate({P}_syscalls_total{{{SR}, op="io_uring_enter"}}[{R}]))',
         "CQEs per io_uring_enter"),
        (f'sum(rate({P}_io_uring_sqes_total{{{SR}}}[{R}])) / sum(rate({P}_syscalls_total{{{SR}, op="io_uring_enter"}}[{R}]))',
         "SQEs per io_uring_enter"),
    ], w=8, h=8),
    timeseries("Buffer ring empty / s", [
        (f"sum(rate({P}_buffer_ring_empty_total{{{SR}}}[{R}])) by (reactor)", "r{{reactor}}"),
    ], w=8, h=7, desc="Receives that found no provided buffer (ENOBUFS): the ring is too small for the burst"),
    timeseries("Zero-copy sends", [
        (f"sum(rate({P}_zerocopy_sends_total{{{SR}}}[{R}])) by (result)", "{{result}}"),
    ], w=8, h=7, stack=True, desc="copied = the kernel fell back to copying (e.g. loopback)"),
    timeseries("CPU cores (container)", [
        (f"sum(rate(container_cpu_usage_seconds_total{{{CONTAINER}}}[{CADVISOR_R}])) by (pod)", "{{pod}}"),
        (f"sum(rate(container_cpu_cfs_throttled_seconds_total{{{CONTAINER}}}[{CADVISOR_R}])) by (pod)", "throttled {{pod}}"),
    ], w=8, h=7, desc="Any throttling distorts latency: run without a CPU limit or with Guaranteed QoS"),

    row("Backpressure and memory"),
    timeseries("Backpressure", [
        (f"sum(rate({P}_backpressure_events_total{{{SR}}}[{R}]))", "events/s"),
        (f"sum(rate({P}_backpressure_seconds_total{{{SR}}}[{R}]))", "paused connection-seconds/s"),
    ], w=8, h=8),
    timeseries("Send queue", [
        (f"sum({P}_send_queue_bytes{{{SR}}}) by (reactor)", "r{{reactor}}"),
    ], "bytes", w=8, h=8),
    timeseries("Memory", [
        (f"sum({P}_buffer_bytes{{{SR}}}) by (kind)", "buffers {{kind}}"),
        (f"sum(container_memory_working_set_bytes{{{CONTAINER}}})", "container working set"),
    ], "bytes", w=8, h=8),

    row("UDP"),
    timeseries("Datagrams/s", [
        (f"sum(rate({P}_udp_datagrams_total{{{SR}}}[{R}])) by (direction)", "{{direction}}"),
    ], w=8, h=7),
    timeseries("Server drops/s", [
        (f"sum(rate({P}_udp_dropped_total{{{SR}}}[{R}])) by (reason)", "{{reason}}"),
    ], w=8, h=7),
    timeseries("Kernel UDP errors/s (node)", [
        (f"sum(rate(node_netstat_Udp_RcvbufErrors[{R}]))", "receive buffer"),
        (f"sum(rate(node_netstat_Udp_InErrors[{R}]))", "in errors"),
        (f"sum(rate(node_netstat_Udp_SndbufErrors[{R}]))", "send buffer"),
    ], w=8, h=7, desc="Drops before the server sees the datagram: socket buffer too small or the reactor is behind"),

    row("Node"),
    timeseries("Softirq CPU by core", [
        (f'sum(rate(node_cpu_seconds_total{{mode="softirq"}}[{R}])) by (cpu)', "cpu{{cpu}}"),
    ], "percentunit", w=8, h=8, desc="Network receive processing; it should land on the cores of the reactors that own the flows"),
    timeseries("NIC packets/s", [
        (f'sum(rate(node_network_receive_packets_total{{device!~"lo|veth.*|cni.*|flannel.*"}}[{R}])) by (device)', "rx {{device}}"),
        (f'sum(rate(node_network_transmit_packets_total{{device!~"lo|veth.*|cni.*|flannel.*"}}[{R}])) by (device)', "tx {{device}}"),
    ], w=8, h=8),
    timeseries("TCP trouble/s", [
        (f"sum(rate(node_netstat_Tcp_RetransSegs[{R}]))", "retransmits"),
        (f"sum(rate(node_netstat_TcpExt_ListenOverflows[{R}]))", "listen overflows"),
        (f"sum(rate(node_netstat_TcpExt_ListenDrops[{R}]))", "listen drops"),
        (f'sum(rate(node_network_receive_drop_total{{device!~"lo"}}[{R}]))', "NIC rx drops"),
    ], w=8, h=8),
]

VARS = [
    {"name": "datasource", "type": "datasource", "query": "prometheus", "label": "Data source",
     "current": {"text": "Prometheus", "value": "prometheus"}},
    {"name": "namespace", "type": "query", "label": "Namespace", "datasource": DS,
     "query": {"query": f"label_values({P}_build_info, namespace)", "refId": "ns"},
     "definition": f"label_values({P}_build_info, namespace)", "refresh": 2, "includeAll": True, "multi": True,
     "current": {"text": "All", "value": "$__all"}},
    {"name": "pod", "type": "query", "label": "Server pod", "datasource": DS,
     "query": {"query": f'label_values({P}_build_info{{namespace=~"$namespace"}}, pod)', "refId": "pod"},
     "definition": f'label_values({P}_build_info{{namespace=~"$namespace"}}, pod)', "refresh": 2,
     "includeAll": True, "multi": True, "current": {"text": "All", "value": "$__all"}},
    {"name": "reactor", "type": "query", "label": "Reactor", "datasource": DS,
     "query": {"query": f'label_values({P}_loop_iterations_total{{namespace=~"$namespace", pod=~"$pod"}}, reactor)',
               "refId": "reactor"},
     "definition": f'label_values({P}_loop_iterations_total{{namespace=~"$namespace", pod=~"$pod"}}, reactor)',
     "refresh": 2, "includeAll": True, "multi": True, "current": {"text": "All", "value": "$__all"}},
]

dashboard = {
    "uid": f"{NAME}-server",
    "title": f"{NAME} server",
    "description": "Throughput, latency, connections, reactor efficiency and kernel-side networking of the server",
    "tags": [NAME, "networking", "benchmark"],
    "timezone": "browser",
    "schemaVersion": 39,
    "version": 1,
    "refresh": "5s",
    "time": {"from": "now-15m", "to": "now"},
    "graphTooltip": 1,
    "templating": {"list": VARS},
    "annotations": {"list": [{
        "name": "Load runs", "datasource": DS, "enable": True, "iconColor": "rgba(255, 152, 48, 0.6)",
        "expr": f"sum(loadgen_connections{{{LG}}}) > 0", "step": "5s", "titleFormat": "load generator active",
    }]},
    "panels": layout(panels),
}

# ------------------------------------------------------------------------------
# Contract check: every project/loadgen metric referenced must be in the contract.
# ------------------------------------------------------------------------------

METRIC = re.compile(rf"\b((?:{re.escape(P)}|loadgen)_[a-zA-Z0-9_]+)")


def referenced(obj) -> set[str]:
    found = set()
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k in ("expr", "definition") and isinstance(v, str):
                found |= set(METRIC.findall(v))
            else:
                found |= referenced(v)
    elif isinstance(obj, list):
        for v in obj:
            found |= referenced(v)
    return found


def check_contract() -> list[str]:
    known = {m["name"]: m for m in contract.load()}
    problems = []
    for name in sorted(referenced(dashboard)):
        base = name
        for suffix in ("_bucket", "_sum", "_count"):
            if name.endswith(suffix) and name.removesuffix(suffix) in known:
                base = name.removesuffix(suffix)
        if base not in known:
            problems.append(f"{name}: used by the dashboard but not in contract/metrics.toml")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="fail if the committed dashboard differs")
    args = ap.parse_args()

    problems = check_contract()
    for p in problems:
        print(f"dashboard: {p}", file=sys.stderr)
    if problems:
        return 1

    text = json.dumps(dashboard, indent=2) + "\n"
    if args.check:
        if not OUT.exists() or OUT.read_text() != text:
            print(f"dashboard: {OUT.relative_to(ROOT)} is stale; run {pathlib.Path(__file__).relative_to(ROOT)}",
                  file=sys.stderr)
            return 1
        print(f"dashboard: {OUT.relative_to(ROOT)} is up to date ({len(referenced(dashboard))} contract metrics)")
        return 0
    OUT.write_text(text)
    print(f"wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
