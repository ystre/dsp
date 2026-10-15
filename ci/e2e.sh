#!/usr/bin/env bash
#
# The whole pipeline, end to end, on a clean copy of the repository (baldr-cpp
# convention). CI-agnostic: any runner (or a laptop) calls this script, or
# ci/run.sh for a subset of the stages.
#
#   CI_RUN_ID   default: the UTC start time (<yyyymmdd>-<hhmmss>Z); names the
#               workspace and the run's Kafka topics (ci/deploy-test.sh)
#   WORKSPACE   default /tmp/workspace/<project>/<run id>: one directory per
#               run, so results of several runs co-exist (tools/workspace-usage.sh)
#   Other knobs: see the stage scripts (CI_PERF_GATE, CI_DEPLOY, IMAGE_PUSH, ...).

set -euo pipefail

CI_DIR="$(dirname "$(realpath "$0")")"
# shellcheck source=../project.env
source "${CI_DIR}/../project.env"

export WORKSPACE_ROOT="${WORKSPACE_ROOT:-/tmp/workspace/${PROJECT_NAME}}"
# UTC, so runs on the host and in the build container sort together.
export CI_RUN_ID="${CI_RUN_ID:-$(date -u +%Y%m%d-%H%M%SZ)}"
export WORKSPACE="${WORKSPACE:-${WORKSPACE_ROOT}/${CI_RUN_ID}}"

STAGES=(
    pre-checks
    build
    unit-test
    function-test
    build-release
    micro-bench
    perf-smoke
    image
    chart
    dashboard
    docs
    package
    package-test
    deploy-test
    summary
)

start=$(date +%s)
"${CI_DIR}/prepare.sh"

# The remaining stages run from the clean copy's own scripts.
for stage in "${STAGES[@]}"; do
    "${WORKSPACE}/ci/${stage}.sh"
done

"${WORKSPACE}/ci/tag-release.sh" --check || true

printf '\n\033[1;32mPipeline passed in %ss (workspace %s)\033[0m\n' "$(( $(date +%s) - start ))" "${WORKSPACE}"
