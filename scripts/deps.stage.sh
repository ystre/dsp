function stage-entry() {
    docker compose --profile kafka --file "${G_PROJECT_DIR}/env/docker-compose.yml" up --detach

    if [[ $(grep -c kafka /etc/hosts) -eq 0 ]]; then
        kafka_ip=$(docker container inspect kafka | jq -r '.[0].NetworkSettings.Networks."dsp-deps_default".IPAddress')
        sudo sh -c 'echo -e "${0}\tkafka" >> /etc/hosts' "${kafka_ip}"
    fi
}
