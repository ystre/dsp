# shellcheck shell=bash
# shellcheck disable=SC2034  # variables are used by the scripts that source this file
#
# Shared by the deploy/lab scripts. Sourced, not executed.

set -euo pipefail

LAB_DIR="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"
PROJECT_DIR="$(realpath "${LAB_DIR}/../..")"
# shellcheck source=../../project.env
source "${PROJECT_DIR}/project.env"
VERSION="$(< "${PROJECT_DIR}/VERSION")"
# The lab registry (registry.sh): HOST:PORT, TLS and authentication required.
export IMAGE_REGISTRY="${IMAGE_REGISTRY:-oci-registry.internal.ystre.org:5000}"
REGISTRY_HOST="${IMAGE_REGISTRY%:*}"
REGISTRY_PORT="${IMAGE_REGISTRY##*:}"
IMAGES=(server loadgen)
PY=(python3 -I)

# Secrets of the lab that live outside the cluster (registry CA key, passwords).
# Owner-only; back it up, the cluster cannot recreate it.
LAB_STATE_DIR="${LAB_STATE_DIR:-${XDG_STATE_HOME:-${HOME}/.local/state}/${PROJECT_NAME}-lab}"

log() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
die() { printf '\033[31merror: %s\033[0m\n' "$*" >&2; exit 1; }

need() {
    local tool
    for tool in "$@"; do command -v "${tool}" >/dev/null || die "${tool} is required"; done
}

warn() { printf '\033[33mwarning: %s\033[0m\n' "$*" >&2; }

# control_plane <jsonpath>: a field of the (first) control-plane node.
control_plane() {
    kubectl get nodes --selector node-role.kubernetes.io/control-plane=true --output "jsonpath={.items[0].$1}"
}

# Node roles of the lab (labels set from here: a node may not give itself a
# node-role.kubernetes.io/ label). The platform node runs the platform (and,
# by default, the server and the registry); load generator nodes run the load
# generator when a run asks for them (nodes.sh).
PLATFORM_ROLE="node-role.kubernetes.io/platform"
LOADGEN_ROLE="node-role.kubernetes.io/loadgen"

# nodes_with_role <label>: the names of the nodes that carry <label>=true, one per line.
nodes_with_role() {
    kubectl get nodes --selector "$1=true" --output 'jsonpath={range .items[*]}{.metadata.name}{"\n"}{end}'
}

# platform_node: the node labelled for the platform; empty if none is (yet).
platform_node() {
    local nodes
    nodes="$(nodes_with_role "${PLATFORM_ROLE}")"
    (( $(grep -c . <<< "${nodes}") <= 1 )) \
        || die "several nodes are labelled ${PLATFORM_ROLE}: $(paste -sd ' ' <<< "${nodes}"); the platform keeps its data on one (nodes.sh platform <node>)"
    echo "${nodes}"
}

# warn_roleless_nodes: warns about nodes with neither lab role, e.g. a freshly joined
# agent whose `nodes.sh loadgen` was forgotten: nothing asks for them by name.
warn_roleless_nodes() {
    local node
    while read -r node; do
        [[ -n "${node}" ]] && warn "node ${node} has no lab role yet: deploy/lab/nodes.sh loadgen ${node} (or nodes.sh platform ${node})"
    done < <(kubectl get nodes --selector "!${PLATFORM_ROLE},!${LOADGEN_ROLE}" \
                 --output 'jsonpath={range .items[*]}{.metadata.name}{"\n"}{end}')
}

# node_ipv4 <node>: the node's internal IPv4 address (a dual-stack node lists an IPv6 one too).
node_ipv4() {
    kubectl get node "$1" --output "jsonpath={.status.addresses[?(@.type=='InternalIP')].address}" \
        | tr ' ' '\n' | grep -m 1 -E '^[0-9]+(\.[0-9]+){3}$' || true
}

# chart_field <chart dir|.tgz> <field>: a top-level Chart.yaml field (version, appVersion, name).
chart_field() {
    helm show chart "$1" | awk -v k="$2:" '/^[^ -]/ && $1 == k { gsub(/"/, "", $2); print $2; exit }'
}

# kafka_topics: the KafkaTopic documents of a rendered manifest (stdin).
kafka_topics() {
    awk 'BEGIN { RS = "\n---\n" } /(^|\n)kind: KafkaTopic\n/ { printf "---\n%s\n", $0 }'
}

# package_own_subcharts <chart dir>: the project's own (file://) subcharts,
# packaged from the working tree into <chart dir>/charts without touching the
# third-party ones; then the third-party ones are downloaded only if missing
# (offline runs).
package_own_subcharts() {
    local chart="$1" sub
    mkdir -p "${chart}/charts"
    while IFS= read -r sub; do
        helm package "${chart}/${sub}" --destination "${chart}/charts" >/dev/null
    done < <(awk '/repository: file:\/\// { sub(/.*file:\/\//, ""); print }' "${chart}/Chart.yaml")
    if helm dependency list "${chart}" 2>/dev/null | awk 'NR > 1 && NF && $4 != "ok" { bad = 1 } END { exit !bad }'; then
        helm dependency build "${chart}"
    fi
}
