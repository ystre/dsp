BUILD_TYPE=Release
BUILD_DIR="${G_PROJECT_DIR}/build/${BUILD_TYPE}"
LOG_DIR=/tmp
LOG_SVC="${LOG_DIR}/dsp-ft-svc.log"
LOG_SIM="${LOG_DIR}/dsp-ft-sim.log"

export DSP_CONFIG="${G_PROJECT_DIR}/res/dsp.yaml"

function stage-entry() {
    # TODO(feat): Call Docker compose for dependencies.
    # TODO(feat): Support for different `dsp.yaml` configs.

    "${BUILD_DIR,,}/src/svc/svc" > "${LOG_SVC}" 2>&1&
    sleep 1     # TODO(feat): Reconnect support in simulator.
    "${BUILD_DIR,,}/src/svc/sim" > "${LOG_SIM}" 2>&1&
    "${BUILD_DIR,,}/src/tools/tcp-client" --address localhost:7200 --count 20000000 --size 200 --batch 10

    metrics=$(curl --silent localhost:9555/metrics | ag -v '^#')

    pkill --signal SIGINT --exact sim
    pkill --signal SIGINT --exact svc
    msg "Testing has finished"

    sleep 2
    msg "Test results"

    msg "Speed test:"
    ag --nocolor --only-matching "Summary:.*" "${LOG_SVC}"

    msg "Metrics:"
    echo "${metrics}"
}
