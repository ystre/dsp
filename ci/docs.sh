#!/usr/bin/env bash
#
# Documentation: generated chapters are up to date, and the AsciiDoc book
# renders without warnings (to dist/doc/).

source "$(dirname "$(realpath "$0")")/lib.sh"
stage "Docs"
cd "${WORKSPACE}"

if ! diff -u doc/generated/metrics.adoc <("${PY[@]}" tools/contract.py adoc); then
    die "doc/generated/metrics.adoc is stale: python3 -I tools/contract.py adoc > doc/generated/metrics.adoc"
fi
info "generated metrics table is up to date"

if have asciidoctor; then
    mkdir -p "${DIST}/doc"
    asciidoctor --failure-level WARN -a "revnumber=${VERSION}" -D "${DIST}/doc" doc/book.adoc
    info "rendered ${DIST}/doc/book.html"
fi
