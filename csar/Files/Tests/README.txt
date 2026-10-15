Tests shipped with the package.

  * helm test <release>
      The server chart's test hook runs the load generator against the
      server's Service (echo, 10 000 messages) and fails on any lost, duplicate
      or reordered reply. Defined in the chart: templates/tests/loadgen-smoke.yaml.

  * Benchmark scenarios
      tools/bench/run.py --target k8s --release <release> --scenario smoke
      (from the source repository; doc/benchmarking.adoc).
