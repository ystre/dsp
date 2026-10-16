#!/usr/bin/env bash
#
# Runs a command inside the build image (env/build.Dockerfile), where every
# tool is required (CI_STRICT_TOOLS=1). The repository and /tmp/workspace are
# mounted at their own paths; the Conan package cache persists in a Docker
# volume; the host's Docker daemon and registry credentials (image stage) and
# kube config (deploy-test) are passed through.
#
#   ci/in-container.sh ci/e2e.sh
#   ci/in-container.sh ci/run.sh build unit-test

set -euo pipefail

CI_DIR="$(dirname "$(realpath "$0")")"
PROJECT_DIR="$(realpath "${CI_DIR}/..")"

# shellcheck source=../project.env
source "${PROJECT_DIR}/project.env"

(( $# )) || { echo "usage: ci/in-container.sh <command> [args...]" >&2; exit 1; }

tag="$(cat "${PROJECT_DIR}/env/build.Dockerfile" "${PROJECT_DIR}/env/conan-profile" | sha256sum | cut -c1-12)"
image="${PROJECT_NAME}-build:${tag}"

if ! docker image inspect "${image}" >/dev/null 2>&1; then
    docker build --tag "${image}" --build-arg "USER_UID=$(id -u)" --build-arg "USER_GID=$(id -g)" \
        --file "${PROJECT_DIR}/env/build.Dockerfile" "${PROJECT_DIR}/env"
fi

mkdir -p /tmp/workspace

args=(
    --rm --interactive
    --network host
    --volume "${PROJECT_DIR}:${PROJECT_DIR}"
    --volume /tmp/workspace:/tmp/workspace
    --workdir "${PROJECT_DIR}"
    --volume "${PROJECT_NAME}-conan-cache:/home/builder/.conan2/p"
    --env CI_STRICT_TOOLS=1
)

[[ -t 1 ]] && args+=(--tty)
for var in WORKSPACE CI_RUN_ID CI_PERF_GATE CI_DEPLOY CI_DEPLOY_KEEP CI_DEPLOY_NAMESPACE \
           IMAGE_REGISTRY IMAGE_PUSH CSAR_WITH_IMAGES CI_BASE_REF GITHUB_BASE_REF; do
    [[ -n "${!var:-}" ]] && args+=(--env "${var}=${!var}")
done

if [[ -S /var/run/docker.sock ]]; then
    args+=(--volume /var/run/docker.sock:/var/run/docker.sock --group-add "$(stat -c %g /var/run/docker.sock)")
fi

# Registry credentials for `docker push` (the client sends them; the host's daemon
# does the TLS). A credential helper on the host is not available in here.
docker_config="${DOCKER_CONFIG:-${HOME}/.docker}/config.json"
if [[ -f "${docker_config}" ]]; then
    args+=(--volume "${docker_config}:/home/builder/.docker/config.json:ro")
    if grep -qE '"(credsStore|credHelpers)"' "${docker_config}"; then
        echo "warning: ${docker_config} uses a credential helper; pushes from the build container cannot use it" >&2
    fi
fi

kubeconfig="${KUBECONFIG:-${HOME}/.kube/config}"
[[ -f "${kubeconfig}" ]] && args+=(--volume "${kubeconfig}:/home/builder/.kube/config:ro")

exec docker run "${args[@]}" "${image}" "$@"
