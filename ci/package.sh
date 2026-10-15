#!/usr/bin/env bash
#
# Release artifacts in dist/:
#   charts/*.tgz                         the four Helm charts
#   <project>-<ver>-linux-x86_64.tar.gz  binaries (debug info stripped)
#   <project>-<ver>-debug.tar.gz         debug info in the .build-id layout (baldr-cpp)
#   <project>-<ver>.csar                 ETSI SOL004 package (tools/csar/build.py)
#   SHA256SUMS
# CSAR_WITH_IMAGES=1 bundles the runtime image (needs docker and ci/image.sh).
#
# Expects a clean workspace (ci/e2e.sh); intermediate files go to a fresh
# directory under build/.

source "$(dirname "$(realpath "$0")")/lib.sh"
stage "Package"
cd "${WORKSPACE}"

mkdir -p "${DIST}/charts" "${WORKSPACE}/build"
scratch="$(mktemp -d "${WORKSPACE}/build/package.XXXXXX")"

if have helm; then
    for chart in "${CHARTS[@]}"; do
        helm package "deploy/helm/${chart}" --destination "${DIST}/charts" >/dev/null
    done
    info "charts: $(cd "${DIST}/charts" && echo *.tgz)"
fi

# Binaries and their debug info.
install_dir="${scratch}/install"
debug_root="${scratch}/debug-info"
cmake --install "${BUILD_RELEASE}" --prefix "${install_dir}" --component runtime >/dev/null
tools/debug-info.sh "${install_dir}" >/dev/null
while IFS= read -r -d '' dbg; do
    id="$(readelf -n "${dbg%.debug}" | awk '/Build ID/ { print $3 }')"
    [[ "${id}" =~ ^[0-9a-f]{16,}$ ]] || die "no build ID in ${dbg%.debug}"
    mkdir -p "${debug_root}/lib/debug/.build-id/${id:0:2}"
    mv "${dbg}" "${debug_root}/lib/debug/.build-id/${id:0:2}/${id:2}.debug"
done < <(find "${install_dir}" -name '*.debug' -print0)
tar -czf "${DIST}/${PROJECT_NAME}-${VERSION}-linux-x86_64.tar.gz" -C "${install_dir}" .
tar -czf "${DIST}/${PROJECT_NAME}-${VERSION}-debug.tar.gz" -C "${debug_root}" lib/debug
info "binaries and debug info archived"

# VNF package.
csar_args=(--chart "${DIST}/charts/${PROJECT_NAME}-${VERSION}.tgz" --out "${DIST}")
if [[ "${CSAR_WITH_IMAGES:-0}" == 1 ]] && have docker; then
    docker save "${IMAGE}" -o "${scratch}/image.tar"
    csar_args+=(--image-tar "${scratch}/image.tar")
fi
"${PY[@]}" tools/csar/build.py "${csar_args[@]}" >/dev/null
info "csar: ${PROJECT_NAME}-${VERSION}.csar"

(cd "${DIST}" && find . -maxdepth 2 -type f \( -name '*.tgz' -o -name '*.tar.gz' -o -name '*.csar' \) -printf '%P\0' \
    | sort -z | xargs -0 sha256sum > SHA256SUMS)
