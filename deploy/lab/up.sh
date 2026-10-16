#!/usr/bin/env bash
#
# Lab on k3s: one long-lived release of the integration chart. A single node
# does; load generator nodes join with nodes.sh (doc/deployment.adoc,
# "Multi-node lab").
#
#   up.sh [<package>]   a released package: a CSAR (validated first) or a
#                       packaged integration chart (.tgz);
#                       default dist/<project>-<VERSION>.csar
#   up.sh --source      the working tree: builds and pushes the images
#                       (needs a release build), installs deploy/helm/umbrella
#   --server-nodes <n>  where the server runs: control-plane (default),
#                       loadgen (the load generator nodes, e.g. the laptop)
#                       or any (install.sh)
#
# Steps (each idempotent): the platform (platform.sh: the registry, operators,
# monitoring), a check that this machine can push to the registry
# (registry.sh check), the images, the release (install.sh), helm test.
#
#   LAB_NAMESPACE, LAB_RELEASE   default <project>
#   IMAGE_REGISTRY               default oci-registry.internal.ystre.org:5000
#   Extra arguments after `--` go to helm (e.g. -- --set kafka.enabled=true for a
#   dedicated Kafka cluster).
#
# The CI deploy test uses the same install step, in its own namespace
# (ci/deploy-test.sh).

source "$(dirname "$(realpath "$0")")/lib.sh"
need kubectl helm docker

ns="${LAB_NAMESPACE:-${PROJECT_NAME}}"
release="${LAB_RELEASE:-${PROJECT_NAME}}"
source_tree=0 package="" server_nodes=control-plane

while (( $# )); do
    case "$1" in
        --source)       source_tree=1; shift ;;
        --server-nodes) server_nodes="${2:?--server-nodes needs any, control-plane or loadgen}"; shift 2 ;;
        --)             shift; break ;;
        -*)             die "unknown argument: $1 (see the header of $0)" ;;
        *)              [[ -z "${package}" ]] || die "one package only: ${package}, $1"; package="$1"; shift ;;
    esac
done
(( source_tree )) && [[ -n "${package}" ]] && die "--source or a package, not both"

if (( ! source_tree )); then
    package="${package:-${PROJECT_DIR}/dist/${PROJECT_NAME}-${VERSION}.csar}"
    [[ -f "${package}" ]] || die "package not found: ${package}
  build one (ci/run.sh build-release image chart package, or ci/e2e.sh: <workspace>/dist/),
  pass one (up.sh <file.csar|chart.tgz>), or deploy the working tree (up.sh --source)"
fi

"${LAB_DIR}/platform.sh"
log "This machine and the registry"
"${LAB_DIR}/registry.sh" check \
    || die "set this machine up for ${IMAGE_REGISTRY} first (doc/deployment.adoc, \"Lab registry\")"

if (( source_tree )); then
    chart="${PROJECT_DIR}/deploy/helm/umbrella"
    tag="${VERSION}"
    log "Images ${IMAGE_REGISTRY}/*:${tag} (working tree)"
    IMAGE_PUSH=1 "${PROJECT_DIR}/ci/run.sh" image
else
    log "Package ${package}"
    if [[ "${package}" == *.csar ]]; then
        "${PY[@]}" "${PROJECT_DIR}/tools/csar/validate.py" "${package}"
        mkdir -p "${PROJECT_DIR}/build"
        scratch="$(mktemp -d "${PROJECT_DIR}/build/lab.XXXXXX")"
        chart="$("${PY[@]}" "${PROJECT_DIR}/tools/csar/extract.py" "${package}" chart "${scratch}")"
    else
        chart="${package}"
    fi
    tag="$(chart_field "${chart}" appVersion)"

    log "Images ${IMAGE_REGISTRY}/*:${tag}"
    for component in "${IMAGES[@]}"; do
        image="${IMAGE_REGISTRY}/${PROJECT_NAME}-${component}:${tag}"
        loaded=""
        # The package's own image, when it carries one (CSAR_WITH_IMAGES=1).
        if [[ "${package}" == *.csar ]] \
            && "${PY[@]}" "${PROJECT_DIR}/tools/csar/extract.py" "${package}" image "${component}" --exists 2>/dev/null; then
            loaded="$("${PY[@]}" "${PROJECT_DIR}/tools/csar/extract.py" "${package}" image "${component}" \
                | docker load | sed -n 's/^Loaded image: //p')"
            [[ -n "${loaded}" ]] || die "docker load of the package's ${component} image failed"
            [[ "${loaded}" == "${image}" ]] || docker tag "${loaded}" "${image}"
        fi
        if [[ -n "${loaded}" ]] || docker image inspect "${image}" >/dev/null 2>&1; then
            echo "${image}: ${loaded:+from the package; }pushing"
            docker push --quiet "${image}" || die "pushing ${image} failed (deploy/lab/registry.sh check)"
        else
            echo "${image}: not in the package nor in the local Docker; the cluster pulls it from ${IMAGE_REGISTRY}"
        fi
    done
fi

"${LAB_DIR}/install.sh" --chart "${chart}" --namespace "${ns}" --release "${release}" \
    --image-registry "${IMAGE_REGISTRY}" --image-tag "${tag}" --server-nodes "${server_nodes}" -- "$@"

log "helm test"
helm test "${release}" --namespace "${ns}" --logs

grafana_port="$(kubectl get svc -A -l app.kubernetes.io/name=grafana -o jsonpath='{.items[0].spec.ports[0].nodePort}' 2>/dev/null || true)"
cat <<EOT

Ready.
  Server:     ${release}-server.${ns}.svc:7200
  Benchmark:  python3 -I tools/bench/run.py --target k8s --namespace ${ns} --release ${release} --scenario smoke
              (add --loadgen-nodes to run the load generator on the load generator nodes: deploy/lab/nodes.sh)
  Dashboard:  http://localhost:${grafana_port:-<grafana NodePort>}/d/${PROJECT_NAME}-server
  Remove:     deploy/lab/down.sh --namespace ${ns} --release ${release}
EOT
