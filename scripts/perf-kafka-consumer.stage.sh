BUILD_TYPE=Release
BUILD_DIR="${G_PROJECT_DIR}/build/${BUILD_TYPE}"
LOG_CLIENT="${G_ARTIFACT_DIR}/kafka-consumer-perf.log"
REPORT_PATH="${G_REPORT_DIR}/kafka-consumer-perf.txt"

function _random-string() {
    local LENGTH="$1"

    if [[ -z $LENGTH ]]; then
        echo -e "Usage: random-string LENGTH [PATTERN]\n\nPattern is given to 'tr'"
    else
        cat /dev/urandom | base64 | tr -dc "0-9a-zA-Z" | head -c"${LENGTH}"
    fi
}

function load-data() {
    local broker="${1}"
    local topic_name="${2}"

    topic=$(docker exec kafka /opt/kafka/bin/kafka-topics.sh --bootstrap-server "${broker}" --list | grep "${topic_name}")
    if [[ -z "${topic}" ]]; then
        msg "Creating \`${topic_name}\` topic..."
        docker exec kafka /opt/kafka/bin/kafka-topics.sh --bootstrap-server "${broker}" --topic "${topic_name}" --create --partitions 1 --config retention.ms=86400000

        msg "Loading data..."
        "${BUILD_DIR,,}/src/tools/kafka-client" produce \
            --broker "${broker}" \
            --topic "${topic_name}" \
            --count 10000000 \
            --size 200
    else
        msg "Topic already exists (${topic}), skipping loading data."
    fi
}

function stage-entry() {
    local broker="localhost:9092"
    local topic_name="perf-test"

    group_id="perf-consumer-"
    group_id+="$(_random-string 16)"
    msg "Group id: ${group_id}"

    load-data "${broker}" "${topic_name}"

    "${BUILD_DIR,,}/src/tools/kafka-client" consume \
        --broker "${broker}" \
        --topic "${topic_name}" \
        --group-id "${group_id}" \
        --count 20000000 \
        --batch-size 1000 \
        --exit-eof true \
        2>&1 | tee "${LOG_CLIENT}"

    msg "Testing has finished"

    msg "Speed test:"
    grep --color=never --only-matching "Summary:.*" "${LOG_CLIENT}" > "${REPORT_PATH}"

    msg "Report has been saved to: ${REPORT_PATH}"
}
