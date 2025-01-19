BUILD_TYPE=Release
BUILD_DIR="${G_PROJECT_DIR}/build/${BUILD_TYPE}"
LOG_DIR=/tmp/dsp/perf-tcp
LOG_SVC="${LOG_DIR}/dsp-ft-svc.log"
LOG_SIM="${LOG_DIR}/dsp-ft-sim.log"

export DSP_CONFIG="${G_PROJECT_DIR}/res/dsp.yaml"

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
    mkdir --parent "${LOG_DIR}"
    rm --force "${LOG_SVC}"
    rm --force "${LOG_SIM}"

    "${BUILD_DIR,,}/src/svc/svc" > "${LOG_SVC}" 2>&1&
    sleep 1     # TODO(feat): Reconnect support in simulator.
    "${BUILD_DIR,,}/src/svc/sim" --address localhost:7200 > "${LOG_SIM}" 2>&1&
    "${BUILD_DIR,,}/src/tools/tcp-client" --address localhost:7200 --count 20000000 --size 200 --batch 10

    # TODO: Ports from configuration.
    metrics=$(curl --silent localhost:9555/metrics | ag -v '^#')

    pkill --signal SIGINT --exact sim
    pkill --signal SIGINT --exact svc
    msg "Testing has finished"

    sleep 2
    msg "Test results"

    msg "Speed test:"
    grep --color=never --only-matching "Summary:.*" "${LOG_SVC}"

    msg "Metrics:"
    echo "${metrics}"
}
