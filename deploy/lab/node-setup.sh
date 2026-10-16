#!/usr/bin/env bash
#
# Sets one machine of the lab up for the lab registry and, with a join bundle,
# joins it to the cluster as a k3s agent. Runs as root on that machine, from
# the bundle written by deploy/lab/registry.sh node-config (registry only) or
# deploy/lab/nodes.sh join (registry and join). Self-contained: the machine
# needs no checkout of the repository.
#
#   sudo ./setup.sh [--dns] [--node-ip <ip>] [--flannel-iface <if>] [--no-docker] [--no-restart]
#
#   --dns                       the registry's name is in DNS (resolves to the
#                               registry node here): no /etc/hosts entry, so a
#                               move of the registry needs only a new record
#   --node-ip, --flannel-iface  k3s agent: the address and interface to use on a
#                               machine with several (Wi-Fi and Ethernet, VPN)
#   --no-docker                 do not make Docker trust the registry
#   --no-restart                do not restart k3s after a change of its
#                               registry configuration (it applies on restart)
#
# Idempotent. What it changes:
#   /etc/hosts                               a marked block: the registry's host name, or
#                                            none with --dns (previous version kept as
#                                            /etc/hosts.<lab>.bak)
#   /etc/docker/certs.d/<host:port>/ca.crt   Docker trusts the lab CA for this registry only
#   /etc/rancher/k3s/registries.yaml         containerd: CA and pull credentials (mode 600);
#                                            refused if the file exists and is not ours
#   /etc/rancher/k3s/<lab>-registry-ca.crt   the CA for containerd
#   k3s agent                                with join.env: get.k3s.io, pinned version

set -euo pipefail

BUNDLE="$(dirname "$(realpath "$0")")"

die()  { printf '\033[31merror: %s\033[0m\n' "$*" >&2; exit 1; }
log()  { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
usage() { sed -n '/^#   sudo/,/^# Idempotent/p' "$0" | sed -e '$d' -e 's/^# \{0,1\}//'; exit "${1:-0}"; }

node_ip="" flannel_iface="" docker=1 restart=1 dns=0
while (( $# )); do
    case "$1" in
        --dns)           dns=1; shift ;;
        --node-ip)       node_ip="${2:?--node-ip needs an address}"; shift 2 ;;
        --flannel-iface) flannel_iface="${2:?--flannel-iface needs an interface}"; shift 2 ;;
        --no-docker)     docker=0; shift ;;
        --no-restart)    restart=0; shift ;;
        -h|--help)       usage ;;
        *)               echo "unknown argument: $1" >&2; usage 1 ;;
    esac
done

(( EUID == 0 )) || die "run as root (sudo $0)"

[[ -r "${BUNDLE}/registry.env" && -r "${BUNDLE}/ca.crt" ]] || die "${BUNDLE} is not a registry bundle (registry.sh node-config)"
# shellcheck source=/dev/null  # written by registry.sh node-config
source "${BUNDLE}/registry.env"
[[ "${LAB_NAME:-}" =~ ^[a-z0-9-]+$ ]]                            || die "registry.env: bad LAB_NAME"
[[ "${REGISTRY_HOST:-}" =~ ^[a-z0-9]([-a-z0-9.]*[a-z0-9])?$ ]]  || die "registry.env: bad REGISTRY_HOST"
[[ "${REGISTRY_PORT:-}" =~ ^[0-9]+$ ]]                          || die "registry.env: bad REGISTRY_PORT"
[[ "${REGISTRY_IP:-}" =~ ^[0-9]+(\.[0-9]+){3}$ ]]               || die "registry.env: bad REGISTRY_IP"
[[ "${REGISTRY_USER:-}" =~ ^[a-z]+$ && "${REGISTRY_PASSWORD:-}" =~ ^[0-9a-f]{32,}$ ]] \
    || die "registry.env: bad credentials"
REGISTRY="${REGISTRY_HOST}:${REGISTRY_PORT}"

join=0
if [[ -e "${BUNDLE}/join.env" ]]; then
    # shellcheck source=/dev/null  # written by nodes.sh join
    source "${BUNDLE}/join.env"
    [[ "${K3S_URL:-}" =~ ^https://[0-9.]+:[0-9]+$ ]]       || die "join.env: bad K3S_URL"
    [[ "${K3S_VERSION:-}" =~ ^v[0-9.]+\+k3s[0-9]+$ ]]      || die "join.env: bad K3S_VERSION"
    [[ -n "${K3S_TOKEN:-}" ]]                              || die "join.env: incomplete"
    join=1
fi

# ------------------------------------------------------------------------------

if (( dns )); then
    log "Host name ${REGISTRY_HOST}: DNS (no /etc/hosts entry)"
else
    log "Host name ${REGISTRY_HOST} -> ${REGISTRY_IP} (/etc/hosts)"
fi
begin="# BEGIN ${LAB_NAME} registry (managed by its setup.sh)"
end="# END ${LAB_NAME} registry"
others="$(awk -v b="${begin}" -v e="${end}" -v h="${REGISTRY_HOST}" \
    '$0 == b { skip = 1 } !skip && $1 !~ /^#/ { for (i = 2; i <= NF; i++) if ($i == h) print } $0 == e { skip = 0 }' /etc/hosts)"
[[ -z "${others}" ]] || die "/etc/hosts already maps ${REGISTRY_HOST} outside the managed block: ${others}"
hosts_new="$(mktemp /etc/hosts.XXXXXX)"
awk -v b="${begin}" -v e="${end}" '$0 == b { skip = 1 } !skip { print } $0 == e { skip = 0 }' /etc/hosts > "${hosts_new}"
(( dns )) || printf '%s\n%s %s\n%s\n' "${begin}" "${REGISTRY_IP}" "${REGISTRY_HOST}" "${end}" >> "${hosts_new}"
hosts_changed=0
if cmp --silent "${hosts_new}" /etc/hosts; then
    echo "unchanged"
else
    # Rewritten in place: /etc/hosts may be a bind mount (containers) or a symbolic link.
    cp -p /etc/hosts "/etc/hosts.${LAB_NAME}.bak"
    cat "${hosts_new}" > /etc/hosts
    hosts_changed=1
    echo "updated (previous version: /etc/hosts.${LAB_NAME}.bak)"
fi
rm -f "${hosts_new}"
if (( dns )); then
    # Through the system's resolver, as containerd and Docker resolve it. Not DNS alone: a
    # local resolver (systemd-resolved) also answers from /etc/hosts.
    resolved="$(getent ahostsv4 "${REGISTRY_HOST}" | awk 'NR == 1 { print $1 }' || true)"
    if [[ "${resolved}" != "${REGISTRY_IP}" ]]; then
        restored=""
        if (( hosts_changed )); then
            cat "/etc/hosts.${LAB_NAME}.bak" > /etc/hosts
            restored=" (/etc/hosts restored)"
        fi
        die "${REGISTRY_HOST} resolves to ${resolved:-nothing} here, not to the registry node ${REGISTRY_IP}; add the DNS record (A ${REGISTRY_IP}; some routers drop public answers with private addresses: use the LAN's DNS then), or run without --dns${restored}"
    fi
    echo "${REGISTRY_HOST} resolves to ${resolved}"
fi

if (( docker )) && { command -v dockerd >/dev/null || [[ -d /etc/docker ]]; }; then
    log "Docker: trust the lab CA for ${REGISTRY}"
    install -D -m 644 "${BUNDLE}/ca.crt" "/etc/docker/certs.d/${REGISTRY}/ca.crt"
    echo "/etc/docker/certs.d/${REGISTRY}/ca.crt (read on every connection; no restart)"
fi

k3s_unit=""
for unit in k3s k3s-agent; do
    systemctl cat "${unit}.service" >/dev/null 2>&1 && k3s_unit="${unit}"
done

if [[ -n "${k3s_unit}" ]] || (( join )); then
    log "k3s containerd: CA and pull credentials for ${REGISTRY}"
    ca_file="/etc/rancher/k3s/${LAB_NAME}-registry-ca.crt"
    marker="# Managed by the ${LAB_NAME} setup.sh: rewritten on every run."
    install -d -m 755 /etc/rancher/k3s
    install -m 644 "${BUNDLE}/ca.crt" "${ca_file}"
    wanted="$(cat <<EOT
${marker}
configs:
  "${REGISTRY}":
    auth:
      username: ${REGISTRY_USER}
      password: ${REGISTRY_PASSWORD}
    tls:
      ca_file: ${ca_file}
EOT
)"
    target=/etc/rancher/k3s/registries.yaml
    if [[ -e "${target}" ]] && ! head -n 1 "${target}" | grep -qxF "${marker}"; then
        die "${target} exists and is not managed by this script; merge this into it by hand (keep it mode 600), then restart k3s:
$(sed -e '1d' -e 's/password: .*/password: <REGISTRY_PASSWORD from registry.env>/' <<< "${wanted}")"
    fi
    changed=0
    if [[ ! -e "${target}" ]] || [[ "$(< "${target}")" != "${wanted}" ]]; then
        (umask 077 && printf '%s\n' "${wanted}" > "${target}.new")
        mv "${target}.new" "${target}"
        changed=1
    fi
    chmod 600 "${target}"
    echo "${target} $( (( changed )) && echo updated || echo unchanged)"
    if (( changed )) && [[ -n "${k3s_unit}" ]] && systemctl is-active --quiet "${k3s_unit}"; then
        if (( restart )); then
            echo "restarting ${k3s_unit} (running pods keep running)"
            systemctl restart "${k3s_unit}"
        else
            echo "restart ${k3s_unit} for it to apply: systemctl restart ${k3s_unit}"
        fi
    fi
fi

if (( join )); then
    log "Join ${K3S_URL} as a k3s agent (${K3S_VERSION})"
    if [[ "${k3s_unit}" == k3s ]]; then
        die "this machine runs a k3s server; a load generator node must be a separate machine"
    elif [[ "${k3s_unit}" == k3s-agent ]]; then
        echo "already a k3s agent; to move it to another cluster, uninstall it first (k3s-agent-uninstall.sh)"
    else
        command -v curl >/dev/null || die "curl is required"
        args=(agent)
        [[ -n "${node_ip}" ]] && args+=(--node-ip "${node_ip}")
        [[ -n "${flannel_iface}" ]] && args+=(--flannel-iface "${flannel_iface}")
        # The official installer; it verifies the k3s binary against the release checksums.
        # The token reaches it through the environment, not the command line.
        curl --silent --show-error --fail --location --proto =https --tlsv1.2 https://get.k3s.io \
            | INSTALL_K3S_VERSION="${K3S_VERSION}" K3S_URL="${K3S_URL}" K3S_TOKEN="${K3S_TOKEN}" sh -s - "${args[@]}"
        echo "joined as $(hostname); on the control-plane machine: deploy/lab/nodes.sh loadgen $(hostname)"
    fi
fi

if command -v curl >/dev/null; then
    log "Check https://${REGISTRY}/v2/"
    curl --silent --show-error --fail --max-time 5 --proto =https --tlsv1.3 --output /dev/null \
        --cacert "${BUNDLE}/ca.crt" \
        --config <(printf 'user = "%s:%s"\n' "${REGISTRY_USER}" "${REGISTRY_PASSWORD}") \
        "https://${REGISTRY}/v2/" \
        || die "https://${REGISTRY}/v2/ failed: is the registry up (registry.sh status), port ${REGISTRY_PORT}/tcp open on ${REGISTRY_IP}?"
    echo "reachable, TLS verified, authenticated"
fi
