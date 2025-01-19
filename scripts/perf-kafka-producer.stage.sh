BUILD_TYPE=Release
BUILD_DIR="${G_PROJECT_DIR}/build/${BUILD_TYPE}"

function stage-entry() {
    msg "Creating \`perf-test\` topic..."
    docker exec kafka /opt/kafka/bin/kafka-topics.sh --bootstrap-server localhost:9092 --topic perf-test --create --partitions 1 --config retention.ms=600000

    "${BUILD_DIR,,}/src/tools/kafka-client" produce --broker localhost:9092 --topic dev-test --count 10000000 --size 200
    msg "Testing has finished"

    docker exec kafka /opt/kafka/bin/kafka-topics.sh --bootstrap-server localhost:9092 --topic perf-test --delete
    msg "\`perf-test\` topic has been deleted"
}
