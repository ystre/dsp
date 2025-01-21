BUILD_TYPE=Debug
BUILD_DIR="${G_PROJECT_DIR}/build/${BUILD_TYPE}"
LOG_DIR="${G_ARTIFACT_DIR}/ft-consumer"
LOG_CLIENT="${LOG_DIR}/ft-consumer.log"

TEST_STATUS=0

# TODO(refact): Make a library (it's copy pasta).
function check() {
    local actual="${1}"
    local expected="${2}"
    local description="${3:-""}"

    if [[ "${actual}" == "${expected}" ]]; then
        msg "Check success: ${COLOR_GREEN}${description}${COLOR_DEF}"
    else
        msg "Check failed: ${COLOR_RED}${description}${COLOR_DEF} (actual: \`${actual}\` | expected: \`${expected}\`)"
        TEST_STATUS=1
    fi
}

function stage-entry() {
    local broker="localhost:9092"
    local topic_name="ft-test-consumer"

    mkdir --parent "${LOG_DIR}"
    rm --force "${LOG_CLIENT}"

    docker exec kafka /opt/kafka/bin/kafka-topics.sh --bootstrap-server "${broker}" --topic "${topic_name}" --create --partitions 5

    "${BUILD_DIR,,}/src/tools/kafka-client" consume --broker "${broker}" --topic "${topic_name}" --group-id ft > "${LOG_CLIENT}" 2>&1&

    kcat -b "${broker}" -P -t "${topic_name}" -K, <<< "keyA,hello kafka"
    kcat -b "${broker}" -P -t "${topic_name}" -K, <<< "keyB,hello again"

    sleep 1     # Wait for polling
    pkill --signal SIGINT --exact kafka-client

    result=$(grep "Message consumed: " "${LOG_CLIENT}" | awk -F'  ' '{print $2}')

    check "$(sed --quiet '1p' <<< "${result}")" "key=keyA payload=hello kafka" "First message (key and payload)"
    check "$(sed --quiet '2p' <<< "${result}")" "key=keyB payload=hello again" "Second message (key and payload)"

    docker exec kafka /opt/kafka/bin/kafka-topics.sh --bootstrap-server "${broker}" --topic "${topic_name}" --delete
    msg "\`${topic_name}\` topic has been deleted"

    return ${TEST_STATUS}
}
