function service-ip() {
    local ret=""
    while [[ -z "${ret}" ]]; do
        ret=$(kubectl get svc svc-dsp-svc -o jsonpath="{.status.loadBalancer.ingress[0].ip}")
        msg "Waiting for IP address..."
        sleep 0.5
    done
    echo "${ret}"
}

function stage-entry() {
    local image_path="${G_ARTIFACT_DIR}/svc.image.tar"
    local namespace="dsp"

    msg "Saving image to: \`${image_path}\`"
    docker save local/svc:latest > "${image_path}"

    msg "Loading image to k3s"
    sudo k3s ctr images import "${image_path}"

    if [[ $(kubectl get namespaces | grep -q "${namespace}") ]]; then
        kubectl create namespace "${namespace}"
    else
        msg "Namespace \`${namespace}\` already exists"
    fi

    helm upgrade --install svc "${G_PROJECT_DIR}/helm/dsp-svc"

    local ip=$(service-ip)
    local port=$(kubectl get svc svc-dsp-svc -o jsonpath="{.spec.ports[0].nodePort}")
    sleep 0.5

    msg "Sending data to ${ip}:${port}"
    xxd -r -p <<< "0008000100626c61" | nc -v -N "${ip}" "${port}"
}
