#!/usr/bin/env bash
#
# Nodes of the lab cluster and their roles (doc/deployment.adoc, "Multi-node
# lab"): the platform node, which runs the platform (and, by default, the
# server and the registry), and load generator nodes, extra k3s agents (e.g. a
# laptop) where the load generator runs when asked to (placement.dedicatedNodes,
# run.py --loadgen-nodes), so that it does not share CPUs with the server.
#
#   nodes.sh join <dir>      writes the setup bundle of a new node into the new
#                            directory <dir>: registry trust and credentials,
#                            k3s agent join (reads the cluster token with sudo);
#                            run its setup.sh on the node
#   nodes.sh loadgen <node>  makes a joined node a load generator node: label
#                            node-role.kubernetes.io/loadgen
#   nodes.sh platform <node> makes <node> the platform node: label
#                            node-role.kubernetes.io/platform, taken from the
#                            previous one (platform.sh gives it to the
#                            control-plane node on its first run)
#   nodes.sh status          the nodes, their roles and taints
#   nodes.sh remove <node>   drains and deletes a node from the cluster
#
# Run on the control-plane machine (or anywhere kubectl points at the cluster;
# `join` needs the token file of the k3s server, K3S_TOKEN_FILE).
#
# The labels only attract what asks for them; they keep nothing away: every
# node takes any other workload. They are set here, not by the agents: the
# NodeRestriction admission plugin does not let a node give itself
# node-role.kubernetes.io/ labels.

source "$(dirname "$(realpath "$0")")/lib.sh"
need kubectl

K3S_TOKEN_FILE="${K3S_TOKEN_FILE:-/var/lib/rancher/k3s/server/node-token}"

usage() { sed -n '/^#   nodes.sh/p' "$0" >&2; exit 1; }

join() {
    local dir="${1:?usage: nodes.sh join <dir>}" url version token
    url="https://$(node_ipv4 "$(control_plane metadata.name)"):6443"
    version="$(control_plane status.nodeInfo.kubeletVersion)"
    [[ "${url}" != "https://:6443" && -n "${version}" ]] || die "no control-plane node found"
    if [[ -r "${K3S_TOKEN_FILE}" ]]; then
        token="$(< "${K3S_TOKEN_FILE}")"
    else
        echo "Reading the cluster join token (${K3S_TOKEN_FILE}) with sudo" >&2
        token="$(sudo cat "${K3S_TOKEN_FILE}")" || die "cannot read ${K3S_TOKEN_FILE}"
    fi
    [[ -n "${token}" ]] || die "empty join token in ${K3S_TOKEN_FILE}"

    "${LAB_DIR}/registry.sh" node-config "${dir}" >/dev/null
    (umask 077 && cat > "${dir}/join.env" <<EOT
# Read by setup.sh. Holds the cluster join token: whoever has it can add nodes.
K3S_URL=${url}
K3S_VERSION=${version}
K3S_TOKEN=${token}
EOT
    )
    cat <<EOT
Wrote ${dir}: the registry setup and the k3s join of a new node. It holds the
registry pull password and the cluster join token: keep it private, copy it
over a trusted channel and delete it (here and there) once the node is in.

On the new node (Linux):
  scp -r ${dir} <node>:
  ssh -t <node> sudo ./${dir##*/}/setup.sh [--node-ip <its LAN address>]

It must reach ${url#https://}/tcp, and the nodes each other on 8472/udp
(flannel VXLAN) and 10250/tcp (kubelet): open them in its firewall.

Then, here:
  deploy/lab/nodes.sh loadgen <node name>     (kubectl get nodes)
EOT
}

loadgen() {
    local node="${1:?usage: nodes.sh loadgen <node>}"
    kubectl get node "${node}" >/dev/null
    [[ "${node}" != "$(platform_node)" ]] \
        || die "${node} is the platform node; the platform and, by default, the server run there"
    kubectl label node "${node}" "${LOADGEN_ROLE}=true" --overwrite
    # The taint of earlier lab layouts kept everything else off the node.
    if kubectl get node "${node}" --output 'jsonpath={.spec.taints[*].key}' | grep -qwF "${LOADGEN_ROLE}"; then
        kubectl taint node "${node}" "${LOADGEN_ROLE}-"
    fi
    status
}

platform() {
    local node="${1:?usage: nodes.sh platform <node>}" current answer
    kubectl get node "${node}" >/dev/null
    current="$(platform_node)"
    if [[ "${node}" == "${current}" ]]; then
        echo "${node} is the platform node already"
        return
    fi
    [[ "$(kubectl get node "${node}" --output 'jsonpath={.metadata.labels.node-role\.kubernetes\.io/loadgen}')" != true ]] \
        || die "${node} is a load generator node; the platform runs elsewhere"
    if [[ -n "${current}" ]]; then
        cat <<EOT
The platform moves from ${current} to ${node}. Its volumes (Prometheus history,
the shared Kafka's data) stay on ${current}: platform.sh then stops until they
are deleted, with their data, or the label moves back. The registry stays
where it is (registry.sh move). The server follows on its next install
(placement platform).
EOT
        read -r -p "Continue? [y/N] " answer
        [[ "${answer}" == [yY] ]] || die "aborted"
        kubectl label node "${current}" "${PLATFORM_ROLE}-"
    fi
    kubectl label node "${node}" "${PLATFORM_ROLE}=true" --overwrite
    echo "Next: deploy/lab/platform.sh"
    status
}

status() {
    kubectl get nodes --label-columns "${PLATFORM_ROLE},${LOADGEN_ROLE}" --output wide
    echo
    kubectl get nodes --output 'custom-columns=NODE:.metadata.name,TAINTS:.spec.taints[*].key'
    warn_roleless_nodes
}

remove() {
    local node="${1:?usage: nodes.sh remove <node>}"
    [[ "${node}" != "$(platform_node)" ]] || die "${node} is the platform node; make another one the platform node first (nodes.sh platform)"
    log "Drain and delete ${node}"
    kubectl drain "${node}" --ignore-daemonsets --delete-emptydir-data --timeout=120s
    kubectl delete node "${node}"
    echo "On ${node} itself, uninstall the agent: /usr/local/bin/k3s-agent-uninstall.sh"
}

case "${1:-}" in
    join)    join "${2:-}" ;;
    loadgen) loadgen "${2:-}" ;;
    platform) platform "${2:-}" ;;
    status)  status ;;
    remove)  remove "${2:-}" ;;
    *)       usage ;;
esac
