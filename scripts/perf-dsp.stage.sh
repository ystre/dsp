BUILD_TYPE=Release
BUILD_DIR="${G_PROJECT_DIR}/build/${BUILD_TYPE}"
LOG_DIR=/tmp/dsp/perf-tcp-kafka-producer
LOG_SVC="${LOG_DIR}/dsp-ft-svc.log"
LOG_SIM="${LOG_DIR}/dsp-ft-sim.log"

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
    dsp_config_src="${G_PROJECT_DIR}/res/dsp.yaml"
    tmp_config="$(mktemp)"

    mkdir --parent "${LOG_DIR}"
    rm --force "${LOG_SVC}"
    rm --force "${LOG_SIM}"

    yq --yaml-output '.dsp.interfaces.northbound.enabled=true' "${dsp_config_src}" > "${tmp_config}"
    msg "Configuration is saved to: \`${tmp_config}\`"

    docker compose --profile kafka --file "${G_PROJECT_DIR}/env/docker-compose.yml" up --detach

    DSP_CONFIG="${tmp_config}" "${BUILD_DIR,,}/src/svc/svc" > "${LOG_SVC}" 2>&1&
    "${BUILD_DIR,,}/src/tools/tcp-client" --address localhost:7200 --count 10000000 --size 200 --batch 10

    # TODO: Ports from configuration.
    metrics=$(curl --silent localhost:9555/metrics | ag -v '^#')

    pkill --signal SIGINT --exact svc
    msg "Testing has finished"

    sleep 2
    msg "Test results"

    msg "Speed test:"
    grep --color=never --only-matching "Summary:.*" "${LOG_SVC}"

    msg "Metrics:"
    echo "${metrics}"

    # TODO: Topic name from configuration.
    docker exec kafka /opt/kafka/bin/kafka-topics.sh --bootstrap-server localhost:9092 --topic dev-test --delete
}
