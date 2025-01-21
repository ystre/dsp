function stage-entry() {
    docker compose --profile kafka --file "${G_PROJECT_DIR}/env/docker-compose.yml" up --detach
}
