JOBS=$(($(nproc) / 2))

function build() {
    local build_type="$1"

    baldr --project "${G_PROJECT_DIR}" -b "${build_type}" -t all -j "${JOBS}"
}

function stage-entry() {
    for type in {Debug,Release}; do
        build "${type}"
    done
}
