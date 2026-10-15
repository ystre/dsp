#!/usr/bin/env bash
#
# Checks the release artifacts: CSAR validity (unsigned, and signed with a
# throwaway certificate to exercise SOL004 option 1), and that the binary
# archive runs.

source "$(dirname "$(realpath "$0")")/lib.sh"
stage "Package test"
cd "${WORKSPACE}"

mkdir -p "${WORKSPACE}/build"
scratch="$(mktemp -d "${WORKSPACE}/build/package-test.XXXXXX")"

(cd "${DIST}" && sha256sum --quiet --check SHA256SUMS)
info "SHA256SUMS verified"

"${PY[@]}" tools/csar/validate.py "${DIST}/${PROJECT_NAME}-${VERSION}.csar"

if have openssl; then
    openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 1 \
        -subj "/CN=${PROJECT_NAME} package-test" -keyout "${scratch}/key.pem" -out "${scratch}/cert.pem" 2>/dev/null
    "${PY[@]}" tools/csar/build.py --chart "${DIST}/charts/${PROJECT_NAME}-${VERSION}.tgz" --out "${scratch}" \
        --sign-key "${scratch}/key.pem" --sign-cert "${scratch}/cert.pem" --suffix=-signed >/dev/null
    "${PY[@]}" tools/csar/validate.py "${scratch}/${PROJECT_NAME}-${VERSION}-signed.csar" \
        --ca-file "${scratch}/cert.pem" --no-helm
fi

mkdir -p "${scratch}/unpacked"
tar -xzf "${DIST}/${PROJECT_NAME}-${VERSION}-linux-x86_64.tar.gz" -C "${scratch}/unpacked"
"${scratch}/unpacked/bin/${PROJECT_NAME}-server" --version
"${scratch}/unpacked/bin/${PROJECT_NAME}-loadgen" --version
# Listing captured first: `tar | grep -q` would fail under pipefail (SIGPIPE when grep exits early).
debug_files="$(tar -tzf "${DIST}/${PROJECT_NAME}-${VERSION}-debug.tar.gz" | grep -c '\.debug$' || true)"
(( debug_files > 0 )) || die "debug archive holds no .debug files"
info "binary and debug archives OK"
