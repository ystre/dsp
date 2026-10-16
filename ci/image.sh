#!/usr/bin/env bash
#
# Runtime images (env/runtime.Dockerfile), one per component with its release
# binary: <registry>/<project>-server and <registry>/<project>-loadgen.
#   IMAGE_REGISTRY  default oci-registry.internal.ystre.org:5000 (the lab registry)
#   IMAGE_PUSH=1    push them (Docker must trust and be logged in to the
#                   registry: deploy/lab/registry.sh check)

source "$(dirname "$(realpath "$0")")/lib.sh"
stage "Image"

have docker || exit 0

mkdir -p "${WORKSPACE}/build"
context="$(mktemp -d "${WORKSPACE}/build/image.XXXXXX")"
for component in "${IMAGES[@]}"; do
    mkdir -p "${context}/bin/${component}"
    cp "${BUILD_RELEASE}/bin/${PROJECT_NAME}-${component}" "${context}/bin/${component}/"
done
strip --strip-debug "${context}"/bin/*/*

revision="$(git -C "${WORKSPACE}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
for component in "${IMAGES[@]}"; do
    image="$(image_ref "${component}")"
    docker build --file "${WORKSPACE}/env/runtime.Dockerfile" --target "${component}" --tag "${image}" \
        --label "org.opencontainers.image.version=${VERSION}" \
        --label "org.opencontainers.image.revision=${revision}" \
        --label "org.opencontainers.image.title=${PROJECT_NAME}-${component}" \
        "${context}"
    info "built ${image}"
done

docker run --rm --entrypoint "/usr/local/bin/${PROJECT_NAME}-server" "$(image_ref server)" --version
docker run --rm --entrypoint "/usr/local/bin/${PROJECT_NAME}-loadgen" "$(image_ref loadgen)" --capabilities >/dev/null
info "both binaries run in their images"

if [[ "${IMAGE_PUSH:-0}" == 1 ]]; then
    for component in "${IMAGES[@]}"; do
        docker push "$(image_ref "${component}")" \
            || die "pushing to ${IMAGE_REGISTRY} failed (deploy/lab/registry.sh check)"
    done
fi
