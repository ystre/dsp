TMP_DIR="${G_PROJECT_DIR}/.tmp"

function stage-entry() {
    mkdir --parent "${TMP_DIR}" && cd "${TMP_DIR}" || return
    "${G_SCRIPT_DIR}/gen-ssl-certs.sh" ca dev-ca dev
    "${G_SCRIPT_DIR}/gen-ssl-certs.sh" -k server dev-ca dev- kafka
}
