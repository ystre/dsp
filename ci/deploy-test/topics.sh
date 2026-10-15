# shellcheck shell=bash
#
# Kafka topics of the CI deploy test. Sourced by ci/deploy-test.sh (after
# ci/lib.sh), not executed.
#
# The release's topics live on the platform's shared cluster. For one run they
# are named <namespace>.<run id>.<topic> and labelled ci-run=<run id>, so runs
# never share a topic. Retention: deleted after a successful run, kept after a
# failure (for inspection); kept topics are reported by later runs, never
# deleted by them.

# topics_report_kept <namespace>: warns about topics kept by earlier runs.
topics_report_kept() {
    local kept
    kept="$(kubectl get kafkatopic --all-namespaces --selector "owner-namespace=$1" \
        --output custom-columns=RUN:.metadata.labels.ci-run,TOPIC:.spec.topicName --no-headers 2>/dev/null || true)"
    [[ -n "${kept}" ]] || return 0
    warn "Kafka topics kept by earlier failed runs (not deleted):"
    awk '{ printf "    run %-18s %s\n", $1, $2 }' <<< "${kept}" >&2
}

# topics_helm_args <namespace> <run id>: the chart values that name and label the run's topics.
topics_helm_args() {
    printf '%s\n' --set "topics.prefix=$1.$2." --set "topics.labels.ci-run=$2"
}

# topics_finish <exit status> <namespace> <run id>: deletes the run's topics
# after a success; keeps them after a failure. Call after the release is gone
# (the topics outlive it: helm.sh/resource-policy: keep).
topics_finish() {
    local status="$1" selector="owner-namespace=$2,ci-run=$3"
    [[ -n "$(kubectl get kafkatopic --all-namespaces --selector "${selector}" --output name 2>/dev/null)" ]] || return 0
    if (( status == 0 )); then
        kubectl delete kafkatopic --all-namespaces --selector "${selector}" --wait >/dev/null \
            || warn "deleting the Kafka topics of run $3 failed (kubectl get kafkatopic -A -l ${selector})"
    else
        warn "deploy test failed: the Kafka topics of run $3 are kept (kubectl get kafkatopic -A -l ${selector})"
    fi
}
