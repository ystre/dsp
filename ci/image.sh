#!/usr/bin/env bash
#
# Runtime image (env/runtime.Dockerfile) with the release binaries.
#   IMAGE_REPOSITORY  default localhost:5000/<project>
#   IMAGE_PUSH=1      push it (e.g. to the lab registry)

source "$(dirname "$(realpath "$0")")/lib.sh"
stage "Image"

have docker || exit 0

mkdir -p "${WORKSPACE}/build"
context="$(mktemp -d "${WORKSPACE}/build/image.XXXXXX")"
mkdir -p "${context}/bin"
for bin in server loadgen; do
    cp "${BUILD_RELEASE}/bin/${PROJECT_NAME}-${bin}" "${context}/bin/"
done
strip --strip-debug "${context}"/bin/*

docker build --file "${WORKSPACE}/env/runtime.Dockerfile" --tag "${IMAGE}" \
    --label "org.opencontainers.image.version=${VERSION}" \
    --label "org.opencontainers.image.revision=$(git -C "${WORKSPACE}" rev-parse --short HEAD 2>/dev/null || echo unknown)" \
    --label "org.opencontainers.image.title=${PROJECT_NAME}" \
    "${context}"
docker run --rm --entrypoint "/usr/local/bin/${PROJECT_NAME}-server" "${IMAGE}" --version
docker run --rm --entrypoint "/usr/local/bin/${PROJECT_NAME}-loadgen" "${IMAGE}" --capabilities >/dev/null
info "built ${IMAGE}"

if [[ "${IMAGE_PUSH:-0}" == 1 ]]; then
    docker push "${IMAGE}"
fi
