#!/usr/bin/env bash
#
# Installs or upgrades one release of the integration chart. The step shared by
# the lab (up.sh, a released package or the source tree) and the CI deploy test
# (ci/deploy-test.sh, the package under test).
#
#   install.sh --chart <dir|.tgz> [--namespace <ns>] [--release <name>]
#              [--image-repository <repo>] [--image-tag <tag>] [-- <helm args>...]
#
# Defaults: namespace and release <project>, image IMAGE_REPOSITORY (default
# localhost:5000/<project>) with the chart's appVersion as tag.
#
# Does not install the platform: every kind the release renders must already be
# served by the cluster (Prometheus Operator, Strimzi operator: platform.sh),
# otherwise it stops before touching anything; so must the Kafka cluster its
# topics go to. Waits for the workloads, a dedicated Kafka cluster and the
# topics to be Ready. A chart directory gets its own subcharts packaged first
# (helm dependency update).

source "$(dirname "$(realpath "$0")")/lib.sh"
need helm kubectl

chart="" ns="${PROJECT_NAME}" release="${PROJECT_NAME}" repo="${IMAGE_REPOSITORY}" tag=""
while (( $# )); do
    case "$1" in
        --chart)            chart="$2"; shift 2 ;;
        --namespace)        ns="$2"; shift 2 ;;
        --release)          release="$2"; shift 2 ;;
        --image-repository) repo="$2"; shift 2 ;;
        --image-tag)        tag="$2"; shift 2 ;;
        --)                 shift; break ;;
        *)                  die "unknown argument: $1 (see the header of $0)" ;;
    esac
done

[[ -n "${chart}" ]] || die "--chart is required"
[[ -e "${chart}" ]] || die "chart not found: ${chart}"

if [[ -d "${chart}" ]]; then
    helm dependency update "${chart}" >/dev/null
fi

tag="${tag:-$(chart_field "${chart}" appVersion)}"
args=(--namespace "${ns}"
      --set "global.image.repository=${repo}" --set "global.image.tag=${tag}"
      --set server.image.pullPolicy=Always
      "$@")

log "Platform check for ${release} in ${ns}"
manifest="$(helm template "${release}" "${chart}" "${args[@]}")"

# "apiVersion kind" of every rendered object, and of every kind the cluster serves.
rendered="$(awk '/^apiVersion:/ { api = $2 } /^kind:/ { print api, $2 }' <<< "${manifest}" | sort -u)"
served="$(kubectl api-resources --no-headers | awk '{ print $(NF - 2), $NF }' | sort -u)"
missing="$(comm -23 <(echo "${rendered}") <(echo "${served}"))"

if [[ -n "${missing}" ]]; then
    printf '  not served by the cluster: %s\n' "${missing//$'\n'/, }" >&2
    die "the platform is missing; install it with deploy/lab/platform.sh (or disable the components that need it)"
fi

echo "every kind the release renders is served ($(wc -l <<< "${rendered}") kinds)"
topics="$(kafka_topics <<< "${manifest}")"

if [[ -n "${topics}" ]] && ! grep -q '^kind: Kafka$' <<< "${manifest}"; then
    # Topics on a cluster outside the release: it must exist.
    cluster="$(awk '$1 == "strimzi.io/cluster:" { print $2; exit }' <<< "${topics}")"
    cluster_ns="$(awk '$1 == "namespace:" { print $2; exit }' <<< "${topics}")"
    kubectl get kafka "${cluster}" --namespace "${cluster_ns}" >/dev/null 2>&1 \
        || die "the topics go to Kafka cluster ${cluster_ns}/${cluster}, which does not exist (deploy/lab/platform.sh installs the shared one)"
    echo "topics go to the Kafka cluster ${cluster_ns}/${cluster}"
fi

log "Release ${release} ($(chart_field "${chart}" name) $(chart_field "${chart}" version), image ${repo}:${tag}) in ${ns}"
helm upgrade --install "${release}" "${chart}" "${args[@]}" --create-namespace \
    --wait --timeout "${INSTALL_TIMEOUT:-10m}"

if grep -q '^kind: Kafka$' <<< "${manifest}"; then
    log "Dedicated Kafka in ${ns}"
    kubectl wait kafka --all --namespace "${ns}" --for=condition=Ready --timeout "${INSTALL_TIMEOUT:-10m}"
fi

if [[ -n "${topics}" ]]; then
    log "Kafka topics"
    kubectl wait --filename - --for=condition=Ready --timeout "${INSTALL_TIMEOUT:-10m}" <<< "${topics}"
fi
