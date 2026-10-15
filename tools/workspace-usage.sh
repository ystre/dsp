#!/usr/bin/env bash
#
# Summarises the disk (usually memory: /tmp is a tmpfs) used by the e2e
# workspaces that accumulate under the workspace root, one per run.
# Read-only: it reports, it never deletes.
#
# Usage: tools/workspace-usage.sh [<workspace-root>]   (default /tmp/workspace/<project>)
#
# Per run: size, and the share of build trees, dist/ and results/ in it.
# Totals: runs, overall size, and the filesystem's size/free space, with a
# warning when the workspaces take more than WARN_PERCENT (default 25) of it.

set -euo pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
# shellcheck source=../project.env
source "${SCRIPT_DIR}/../project.env"
ROOT="${1:-/tmp/workspace/${PROJECT_NAME}}"
WARN_PERCENT="${WARN_PERCENT:-25}"

if [[ ! -d "${ROOT}" ]]; then
    echo "No workspaces under ${ROOT}."
    exit 0
fi

kib() { du -sk "$1" 2>/dev/null | cut -f1; }
human() { numfmt --to=iec --suffix=B $(( $1 * 1024 )); }

mapfile -t runs < <(find "${ROOT}" -mindepth 1 -maxdepth 1 -type d | sort)
total=0
printf '\nWorkspaces under %s\n' "${ROOT}"
printf '  %-17s %9s %9s %9s %9s  %s\n' run total build dist results status
for run in "${runs[@]}"; do
    size=$(kib "${run}")
    total=$(( total + size ))
    build=$( [[ -d "${run}/build"   ]] && kib "${run}/build"   || echo 0)
    dist=$(  [[ -d "${run}/dist"    ]] && kib "${run}/dist"    || echo 0)
    res=$(   [[ -d "${run}/results" ]] && kib "${run}/results" || echo 0)
    # A run that reached packaging has its CSAR; otherwise it stopped earlier (failed or running).
    if compgen -G "${run}/dist/*.csar" >/dev/null; then status=packaged; else status=incomplete; fi
    printf '  %-17s %9s %9s %9s %9s  %s\n' "$(basename "${run}")" \
        "$(human "${size}")" "$(human "${build}")" "$(human "${dist}")" "$(human "${res}")" "${status}"
done

read -r fstype fs_size fs_avail < <(df -k --output=fstype,size,avail "${ROOT}" | tail -1)
percent=$(( fs_size > 0 ? total * 100 / fs_size : 0 ))
printf '  %s\n' "-----------------------------------------------------------------"
printf '  %d run(s), %s in total; %s filesystem: %s, %s free (workspaces use %d %%)\n' \
    "${#runs[@]}" "$(human "${total}")" "${fstype}" "$(human "${fs_size}")" "$(human "${fs_avail}")" "${percent}"
if (( percent >= WARN_PERCENT )); then
    printf '  \033[33mwarning: workspaces use %d %% of the %s filesystem%s; remove old runs from %s\033[0m\n' \
        "${percent}" "${fstype}" "$([[ ${fstype} == tmpfs ]] && echo ' (memory)')" "${ROOT}"
fi
