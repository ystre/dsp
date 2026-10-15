#!/usr/bin/env bash
#
# Lab on a single-node k3s: one long-lived release of the integration chart.
#
#   up.sh [<package>]   a released package: a CSAR (validated first) or a
#                       packaged integration chart (.tgz);
#                       default dist/<project>-<VERSION>.csar
#   up.sh --source      the working tree: builds and pushes the image
#                       (needs a release build), installs deploy/helm/umbrella
#
# Steps (each idempotent): the platform (platform.sh: registry on
# localhost:5000, operators, monitoring), the image, the release (install.sh),
# helm test.
#
#   LAB_NAMESPACE, LAB_RELEASE   default <project>
#   IMAGE_REPOSITORY             default localhost:5000/<project>
#   Extra arguments after `--` go to helm (e.g. -- --set kafka.enabled=true for a
#   dedicated Kafka cluster).
#
# The CI deploy test uses the same install step, in its own namespace
# (ci/deploy-test.sh).

source "$(dirname "$(realpath "$0")")/lib.sh"
need kubectl helm docker

ns="${LAB_NAMESPACE:-${PROJECT_NAME}}"
release="${LAB_RELEASE:-${PROJECT_NAME}}"
source_tree=0 package=""

if [[ "${1:-}" == --source ]]; then
    source_tree=1; shift
elif [[ -n "${1:-}" && "$1" != -- ]]; then
    package="$1"; shift
fi

[[ "${1:-}" == -- ]] && shift

if (( ! source_tree )); then
    package="${package:-${PROJECT_DIR}/dist/${PROJECT_NAME}-${VERSION}.csar}"
    [[ -f "${package}" ]] || die "package not found: ${package}
  build one (ci/run.sh build-release image chart package, or ci/e2e.sh: <workspace>/dist/),
  pass one (up.sh <file.csar|chart.tgz>), or deploy the working tree (up.sh --source)"
fi

"${LAB_DIR}/platform.sh"

if (( source_tree )); then
    chart="${PROJECT_DIR}/deploy/helm/umbrella"
    tag="${VERSION}"
    log "Image ${IMAGE_REPOSITORY}:${tag} (working tree)"
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
    image="${IMAGE_REPOSITORY}:${tag}"

    log "Image ${image}"
    loaded=""
    # The package's own image, when it carries one (CSAR_WITH_IMAGES=1).
    if [[ "${package}" == *.csar ]] && "${PY[@]}" "${PROJECT_DIR}/tools/csar/extract.py" "${package}" image --exists 2>/dev/null; then
        loaded="$("${PY[@]}" "${PROJECT_DIR}/tools/csar/extract.py" "${package}" image | docker load | sed -n 's/^Loaded image: //p')"
        [[ -n "${loaded}" ]] || die "docker load of the package's image failed"
        [[ "${loaded}" == "${image}" ]] || docker tag "${loaded}" "${image}"
    fi
    if [[ -n "${loaded}" ]] || docker image inspect "${image}" >/dev/null 2>&1; then
        echo "${loaded:+from the package; }pushing"
        docker push --quiet "${image}"
    else
        echo "not in the package nor in the local Docker; the cluster pulls it from ${IMAGE_REPOSITORY}"
    fi
fi

"${LAB_DIR}/install.sh" --chart "${chart}" --namespace "${ns}" --release "${release}" \
    --image-repository "${IMAGE_REPOSITORY}" --image-tag "${tag}" -- "$@"

log "helm test"
helm test "${release}" --namespace "${ns}" --logs

grafana_port="$(kubectl get svc -A -l app.kubernetes.io/name=grafana -o jsonpath='{.items[0].spec.ports[0].nodePort}' 2>/dev/null || true)"
cat <<EOT

Ready.
  Server:     ${release}-server.${ns}.svc:7200
  Benchmark:  python3 -I tools/bench/run.py --target k8s --namespace ${ns} --release ${release} --scenario smoke
  Dashboard:  http://localhost:${grafana_port:-<grafana NodePort>}/d/${PROJECT_NAME}-server
  Remove:     deploy/lab/down.sh --namespace ${ns} --release ${release}
EOT
