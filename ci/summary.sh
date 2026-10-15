#!/usr/bin/env bash

source "$(dirname "$(realpath "$0")")/lib.sh"
stage "Summary"

info "${PROJECT_NAME} ${VERSION} ($(git -C "${WORKSPACE}" rev-parse --short HEAD 2>/dev/null || echo 'no commit'))"
if [[ -d "${DIST}" ]]; then
    info "Artifacts in ${DIST}:"
    (cd "${DIST}" && find . -type f -not -path './doc/*' -printf '%P\t%s\n' | sort \
        | awk -F'\t' '{ printf "    %-48s %10.1f KiB\n", $1, $2 / 1024 }')
    [[ -f "${DIST}/doc/book.html" ]] && info "Documentation: ${DIST}/doc/book.html"
fi
if [[ -d "${RESULTS}" ]]; then
    info "Benchmark results in ${RESULTS}:"
    find "${RESULTS}" -name summary.json -printf '    %h\n' | sort
fi

"${WORKSPACE}/tools/workspace-usage.sh" "${WORKSPACE_ROOT}" || true
