function stage-entry() {
    "${G_SCRIPT_DIR}/perf-report.py" "${G_REPORT_DIR}" | tee "${G_REPORT_DIR}/perf-report.json"

    echo
    msg "Dumping hardware info..."
    inxi --expanded --filter --extra | tee --append "${G_REPORT_DIR}/perf-report-hardware.txt"
}
