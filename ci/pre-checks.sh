#!/usr/bin/env bash
#
# Fast static checks: versioning, naming, formatting, shell and Python hygiene,
# lint of the charts without dependencies.

source "$(dirname "$(realpath "$0")")/lib.sh"
stage "Pre-checks"
cd "${WORKSPACE}"

"${CI_DIR}/check-version-bump.sh"
tools/sync-version.sh --check
tools/rename.sh --check

if have clang-format; then
    mapfile -d '' cpp < <(source_files '*.cpp' '*.hpp')
    clang-format --dry-run --Werror "${cpp[@]}"
    info "clang-format: ${#cpp[@]} files formatted"
fi

if have shellcheck; then
    mapfile -d '' sh < <(source_files '*.sh')
    shellcheck --external-sources --source-path=SCRIPTDIR "${sh[@]}"
    info "shellcheck: ${#sh[@]} scripts clean"
fi

mapfile -d '' py < <(source_files '*.py')
"${PY[@]}" -m py_compile "${py[@]}"
info "python: ${#py[@]} files compile"

if have helm; then
    for chart in server loadgen kafka; do
        helm lint --strict "deploy/helm/${chart}" >/dev/null || helm lint --strict "deploy/helm/${chart}"
    done
    info "helm lint: server, loadgen, kafka (deps and umbrella need their dependencies: chart stage)"
fi
