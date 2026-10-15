#!/usr/bin/env bash
#
# Helm charts: dependencies, strict lint, and rendering with the defaults and
# with every optional feature on (which also validates values.schema.json).

source "$(dirname "$(realpath "$0")")/lib.sh"
stage "Charts"

have helm || exit 0
cd "${WORKSPACE}/deploy/helm"

# The project's own subchart of the dependencies chart: packaged from the working tree.
mkdir -p deps/charts
helm package kafka --destination deps/charts >/dev/null

# Third-party dependencies: download only if not already present (offline runs).
if helm dependency list deps 2>/dev/null | awk 'NR > 1 && NF && $4 != "ok" { bad = 1 } END { exit !bad }'; then
    helm dependency build deps
fi

# The project's own subcharts: always repackaged from the working tree.
helm dependency update umbrella >/dev/null

for chart in "${CHARTS[@]}"; do
    helm lint --strict "${chart}" >/dev/null || helm lint --strict "${chart}"
    info "lint ${chart}"
done

render() {
    local name="$1"; shift
    helm template "${PROJECT_NAME}" "$@" > "${WORKSPACE}/build/chart-${name}.yaml"
    info "render ${name}: $(grep -c '^kind:' "${WORKSPACE}/build/chart-${name}.yaml") objects"
}

mkdir -p "${WORKSPACE}/build"

render server-defaults server

render server-all server \
    --set workload.kind=StatefulSet --set workload.replicas=2 \
    --set ports.udp.enabled=true --set ports.ws.enabled=true \
    --set service.type=LoadBalancer --set service.externalTrafficPolicy=Local \
    --set performance.hostNetwork=true --set performance.guaranteedQoS=true --set performance.hugepages2Mi=1Gi \
    --set config.enabled=true --set monitoring.prometheusRule.enabled=true \
    --set podDisruptionBudget.enabled=true --set autoscaling.enabled=true --set networkPolicy.enabled=true

render loadgen-defaults loadgen
render kafka-defaults kafka
render deps-defaults deps
render umbrella-defaults umbrella
render umbrella-dedicated-kafka umbrella --set kafka.enabled=true
render umbrella-ci umbrella --set topics.prefix=ci.run. --set topics.labels.ci-run=run \
    --set server.monitoring.dashboard.enabled=false --set loadgen.enabled=true

# Negative check: the schema must reject an invalid value.
if helm template x server --set workload.kind=Job >/dev/null 2>&1; then
    die "values.schema.json accepted an invalid workload.kind"
fi

info "schema rejects invalid values"
