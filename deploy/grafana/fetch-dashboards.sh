#!/usr/bin/env bash
#
# Vendors the platform's third-party Grafana dashboards into
# deploy/helm/deps/dashboards/<folder>/ (the dependencies chart ships each as a
# ConfigMap for the Grafana sidecar; <folder> becomes the Grafana folder), with
# their licences, so that installs work offline and every change is reviewed.
#
# Usage: deploy/grafana/fetch-dashboards.sh   (downloads into a new build/fetch-dashboards.*)
#
# Every file is pinned by version and SHA-256. Bumping: change the version
# below, run this, review the diff of the dashboards (metric names, the labels
# they filter on: doc/deployment.adoc "Dependencies chart"), then replace the
# checksums with the ones it reports.
#   * STRIMZI_VERSION must equal the strimzi-kafka-operator chart version of
#     the dependencies chart: the Kafka and KRaft dashboards are those for the
#     Strimzi Metrics Reporter (the Kafka chart's metricsConfig), the exporter
#     and operator ones those of the same release.
#   * NODE_EXPORTER_FULL_REVISION: grafana.com dashboard 1860 (rfmoz).

set -euo pipefail

STRIMZI_VERSION=1.2.0
NODE_EXPORTER_FULL_REVISION=45
RFMOZ_COMMIT=99a25cc154c21c4fcbae1aa24352bf1b6764d847

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
CHART="$(realpath "${SCRIPT_DIR}/../helm/deps")"
DEST="${CHART}/dashboards"
STRIMZI="https://raw.githubusercontent.com/strimzi/strimzi-kafka-operator/${STRIMZI_VERSION}"
METRICS="${STRIMZI}/packaging/examples/metrics"

chart_strimzi="$(awk '/name: strimzi-kafka-operator/ { found = 1 } found && /version:/ { print $2; exit }' "${CHART}/Chart.yaml")"
[[ "${chart_strimzi}" == "${STRIMZI_VERSION}" ]] \
    || { echo "STRIMZI_VERSION ${STRIMZI_VERSION} differs from the chart's strimzi-kafka-operator ${chart_strimzi}" >&2; exit 1; }

# <destination> <url> <sha256>
FILES=(
    "kafka/strimzi-kafka.json           ${METRICS}/strimzi-metrics-reporter/grafana-dashboards/strimzi-kafka.json  dbcfe97215fcd43fe177735f59578ec09ea00e15c14bee4c60e392bdbf535cca"
    "kafka/strimzi-kraft.json           ${METRICS}/strimzi-metrics-reporter/grafana-dashboards/strimzi-kraft.json  de020adb83586096ede522cc046fc81f274703efb098a866f83f1c8d29fc30bb"
    "kafka/strimzi-kafka-exporter.json  ${METRICS}/grafana-dashboards/strimzi-kafka-exporter.json                  92c0f574461a4aa38d80c5b800392f1d5f720df92747d91ff916ff2ec7c45431"
    "kafka/strimzi-operators.json       ${METRICS}/grafana-dashboards/strimzi-operators.json                       442ab5c6de8c60e55663b1c6831c4f706eac859e78640d38bc59aaed5ab256ba"
    "nodes/node-exporter-full.json      https://grafana.com/api/dashboards/1860/revisions/${NODE_EXPORTER_FULL_REVISION}/download  184c6b7409f306da75525d7772f71945b10cea23ad16b5d78c4698ea0ea51986"
)
# Apache-2.0 both: redistribution carries the licence.
LICENCES=(
    "LICENCES/strimzi.txt  ${STRIMZI}/LICENSE"
    "LICENCES/rfmoz.txt    https://raw.githubusercontent.com/rfmoz/grafana-dashboards/${RFMOZ_COMMIT}/LICENSE"
)

mkdir -p "${SCRIPT_DIR}/../../build"
staging="$(mktemp -d "${SCRIPT_DIR}/../../build/fetch-dashboards.XXXXXX")"

fetch() {
    mkdir -p "$(dirname "${staging}/$1")"
    echo "Fetching $2"
    curl --fail --silent --show-error --location --proto =https -o "${staging}/$1" "$2"
}

failed=0
for entry in "${FILES[@]}"; do
    read -r file url sum <<< "${entry}"
    fetch "${file}" "${url}"
    actual="$(sha256sum "${staging}/${file}" | cut -d' ' -f1)"
    if [[ "${actual}" != "${sum}" ]]; then
        echo "  checksum mismatch for ${file}: pinned ${sum}, got ${actual}" >&2
        failed=1
    fi
    python3 -I -c 'import json, sys; d = json.load(open(sys.argv[1])); assert d.get("title") and d.get("uid"), "no title/uid"' \
        "${staging}/${file}" || { echo "  ${file} is not a dashboard" >&2; failed=1; }
done
for entry in "${LICENCES[@]}"; do
    read -r file url <<< "${entry}"
    fetch "${file}" "${url}"
done
(( ! failed )) || { echo "nothing replaced; review the new files, then update the pins" >&2; exit 1; }

# All files checked: replace them together, never a mix of versions.
for entry in "${FILES[@]}" "${LICENCES[@]}"; do
    read -r file _ <<< "${entry}"
    mkdir -p "$(dirname "${DEST}/${file}")"
    mv "${staging}/${file}" "${DEST}/${file}"
done
echo "Vendored the dashboards (Strimzi ${STRIMZI_VERSION}, Node Exporter Full r${NODE_EXPORTER_FULL_REVISION}) into ${DEST}"
