#!/usr/bin/env bash
#
# Load generator nodes of the lab cluster (doc/deployment.adoc, "Multi-node
# lab"): extra k3s agents, e.g. a laptop, where the load generator runs when
# asked to (placement.dedicatedNodes, run.py --loadgen-nodes), so that it does
# not share CPUs with the server.
#
#   nodes.sh join <dir>      writes the setup bundle of a new node into the new
#                            directory <dir>: registry trust and credentials,
#                            k3s agent join (reads the cluster token with sudo);
#                            run its setup.sh on the node
#   nodes.sh loadgen <node>  makes a joined node a load generator node: label
#                            node-role.kubernetes.io/loadgen
#   nodes.sh status          the nodes, their roles and taints
#   nodes.sh remove <node>   drains and deletes a node from the cluster
#
# Run on the control-plane machine (or anywhere kubectl points at the cluster;
# `join` needs the token file of the k3s server, K3S_TOKEN_FILE).
#
# The label only attracts the load generator when it asks for these nodes; it
# keeps nothing away: they are ordinary nodes for every other workload. It is
# set here, not by the agent: the NodeRestriction admission plugin does not let
# a node give itself node-role.kubernetes.io/ labels.

source "$(dirname "$(realpath "$0")")/lib.sh"
need kubectl

ROLE="node-role.kubernetes.io/loadgen"
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
    [[ -z "$(kubectl get node "${node}" --output "jsonpath={.metadata.labels.node-role\.kubernetes\.io/control-plane}")" ]] \
        || die "${node} is the control-plane node; the server runs there"
    kubectl label node "${node}" "${ROLE}=true" --overwrite
    # The taint of earlier lab layouts kept everything else off the node.
    if kubectl get node "${node}" --output 'jsonpath={.spec.taints[*].key}' | grep -qwF "${ROLE}"; then
        kubectl taint node "${node}" "${ROLE}-"
    fi
    status
}

status() {
    kubectl get nodes --label-columns "${ROLE}" --output wide
    echo
    kubectl get nodes --output 'custom-columns=NODE:.metadata.name,TAINTS:.spec.taints[*].key'
}

remove() {
    local node="${1:?usage: nodes.sh remove <node>}"
    log "Drain and delete ${node}"
    kubectl drain "${node}" --ignore-daemonsets --delete-emptydir-data --timeout=120s
    kubectl delete node "${node}"
    echo "On ${node} itself, uninstall the agent: /usr/local/bin/k3s-agent-uninstall.sh"
}

case "${1:-}" in
    join)    join "${2:-}" ;;
    loadgen) loadgen "${2:-}" ;;
    status)  status ;;
    remove)  remove "${2:-}" ;;
    *)       usage ;;
esac
