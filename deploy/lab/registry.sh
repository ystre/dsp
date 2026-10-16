#!/usr/bin/env bash
#
# The lab registry (doc/deployment.adoc, "Lab registry"): IMAGE_REGISTRY,
# default oci-registry.internal.ystre.org:5000, on one node of the cluster
# (the control-plane node unless moved); TLS and basic authentication for
# every request.
#
#   registry.sh up                 creates what is missing (CA, credentials,
#                                  certificate; renews the certificate 30 days
#                                  before it expires) and deploys or updates the
#                                  registry. Idempotent; platform.sh runs it.
#   registry.sh node-config <dir>  writes the setup bundle of one machine into
#                                  the new directory <dir>: CA certificate, pull
#                                  credentials, setup.sh. On that machine:
#                                  sudo <dir>/setup.sh (host name, Docker and
#                                  k3s trust and credentials)
#   registry.sh login              docker login with the push credentials
#   registry.sh check              is this machine set up: name, TLS, auth, Docker
#   registry.sh status             certificate validity, the registry pod
#   registry.sh move <node>        runs the registry on another node from now on;
#                                  its images are deleted (push them again)
#
# Secrets outside the cluster, owner-only, in LAB_STATE_DIR/registry (default
# ~/.local/state/<project>-lab/registry); back it up:
#   ca.key, ca.crt        lab CA (ECDSA P-256, 10 years, path length 0,
#                         name-constrained to the registry's host name)
#   tls.key, tls.crt      registry certificate (1 year, serverAuth)
#   push.password         for `docker login` (push and pull)
#   pull.password         for the nodes' containerd (registries.yaml)
#   htpasswd, http.secret what the registry mounts (Secret registry-auth)
#   node                  the node the registry runs on (`up` records the
#                         control-plane node; `move` changes it)
#
# The registry's htpasswd authentication does not restrict what an
# authenticated user may do: both accounts can push. They are separate so
# that the credentials spread to every node can be rotated on their own
# (delete pull.password, `registry.sh up`, new bundles for the nodes).

source "$(dirname "$(realpath "$0")")/lib.sh"

STATE="${LAB_STATE_DIR}/registry"
NS=registry
CERT_DAYS=365
RENEW_DAYS=30
HTPASSWD_IMAGE=httpd:2.4.62-alpine

[[ "${IMAGE_REGISTRY}" =~ ^[a-z0-9]([-a-z0-9.]*[a-z0-9])?:[0-9]+$ ]] \
    || die "IMAGE_REGISTRY must be HOST:PORT with a DNS host name, got '${IMAGE_REGISTRY}'"

usage() { sed -n '/^#   registry.sh/,/^#$/p' "$0" >&2; exit 1; }

# ------------------------------------------------------------------------------
# Secrets on disk
# ------------------------------------------------------------------------------

# random_secret <file>: 192 random bits, hex (no quoting issues in YAML or shell).
random_secret() {
    [[ -s "$1" ]] && return
    openssl rand -hex 24 > "$1"
    echo "created $(basename "$1")"
}

ensure_ca() {
    if [[ -s "${STATE}/ca.key" && -s "${STATE}/ca.crt" ]]; then
        openssl x509 -in "${STATE}/ca.crt" -noout -ext nameConstraints | grep -qF "DNS:${REGISTRY_HOST}" \
            || die "the lab CA (${STATE}/ca.crt) is constrained to another host name than ${REGISTRY_HOST}; move ${STATE} aside to start over"
        openssl x509 -in "${STATE}/ca.crt" -noout -checkend $(( RENEW_DAYS * 86400 )) >/dev/null \
            || die "the lab CA (${STATE}/ca.crt) expires within ${RENEW_DAYS} days; move ${STATE} aside, then 'registry.sh up' and new node bundles"
        return
    fi
    openssl req -x509 -new -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -sha256 -days 3650 \
        -subj "/CN=${PROJECT_NAME} lab registry CA" \
        -addext "basicConstraints=critical,CA:TRUE,pathlen:0" \
        -addext "keyUsage=critical,keyCertSign,cRLSign" \
        -addext "nameConstraints=critical,permitted;DNS:${REGISTRY_HOST}" \
        -keyout "${STATE}/ca.key" -out "${STATE}/ca.crt" 2>/dev/null
    echo "created the lab CA (${STATE}/ca.crt)"
}

# cert_ok: the registry certificate exists, is for this host, from this CA and not due for renewal.
cert_ok() {
    [[ -s "${STATE}/tls.key" && -s "${STATE}/tls.crt" ]] \
        && openssl verify -CAfile "${STATE}/ca.crt" "${STATE}/tls.crt" >/dev/null 2>&1 \
        && openssl x509 -in "${STATE}/tls.crt" -noout -checkhost "${REGISTRY_HOST}" | grep -q 'does match' \
        && openssl x509 -in "${STATE}/tls.crt" -noout -checkend $(( RENEW_DAYS * 86400 )) >/dev/null
}

ensure_cert() {
    cert_ok && return
    # A new key with every certificate.
    openssl req -new -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -sha256 \
        -subj "/CN=${REGISTRY_HOST}" -keyout "${STATE}/tls.key.new" 2>/dev/null \
        | openssl x509 -req -sha256 -days "${CERT_DAYS}" \
            -CA "${STATE}/ca.crt" -CAkey "${STATE}/ca.key" -set_serial "0x$(openssl rand -hex 16)" \
            -extfile <(printf '%s\n' "basicConstraints=critical,CA:FALSE" "keyUsage=critical,digitalSignature" \
                           "extendedKeyUsage=serverAuth" "subjectAltName=DNS:${REGISTRY_HOST}") \
            -out "${STATE}/tls.crt.new" 2>/dev/null
    mv "${STATE}/tls.key.new" "${STATE}/tls.key"
    mv "${STATE}/tls.crt.new" "${STATE}/tls.crt"
    echo "issued the registry certificate for ${REGISTRY_HOST}, valid until $(openssl x509 -in "${STATE}/tls.crt" -noout -enddate | cut -d= -f2)"
}

# bcrypt_line <user> <password file>: one htpasswd line. The password goes through stdin only.
# htpasswd's default cost: the passwords are 192 random bits, out of reach of guessing at any
# cost, and the registry checks the hash on every request of a pull.
bcrypt_line() {
    if command -v htpasswd >/dev/null; then
        htpasswd -niB "$1" < "$2"
    else
        need docker
        docker run --rm --interactive --network none "${HTPASSWD_IMAGE}" htpasswd -niB "$1" < "$2"
    fi | sed '/^$/d'
}

ensure_htpasswd() {
    # Rebuilt only when a password changed (bcrypt salts would change it on every run).
    if [[ -s "${STATE}/htpasswd" && ! "${STATE}/push.password" -nt "${STATE}/htpasswd" \
          && ! "${STATE}/pull.password" -nt "${STATE}/htpasswd" ]]; then
        return
    fi
    { bcrypt_line push "${STATE}/push.password"; bcrypt_line pull "${STATE}/pull.password"; } > "${STATE}/htpasswd.new"
    [[ "$(wc -l < "${STATE}/htpasswd.new")" == 2 ]] || die "creating the htpasswd file failed"
    mv "${STATE}/htpasswd.new" "${STATE}/htpasswd"
    echo "wrote htpasswd (users push, pull)"
}

ensure_state() {
    need openssl
    mkdir -p "${STATE}"
    chmod 700 "${LAB_STATE_DIR}" "${STATE}"
    umask 077
    ensure_ca
    ensure_cert
    random_secret "${STATE}/push.password"
    random_secret "${STATE}/pull.password"
    random_secret "${STATE}/http.secret"
    ensure_htpasswd
}

need_state() {
    [[ -s "${STATE}/ca.crt" ]] || die "no lab registry state in ${STATE}; run deploy/lab/registry.sh up first"
}

# ------------------------------------------------------------------------------
# Cluster
# ------------------------------------------------------------------------------

# registry_node: the node the registry runs on (and its data lives on).
registry_node() {
    if [[ -s "${STATE}/node" ]]; then
        cat "${STATE}/node"
    else
        control_plane metadata.name
    fi
}

registry_ip() {
    local node
    node="$(registry_node)"
    [[ -n "${node}" ]] || return 0
    node_ipv4 "${node}"
}

# volume_node: the node of the registry's data volume (local-path), if it has one yet.
volume_node() {
    local pv
    pv="$(kubectl get pvc registry-data --namespace "${NS}" --ignore-not-found --output 'jsonpath={.spec.volumeName}')"
    [[ -n "${pv}" ]] || return 0
    kubectl get pv "${pv}" --ignore-not-found \
        --output 'jsonpath={.spec.nodeAffinity.required.nodeSelectorTerms[0].matchExpressions[0].values[0]}'
}

# curl_registry <path> [curl args]: an authenticated request, verified with the lab CA. The
# password reaches curl through a pipe, not its command line.
curl_registry() {
    local path="$1"; shift
    curl --silent --show-error --fail --max-time 5 --proto =https --tlsv1.3 \
        --cacert "${STATE}/ca.crt" --config <(printf 'user = "push:%s"\n' "$(< "${STATE}/push.password")") \
        "$@" "https://${IMAGE_REGISTRY}${path}"
}

up() {
    need kubectl curl
    log "Registry ${IMAGE_REGISTRY}: certificates and credentials (${STATE})"
    ensure_state

    log "Registry ${IMAGE_REGISTRY}: deployment"
    local node hostname ip checksum volume
    node="$(registry_node)"
    [[ -n "${node}" ]] || die "no control-plane node found"
    hostname="$(kubectl get node "${node}" --output 'jsonpath={.metadata.labels.kubernetes\.io/hostname}')" \
        || die "the registry node ${node} is not in the cluster (${STATE}/node; registry.sh move <node>)"
    ip="$(registry_ip)"
    [[ -n "${hostname}" && -n "${ip}" ]] || die "node ${node} has no hostname label or no internal address"
    [[ -s "${STATE}/node" ]] || echo "${node}" > "${STATE}/node"
    volume="$(volume_node)"
    [[ -z "${volume}" || "${volume}" == "${hostname}" ]] \
        || die "the registry's data volume is on ${volume}, not on ${node}; registry.sh move ${node} moves it (its images are deleted)"
    kubectl create namespace "${NS}" --dry-run=client -o yaml | kubectl apply -f -

    # Objects of earlier lab layouts (a per-node proxy to a Service).
    kubectl delete --namespace "${NS}" --ignore-not-found daemonset/registry-proxy configmap/registry-proxy service/registry

    kubectl create secret tls registry-tls --namespace "${NS}" \
        --cert "${STATE}/tls.crt" --key "${STATE}/tls.key" --dry-run=client -o yaml | kubectl apply -f -
    kubectl create secret generic registry-auth --namespace "${NS}" \
        --from-file htpasswd="${STATE}/htpasswd" --from-file http-secret="${STATE}/http.secret" \
        --dry-run=client -o yaml | kubectl apply -f -

    checksum="$(cat "${STATE}/tls.crt" "${STATE}/htpasswd" "${STATE}/http.secret" | sha256sum | cut -c1-16)"
    sed -e "s/__PORT__/${REGISTRY_PORT}/g" -e "s/__CHECKSUM__/${checksum}/g" -e "s/__NODE__/${hostname}/g" \
        "${LAB_DIR}/registry.yaml" \
        | kubectl apply -f -
    kubectl --namespace "${NS}" rollout status deployment/registry --timeout=180s

    local tries=0
    until curl_registry /v2/ --resolve "${IMAGE_REGISTRY}:${ip}" >/dev/null 2>&1; do
        (( ++tries < 30 )) || { curl_registry /v2/ --resolve "${IMAGE_REGISTRY}:${ip}" || true; die "the registry does not answer on ${ip}:${REGISTRY_PORT}"; }
        sleep 1
    done
    echo "serving on ${node} ${ip}:${REGISTRY_PORT} (TLS 1.3, authenticated)"
}

move() {
    local node="${1:?usage: registry.sh move <node>}" current answer
    need kubectl
    need_state
    kubectl get node "${node}" >/dev/null || die "no node ${node} in the cluster (kubectl get nodes)"
    [[ -z "$(kubectl get node "${node}" --output 'jsonpath={.spec.taints[?(@.effect=="NoSchedule")].key}')" ]] \
        || die "${node} has a NoSchedule taint; the registry does not tolerate it"
    current="$(registry_node)"
    [[ "${node}" != "${current}" ]] || die "the registry already runs on ${node}"
    cat <<EOT
The registry moves from ${current} to ${node}. Its data volume (on ${current}) is
deleted with every image in it: push them again afterwards (up.sh --source).
EOT
    read -r -p "Continue? [y/N] " answer
    [[ "${answer}" == [yY] ]] || die "aborted"
    kubectl delete --namespace "${NS}" --ignore-not-found --wait deployment/registry pvc/registry-data
    echo "${node}" > "${STATE}/node"
    up
    cat <<EOT

The name ${REGISTRY_HOST} must now resolve to $(registry_ip) everywhere:
  * a DNS record: change it; nothing to do on the machines
  * /etc/hosts (setup.sh's block): a new bundle and setup.sh on every machine
    (registry.sh node-config, nodes.sh join for agents not yet joined)
Then registry.sh check, and push the images again.
EOT
}

# ------------------------------------------------------------------------------
# Machines
# ------------------------------------------------------------------------------

node_config() {
    local dir="${1:?usage: registry.sh node-config <dir>}" ip
    need kubectl
    need_state
    [[ ! -e "${dir}" ]] || die "${dir} exists; the bundle goes into a new directory"
    ip="$(registry_ip)"
    [[ -n "${ip}" ]] || die "no address for the registry node ($(registry_node)); is it up (registry.sh up)?"

    (umask 077 && mkdir -p "${dir}")
    install -m 600 "${STATE}/ca.crt" "${dir}/ca.crt"
    install -m 700 "${LAB_DIR}/node-setup.sh" "${dir}/setup.sh"
    (umask 077 && cat > "${dir}/registry.env" <<EOT
# Read by setup.sh. Holds the registry's pull password: keep this directory private.
LAB_NAME=${PROJECT_NAME}-lab
REGISTRY_HOST=${REGISTRY_HOST}
REGISTRY_PORT=${REGISTRY_PORT}
REGISTRY_IP=${ip}
REGISTRY_USER=pull
REGISTRY_PASSWORD=$(< "${STATE}/pull.password")
EOT
    )
    echo "wrote ${dir}: it holds credentials, keep it private and delete it once used."
    echo "Copy it to the machine (scp -r), then run there: sudo ./${dir##*/}/setup.sh   (--help for the options)"
}

login() {
    need docker
    need_state
    docker login "${IMAGE_REGISTRY}" --username push --password-stdin < "${STATE}/push.password"
    if ! grep -qE '"(credsStore|credHelpers)"' "${HOME}/.docker/config.json" 2>/dev/null; then
        warn "Docker keeps the password base64-encoded in ~/.docker/config.json; a credential helper (docker-credential-pass, -secretservice) stores it encrypted"
    fi
}

check() {
    need curl getent
    need_state
    local failed=0 ip resolved
    ip="$(registry_ip 2>/dev/null || true)"
    resolved="$(getent ahostsv4 "${REGISTRY_HOST}" | awk 'NR == 1 { print $1 }' || true)"
    if [[ -z "${resolved}" ]]; then
        echo "FAIL  ${REGISTRY_HOST} does not resolve (a DNS record, or setup.sh adds it to /etc/hosts)"; failed=1
    elif [[ -n "${ip}" && "${resolved}" != "${ip}" ]]; then
        echo "FAIL  ${REGISTRY_HOST} resolves to ${resolved}, the registry node ($(registry_node)) is ${ip}"; failed=1
    else
        echo "ok    ${REGISTRY_HOST} resolves to ${resolved}"
    fi
    if curl_registry /v2/ >/dev/null; then
        echo "ok    https://${IMAGE_REGISTRY}/v2/: TLS verified with the lab CA, authenticated"
    else
        echo "FAIL  https://${IMAGE_REGISTRY}/v2/ (is it deployed? registry.sh up)"; failed=1
    fi
    if command -v docker >/dev/null; then
        if cmp --silent "${STATE}/ca.crt" "/etc/docker/certs.d/${IMAGE_REGISTRY}/ca.crt"; then
            echo "ok    Docker trusts the lab CA for ${IMAGE_REGISTRY}"
        else
            echo "FAIL  Docker does not trust the lab CA (/etc/docker/certs.d/${IMAGE_REGISTRY}/ca.crt; setup.sh)"; failed=1
        fi
        if grep -qF "\"${IMAGE_REGISTRY}\"" "${HOME}/.docker/config.json" 2>/dev/null; then
            echo "ok    Docker has credentials for ${IMAGE_REGISTRY}"
        else
            echo "FAIL  Docker has no credentials for ${IMAGE_REGISTRY} (registry.sh login)"; failed=1
        fi
    fi
    if (( failed )); then
        echo "Set this machine up: deploy/lab/registry.sh node-config <new dir> && sudo <dir>/setup.sh && deploy/lab/registry.sh login" >&2
    fi
    return "${failed}"
}

status() {
    need kubectl
    need_state
    echo "CA:          $(openssl x509 -in "${STATE}/ca.crt" -noout -subject -enddate | paste -sd ' ')"
    echo "Certificate: $(openssl x509 -in "${STATE}/tls.crt" -noout -subject -enddate | paste -sd ' ')"
    echo "Node:        $(registry_node) $(registry_ip 2>/dev/null || true)"
    cert_ok || echo "             due for renewal: registry.sh up"
    kubectl --namespace "${NS}" get pods --selector app=registry --output wide
}

case "${1:-}" in
    up)          up ;;
    node-config) node_config "${2:-}" ;;
    login)       login ;;
    check)       check ;;
    status)      status ;;
    move)        move "${2:-}" ;;
    *)           usage ;;
esac
