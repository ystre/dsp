function stage-entry() {
    local stages=(
        build
        unit-test
        cert
        deps
        ft-kafka-producer
        ft-kafka-consumer
        perf-tcp
        perf-kafka-producer
        perf-kafka-consumer
        perf-dsp
        doc
        create-perf-report
    )

    # Loop variables must be declared separately to avoid overwriting other
    # variables with the same name.
    local stage
    for stage in "${stages[@]}"; do
        run-stage "${stage}"
    done
}
