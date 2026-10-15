# dsp

A network server built for the highest throughput per core and the lowest
tail latency the Linux kernel allows. TCP comes first; UDP and WebSocket are
part of the design. It uses thread-per-core reactors over io_uring (with epoll
as a fallback), and the application logic is a pluggable handler.

**Status: Phase 1 (framework).** This phase delivers:
- the design and its contracts;
- a CI pipeline that runs anywhere;
- the benchmarking framework;
- the Helm charts and an ETSI SOL004 package;
- a Grafana dashboard.

The server and the load generator are minimal "hello world" programs that
exercise all of it. Phase 2 implements the design.

Documentation is an AsciiDoc book: [`doc/book.adoc`](doc/book.adoc). It renders
to `dist/doc/book.html` (`ci/run.sh docs`).

## Quick start

```sh
tools/configure.sh build/release Release && cmake --build build/release
BIN_DIR=build/release/bin tests/run.sh                                # functional tests
python3 -I tools/bench/run.py --scenario smoke --bin-dir build/release/bin
ci/e2e.sh                                                             # whole pipeline, clean copy
deploy/lab/up.sh --source                                             # k3s lab from the working tree + helm test
```

## Layout

```
bench/              scenarios, CI baselines
ci/                 CI stages
contract/           metrics contract, load generator summary schema
csar/               ETSI SOL004 package sources
deploy/             Helm charts (server, loadgen, kafka, umbrella; deps = platform with shared Kafka), Grafana generator, k3s lab
doc/                the book
env/                images
libadmin/           metrics exposition, admin HTTP
libproto/           benchmark wire protocol
loadgen/            the load generator
server/             the server
tests/              functional tests
tools/              bench runner/compare/report, CSAR build/validate, versioning, rename
project.env         project identity (name, metric prefix, vendor): tools/rename.sh
```
