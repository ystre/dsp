BUILD_TYPE=Release
BUILD_DIR="${G_PROJECT_DIR}/build/${BUILD_TYPE}"
LOG_CLIENT="${G_ARTIFACT_DIR}/kafka-producer.log"
REPORT_PATH="${G_REPORT_DIR}/kafka-producer-perf.txt"

function stage-entry() {
    local broker="localhost:9092"
    local topic_name="perf-test"

    msg "Creating \`perf-test\` topic..."
    docker exec kafka /opt/kafka/bin/kafka-topics.sh --bootstrap-server "${broker}" --topic "${topic_name}" --create --partitions 1 --config retention.ms=600000

    "${BUILD_DIR,,}/src/tools/kafka-client" produce \
        --broker "${broker}" \
        --topic "${topic_name}" \
        --count 10000000 \
        --size 200 \
        | tee "${LOG_CLIENT}"

    msg "Testing has finished"

    msg "Speed test:"
    grep --color=never --only-matching "Summary:.*" "${LOG_CLIENT}" > "${REPORT_PATH}"

    docker exec kafka /opt/kafka/bin/kafka-topics.sh --bootstrap-server "${broker}" --topic "${topic_name}" --delete
    msg "\`perf-test\` topic has been deleted"

    msg "Report has been saved to: ${REPORT_PATH}"
}
