BUILD_TYPE=Release
BUILD_DIR="${G_PROJECT_DIR}/build/${BUILD_TYPE,,}"
DOCKERFILE="service.Dockerfile"

function stage-entry() {
    cmake --build "${BUILD_DIR}" --target install
    cp "${G_PROJECT_DIR}/env/${DOCKERFILE}" "${G_ARTIFACT_DIR}/${DOCKERFILE}"

    docker build "${G_ARTIFACT_DIR}" \
        --file "${G_ARTIFACT_DIR}/${DOCKERFILE}" \
        --tag local/svc:latest
}
