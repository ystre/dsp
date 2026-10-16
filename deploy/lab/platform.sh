#!/usr/bin/env bash
#
# Installs the platform once per cluster, everything the lab and the CI deploy
# test share. Idempotent: running it again upgrades the release.
#   * the lab registry IMAGE_REGISTRY (registry.sh: TLS, authentication;
#     namespace registry);
#   * the dependencies chart (Prometheus Operator, Prometheus, Grafana,
#     node-exporter; Strimzi operator watching every namespace; the shared
#     Kafka cluster) as release "platform" in namespace "platform", every pod
#     on the control-plane node but node-exporter (platform-values.yaml; helm
#     args after `--` override it).
#
#   platform.sh [--chart <dir|.tgz>] [-- <helm args>...]   default: deploy/helm/deps
#
# CRDs are cluster-wide and owned by one release. A component whose CRDs the
# cluster already serves from elsewhere is left disabled (its existing
# installation is used); one that this release installed before stays enabled.
#
#   PLATFORM_NAMESPACE, PLATFORM_RELEASE   default: platform

source "$(dirname "$(realpath "$0")")/lib.sh"
need helm kubectl curl openssl

chart="${PROJECT_DIR}/deploy/helm/deps"
if [[ "${1:-}" == --chart ]]; then chart="${2:?--chart needs a path}"; shift 2; fi
[[ "${1:-}" == -- ]] && shift
ns="${PLATFORM_NAMESPACE:-platform}"
release="${PLATFORM_RELEASE:-platform}"

"${LAB_DIR}/registry.sh" up
log "This machine and the registry"

"${LAB_DIR}/registry.sh" check \
    || warn "this machine cannot push to or pull from ${IMAGE_REGISTRY} yet (the platform itself does not need it)"

# Values of the existing release (empty JSON if there is none).
current="$(helm get values "${release}" --namespace "${ns}" --all --output json 2>/dev/null || echo '{}')"

# enabled <subchart> <crd>: true if this release has it already, or nobody provides the CRD.
enabled() {
    local mine
    mine="$("${PY[@]}" -c 'import json, sys; print(str(json.loads(sys.argv[1]).get(sys.argv[2], {}).get("enabled", False)).lower())' \
        "${current}" "$1")"
    if [[ "${mine}" == true ]] || ! kubectl get crd "$2" >/dev/null 2>&1; then
        echo true
    else
        echo false
    fi
}
monitoring="$(enabled kube-prometheus-stack prometheuses.monitoring.coreos.com)"
kafka_operator="$(enabled strimzi-kafka-operator kafkas.kafka.strimzi.io)"

log "Platform release ${release} in ${ns}"
echo "kube-prometheus-stack:  ${monitoring}"
echo "strimzi-kafka-operator: ${kafka_operator}"

if [[ -d "${chart}" ]]; then
    package_own_subcharts "${chart}"
fi
helm upgrade --install "${release}" "${chart}" --namespace "${ns}" --create-namespace \
    --values "${LAB_DIR}/platform-values.yaml" \
    --set "kube-prometheus-stack.enabled=${monitoring}" \
    --set "strimzi-kafka-operator.enabled=${kafka_operator}" \
    "$@" --wait --timeout "${PLATFORM_TIMEOUT:-10m}"

# Captured first: `helm | grep -q` would fail under pipefail (SIGPIPE when grep exits early).
manifest="$(helm get manifest "${release}" --namespace "${ns}")"
if grep -q '^kind: Kafka$' <<< "${manifest}"; then
    log "Shared Kafka in ${ns}"
    kubectl wait kafka --all --namespace "${ns}" --for=condition=Ready --timeout "${PLATFORM_TIMEOUT:-10m}"
fi
