function stage-entry() {
    for type in {Debug,Release}; do
        ctest --output-on-failure --test-dir "${G_BUILD_DIR}/${type,,}"
    done
}
