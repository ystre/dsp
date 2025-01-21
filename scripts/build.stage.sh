JOBS=$(($(nproc) / 2))

function stage-entry() {
    baldr --project "${G_PROJECT_DIR}" --build-type Debug -DSANITIERS=asan --target all --jobs "${JOBS}"
    baldr --project "${G_PROJECT_DIR}" --build-type Release --target all --jobs "${JOBS}"
}
