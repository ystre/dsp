#!/usr/bin/env python3

import json
import re
import os

report: dict = {}


def extract(key: str, input_file_path: str):
    with open(input_file_path) as inf:
        lines = inf.readlines()

    if len(lines) == 0:
        report[key] = "failed"
        return

    pattern = r"Summary: ([\d.]+ MBps) and ([\d.]+k?) MPS"
    # TODO: It assumes the first line is the summary.
    m = re.search(pattern, lines[0])

    if m:
        report[key] = {
            "dataRate": m.group(1),
            "messageRate": m.group(2)
        }
    else:
        report[key] = "failed"


def main():
    artifact_dir = os.sys.argv[1]
    tcp_perf_log = os.path.join(artifact_dir, "tcp-perf.txt")
    kafka_producer_perf_log = os.path.join(artifact_dir, "kafka-producer-perf.txt")
    kafka_consumer_perf_log = os.path.join(artifact_dir, "kafka-consumer-perf.txt")
    dsp_perf_log = os.path.join(artifact_dir, "dsp-perf.txt")

    extract("tcp", tcp_perf_log)
    extract("kafkaProducer", kafka_producer_perf_log)
    extract("kafkaConsumer", kafka_consumer_perf_log)
    extract("dsp", dsp_perf_log)

    print(json.dumps(report, indent=4))

if __name__ == "__main__":
    main()
