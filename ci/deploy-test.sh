#!/usr/bin/env bash
#
# Opt-in (CI_DEPLOY=1): deploys the package under test (the integration chart
# in dist/) to the current kube context with the lab's install step
# (deploy/lab/install.sh), runs `helm test` and the `smoke` scenario on
# Kubernetes, then uninstalls the release.
#
# It runs in its own namespace, so it never touches the lab release
# (deploy/lab/up.sh). The platform (Prometheus Operator, Strimzi) must already
# be there (deploy/lab/platform.sh); this stage never installs cluster-wide
# components. The release ships no dashboard: the lab's dashboard shows the CI
# namespace through its namespace selector.
#
# Kafka topics are per run: deleted after a success, kept after a failure
# (ci/deploy-test/topics.sh). The release is uninstalled either way.
#
#   CI_DEPLOY_NAMESPACE  default <project>-ci
#   CI_DEPLOY_KEEP=1     keep the release (and the topics) afterwards
#   CI_RUN_ID            default: the UTC time; ci/e2e.sh sets it (the workspace name)
#
# The images are pushed to IMAGE_REGISTRY (default oci-registry.internal.ystre.org:5000, the lab
# registry; Docker must trust it and be logged in: deploy/lab/registry.sh check).

source "$(dirname "$(realpath "$0")")/lib.sh"
# shellcheck source=deploy-test/topics.sh
source "${CI_DIR}/deploy-test/topics.sh"
stage "Deploy test"

if [[ "${CI_DEPLOY:-0}" != 1 ]]; then
    info "skipped (set CI_DEPLOY=1 to deploy to the current kube context: $(kubectl config current-context 2>/dev/null || echo none))"
    exit 0
fi

for tool in kubectl helm docker; do command -v "${tool}" >/dev/null || die "${tool} is required"; done

ns="${CI_DEPLOY_NAMESPACE:-${PROJECT_NAME}-ci}"
release="${PROJECT_NAME}"
chart="${DIST}/charts/${PROJECT_NAME}-${VERSION}.tgz"
run_id="${CI_RUN_ID:-$(date -u +%Y%m%d-%H%M%SZ)}"
[[ -f "${chart}" ]] || die "${chart} not found (the package stage builds it)"

topics_report_kept "${ns}"

for component in "${IMAGES[@]}"; do
    docker push --quiet "$(image_ref "${component}")" \
        || die "pushing to ${IMAGE_REGISTRY} failed (deploy/lab/registry.sh check)"
done

info "deploying ${chart} as ${release} into ${ns} ($(kubectl config current-context)), run ${run_id}"

finish() {
    local status=$?
    [[ "${CI_DEPLOY_KEEP:-0}" == 1 ]] && return
    if helm status "${release}" --namespace "${ns}" >/dev/null 2>&1; then
        "${WORKSPACE}/deploy/lab/down.sh" --namespace "${ns}" --release "${release}" >/dev/null \
            || warn "uninstalling ${release} from ${ns} failed"
    fi
    topics_finish "${status}" "${ns}" "${run_id}"
}

trap finish EXIT

mapfile -t topic_args < <(topics_helm_args "${ns}" "${run_id}")

"${WORKSPACE}/deploy/lab/install.sh" \
    --chart "${chart}" --namespace "${ns}" --release "${release}" \
    --image-registry "${IMAGE_REGISTRY}" --image-tag "${VERSION}" \
    -- --set server.monitoring.dashboard.enabled=false "${topic_args[@]}"

helm test "${release}" --namespace "${ns}" --logs
"${PY[@]}" "${WORKSPACE}/tools/bench/run.py" --target k8s --namespace "${ns}" --release "${release}" \
    --scenario smoke --repeat 1 --bin-dir "${BUILD_RELEASE}/bin" --out "${RESULTS}" \
    --image-registry "${IMAGE_REGISTRY}" --image-tag "${VERSION}" --image-pull-policy Always
