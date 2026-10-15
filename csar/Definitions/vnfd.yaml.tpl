tosca_definitions_version: tosca_simple_yaml_1_3

description: >-
  ${PROJECT_NAME} ${VERSION}: high-performance network server (TCP; UDP and
  WebSocket ready) as a containerized VNF. One deployment flavour, deployed
  through one Helm chart (MCIOP) with the server, its Kafka topics, an
  optional dedicated Kafka cluster and the load generator. The cluster must
  provide the Prometheus Operator, the Strimzi operator and a Kafka cluster
  for the topics (the project's dependencies chart, not part of this
  package).

metadata:
  template_name: ${PROJECT_NAME}
  template_author: ${VENDOR}
  template_version: ${VERSION}

imports:
  - etsi_nfv_sol001_common_types.yaml
  - etsi_nfv_sol001_vnfd_types.yaml

node_types:
  ${VENDOR}.${PROJECT_NAME}.VNF:
    derived_from: tosca.nodes.nfv.VNF
    properties:
      descriptor_id:
        type: string
        constraints: [ valid_values: [ ${DESCRIPTOR_ID} ] ]
        default: ${DESCRIPTOR_ID}
      descriptor_version:
        type: string
        constraints: [ valid_values: [ "${VERSION}" ] ]
        default: "${VERSION}"
      provider:
        type: string
        constraints: [ valid_values: [ ${VENDOR} ] ]
        default: ${VENDOR}
      product_name:
        type: string
        constraints: [ valid_values: [ ${PROJECT_NAME} ] ]
        default: ${PROJECT_NAME}
      software_version:
        type: string
        constraints: [ valid_values: [ "${VERSION}" ] ]
        default: "${VERSION}"
      product_info_name:
        type: string
        default: ${PROJECT_NAME} network server
      product_info_description:
        type: string
        default: Thread-per-core network server with pluggable handlers (doc/architecture.adoc)
      vnfm_info:
        type: list
        entry_schema:
          type: string
        default: [ "${VNFM_INFO}" ]
      flavour_id:
        type: string
        constraints: [ valid_values: [ default ] ]
        default: default
      flavour_description:
        type: string
        default: ""
    interfaces:
      Vnflcm:
        type: tosca.interfaces.nfv.Vnflcm

topology_template:
  substitution_mappings:
    node_type: ${VENDOR}.${PROJECT_NAME}.VNF
    properties:
      flavour_id: default

  node_templates:
    vnf:
      type: ${VENDOR}.${PROJECT_NAME}.VNF
      properties:
        flavour_description: >-
          Single flavour. Capacity scales with the server Deployment's replicas and
          the reactors per pod (CPU requests); see the Helm values.

    mciop:
      type: tosca.nodes.nfv.Mciop
      requirements:
        - associatedVdu: server
      artifacts:
        helm_chart:
          type: tosca.artifacts.nfv.HelmChart
          description: Integration chart (server, dependencies, load generator), subcharts vendored.
          file: Artifacts/Deployment/HELM/${HELM_CHART}

    server:
      type: tosca.nodes.nfv.Vdu.OsContainerDeployableUnit
      properties:
        name: server
        description: Server pods (one reactor per requested CPU in Phase 2).
        vdu_profile:
          min_number_of_instances: 1
          max_number_of_instances: 8
      requirements:
        - container: server_container

    server_container:
      type: tosca.nodes.nfv.Vdu.OsContainer
      properties:
        name: server
        description: The server process; the same image also holds the load generator.
        # Milli-CPUs; Guaranteed QoS with integer CPUs is a Helm value (performance.guaranteedQoS).
        requested_cpu_resources: 1000
        requested_memory_resources: 128 MiB
        memory_resource_limit: 1 GiB
${SW_IMAGE_ARTIFACT}
    data_cp:
      type: tosca.nodes.nfv.VirtualCp
      properties:
        layer_protocols: [ ipv4 ]
        protocol:
          - associated_layer_protocol: ipv4
        address_pool_name_configurable: false
        additionalServiceData:
          - portData:
              - { name: tcp, protocol: tcp, port: 7200, portConfigurable: true }
              - { name: udp, protocol: udp, port: 7201, portConfigurable: true }
              - { name: ws,  protocol: tcp, port: 7280, portConfigurable: true }
      requirements:
        - target: server

    admin_cp:
      type: tosca.nodes.nfv.VirtualCp
      properties:
        layer_protocols: [ ipv4 ]
        protocol:
          - associated_layer_protocol: ipv4
        address_pool_name_configurable: false
        additionalServiceData:
          - portData:
              - { name: admin, protocol: tcp, port: 9555, portConfigurable: true }
            serviceData:
              path: [ /metrics, /healthz, /readyz ]
      requirements:
        - target: server

  policies:
    - instantiation_levels:
        type: tosca.policies.nfv.InstantiationLevels
        properties:
          levels:
            default:
              description: One server pod.
          default_level: default

    - server_instantiation_levels:
        type: tosca.policies.nfv.VduInstantiationLevels
        properties:
          levels:
            default:
              number_of_instances: 1
        targets: [ server ]
