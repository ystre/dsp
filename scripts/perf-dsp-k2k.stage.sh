BUILD_TYPE=Release
BUILD_DIR="${G_PROJECT_DIR}/build/${BUILD_TYPE}"
LOG_DIR="${G_ARTIFACT_DIR}/perf-tests/dsp-k2k"
LOG_SVC="${LOG_DIR}/svc.log"
LOG_SIM="${LOG_DIR}/sim.log"
REPORT_PATH="${G_REPORT_DIR}/dsp-perf-k2k.txt"

function stage-entry() {
    local dsp_config_src="${G_PROJECT_DIR}/res/dsp-k2k.yaml"
    local tmp_config="$(mktemp)"

    # TODO: Ports from configuration.
    local broker="localhost:9092"
    local metrics_address="localhost:9555/metrics"
    local topic_name_input="$(yq -r '.dsp.interfaces.southbound.topics[0]' "${dsp_config_src}")"
    local topic_name_output="$(yq -r '.app.topic' "${dsp_config_src}")"

    mkdir --parent "${LOG_DIR}"
    rm --force "${LOG_SVC}" "${LOG_SIM}" "${REPORT_PATH}"       # TODO(design): Global vs. local variables.

    yq --yaml-output '.dsp.interfaces.northbound.enabled=true' "${dsp_config_src}" > "${tmp_config}"
    msg "Configuration is saved to: \`${tmp_config}\`"

    "${BUILD_DIR,,}/src/tools/kafka-client" produce --broker "${broker}" --topic "${topic_name_input}" --count 5M --size 200
    2>&1 DSP_CONFIG="${tmp_config}" "${BUILD_DIR,,}/src/svc/svc" | tee "${LOG_SVC}"

    metrics=$(curl --silent "${metrics_address}" | grep --invert-match -E '^#')

    pkill --signal SIGINT --exact svc
    msg "Testing has finished"

    sleep 2
    msg "Test results"

    msg "Speed test:"
    grep --color=never --only-matching "Summary:.*" "${LOG_SVC}" | tee --append "${REPORT_PATH}"

    # TODO: Process is no longer running at this point.
    # msg "Metrics:"
    # echo "${metrics}" | tee --append "${REPORT_PATH}"

    docker exec kafka /opt/kafka/bin/kafka-topics.sh --bootstrap-server "${broker}" --topic "${topic_name_input}" --delete
    docker exec kafka /opt/kafka/bin/kafka-topics.sh --bootstrap-server "${broker}" --topic "${topic_name_output}" --delete

    msg "Report has been saved to: ${REPORT_PATH}"
}
