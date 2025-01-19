BUILD_TYPE=Release
BUILD_DIR="${G_PROJECT_DIR}/build/${BUILD_TYPE}"

TEST_STATUS=0

function check() {
    local actual="${1}"
    local expected="${2}"
    local description="${3:-""}"

    if [[ "${actual}" == "${expected}" ]]; then
        msg "Check success: ${COLOR_GREEN}${description}${COLOR_DEF}"
    else
        msg "Check failed: ${COLOR_RED}${description}${COLOR_DEF} (actual: \`${actual}\` | expected: \`${expected})\`"
        TEST_STATUS=1
    fi
}

function stage-entry() {
    local topic_name="ft-test"
    "${BUILD_DIR,,}/src/tools/kafka-client" produce --broker localhost:9092 --topic "${topic_name}" --count 1 --size 200
    messages=$(kcat -b localhost:9092 -C -t "${topic_name}" -J -c 1)

    check "$(jq -r '.key' <<< "${messages}")" "null" "Key"
    check "$(jq -r '.payload' <<< "${messages}" | wc -c)" 201 "Payload length"   # +1 because of newline (?)
    check "$(jq -r '.headers[0]' <<< "${messages}")" "ts" "Header key"

    docker exec kafka /opt/kafka/bin/kafka-topics.sh --bootstrap-server localhost:9092 --topic "${topic_name}" --delete
    msg "\`${topic_name}\` topic has been deleted"

    return ${TEST_STATUS}
}
