#!/usr/bin/env bash
#
# Installs the platform once per cluster, everything the lab and the CI deploy
# test share. Idempotent: running it again upgrades the release.
#   * the platform node: the node labelled node-role.kubernetes.io/platform
#     (nodes.sh platform <node>); on the first run, the control-plane node gets
#     the label. The platform's volumes stay on the node they were created on:
#     it stops if one is elsewhere (after a move of the label);
#   * the lab registry IMAGE_REGISTRY (registry.sh: TLS, authentication;
#     namespace registry);
#   * the dependencies chart (Prometheus Operator, Prometheus, Grafana,
#     node-exporter; Strimzi operator watching every namespace; the shared
#     Kafka cluster) as release "platform" in namespace "platform", every pod
#     on the platform node but node-exporter (platform-values.yaml; helm args
#     after `--` override it).
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

log "Platform node (${PLATFORM_ROLE})"
node="$(platform_node)"
if [[ -z "${node}" ]]; then
    node="$(control_plane metadata.name)"
    [[ -n "${node}" ]] || die "no control-plane node to label; label one: deploy/lab/nodes.sh platform <node>"
    kubectl label node "${node}" "${PLATFORM_ROLE}=true"
    echo "no node had the label: ${node} (the control-plane node) is the platform node from now on"
else
    echo "${node}"
fi
warn_roleless_nodes

# Volumes of the platform elsewhere: their pods would wait for that node forever.
hostname="$(kubectl get node "${node}" --output 'jsonpath={.metadata.labels.kubernetes\.io/hostname}')"
stranded="$(kubectl get pvc --namespace "${ns}" --ignore-not-found \
        --output 'jsonpath={range .items[*]}{.metadata.name}{" "}{.spec.volumeName}{"\n"}{end}' \
    | while read -r claim volume; do
        [[ -n "${volume}" ]] || continue
        at="$(kubectl get pv "${volume}" --ignore-not-found \
            --output 'jsonpath={.spec.nodeAffinity.required.nodeSelectorTerms[0].matchExpressions[0].values[0]}')"
        [[ -z "${at}" || "${at}" == "${hostname}" ]] || echo "${claim} (on ${at})"
    done)"
if [[ -n "${stranded}" ]]; then
    printf '  %s\n' "${stranded}" >&2
    die "these volumes of ${ns} are not on the platform node ${node}; move the label back (nodes.sh platform <node>), or give up their data: kubectl delete pvc --namespace ${ns} <claim>... (with the pods using them), then run this again"
fi

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
