BUILD_TYPE=Release
BUILD_DIR="${G_PROJECT_DIR}/build/${BUILD_TYPE}"
LOG_DIR="${G_ARTIFACT_DIR}/perf-tests/dsp"
LOG_SVC="${LOG_DIR}/svc.log"
LOG_SIM="${LOG_DIR}/sim.log"
REPORT_PATH="${G_REPORT_DIR}/dsp-perf.txt"

trap on-exit exit

function on-exit() {
    if [[ $? -ne 0 ]]; then
        msg "Test failed"
        msg "Dumping logs: ${LOG_SVC}"
        cat "${LOG_SVC}"
        msg "Dumping logs: ${LOG_SIM}"
        cat "${LOG_SIM}"
    fi
}

function stage-entry() {
    local dsp_config_src="${G_PROJECT_DIR}/res/dsp.yaml"
    local tmp_config="$(mktemp)"

    # TODO: Ports from configuration.
    local broker="localhost:9092"
    local service_address="localhost:7200"
    local metrics_address="localhost:9555/metrics"
    local topic_name="$(yq -r '.app.topic' "${dsp_config_src}")"

    mkdir --parent "${LOG_DIR}"
    rm --force "${LOG_SVC}" "${LOG_SIM}" "${REPORT_PATH}"       # TODO(design): Global vs. local variables.

    yq --yaml-output '.dsp.interfaces.northbound.enabled=true' "${dsp_config_src}" > "${tmp_config}"
    msg "Configuration is saved to: \`${tmp_config}\`"

    DSP_CONFIG="${tmp_config}" "${BUILD_DIR,,}/src/svc/svc" > "${LOG_SVC}" 2>&1&
    sleep 1     # TODO(feat): Reconnect support in simulator.
    "${BUILD_DIR,,}/src/tools/tcp-client" --address "${service_address}" --count 10000000 --size 200 --batch 10

    metrics=$(curl --silent "${metrics_address}" | grep --invert-match -E '^#')

    pkill --signal SIGINT --exact svc
    msg "Testing has finished"

    sleep 2
    msg "Test results"

    msg "Speed test:"
    grep --color=never --only-matching "Summary:.*" "${LOG_SVC}" | tee --append "${REPORT_PATH}"

    msg "Metrics:"
    echo "${metrics}" | tee --append "${REPORT_PATH}"

    docker exec kafka /opt/kafka/bin/kafka-topics.sh --bootstrap-server "${broker}" --topic "${topic_name}" --delete

    msg "Report has been saved to: ${REPORT_PATH}"
}
