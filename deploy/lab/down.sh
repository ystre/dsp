#!/usr/bin/env bash
#
# Uninstalls one release (with its dedicated Kafka cluster and its volumes, if
# it has one). The namespace, the registry and the platform are left alone.
#
# The release's Kafka topics outlive it (helm.sh/resource-policy: keep), so
# their data survives a reinstall; --delete-topics deletes them as well.
#
#   down.sh [--namespace <ns>] [--release <name>] [--delete-topics]
#       default <project> (LAB_NAMESPACE, LAB_RELEASE)

source "$(dirname "$(realpath "$0")")/lib.sh"
need helm kubectl

ns="${LAB_NAMESPACE:-${PROJECT_NAME}}"
release="${LAB_RELEASE:-${PROJECT_NAME}}"
delete_topics=0
while (( $# )); do
    case "$1" in
        --namespace)     ns="$2"; shift 2 ;;
        --release)       release="$2"; shift 2 ;;
        --delete-topics) delete_topics=1; shift ;;
        *)               die "unknown argument: $1" ;;
    esac
done

# The topics of the release's current revision, before the release is gone.
topics="$(helm get manifest "${release}" --namespace "${ns}" | kafka_topics)"

log "Uninstall ${release} from ${ns}"
helm uninstall "${release}" --namespace "${ns}" --wait

if [[ -n "${topics}" ]]; then
    if (( delete_topics )); then
        log "Delete the release's Kafka topics"
        kubectl delete --filename - --wait <<< "${topics}"
    else
        echo "Kafka topics kept (--delete-topics removes them):"
        awk '$1 == "topicName:" { print "  " $2 }' <<< "${topics}"
    fi
fi
