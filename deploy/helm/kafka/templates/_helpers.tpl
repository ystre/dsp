{{- define "kafka.labels" -}}
app.kubernetes.io/name: {{ .Chart.Name }}
app.kubernetes.io/instance: {{ .Release.Name }}
app.kubernetes.io/component: kafka
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
helm.sh/chart: {{ printf "%s-%s" .Chart.Name .Chart.Version }}
{{- end -}}

{{/*
Strimzi pod template of every pod of the cluster (brokers and controllers,
entity operator, Kafka exporter): labels, annotations and placement. Strimzi
has no nodeSelector field: the nodeSelector becomes required node affinity,
added to every term of .Values.affinity's (AND, as in a pod's nodeSelector).
Empty when nothing is set.
*/}}
{{- define "kafka.podTemplate" -}}
{{- $v := .Values -}}
{{- $affinity := deepCopy ($v.affinity | default dict) -}}
{{- if $v.nodeSelector -}}
{{- $exprs := list -}}
{{- range $key, $value := $v.nodeSelector -}}
{{- $exprs = append $exprs (dict "key" $key "operator" "In" "values" (list (toString $value))) -}}
{{- end -}}
{{- $nodeAffinity := get $affinity "nodeAffinity" | default dict -}}
{{- $required := get $nodeAffinity "requiredDuringSchedulingIgnoredDuringExecution" | default dict -}}
{{- $terms := list -}}
{{- range (get $required "nodeSelectorTerms" | default (list dict)) -}}
{{- $terms = append $terms (set (deepCopy .) "matchExpressions" (concat (get . "matchExpressions" | default list) $exprs)) -}}
{{- end -}}
{{- $_ := set $required "nodeSelectorTerms" $terms -}}
{{- $_ = set $nodeAffinity "requiredDuringSchedulingIgnoredDuringExecution" $required -}}
{{- $_ = set $affinity "nodeAffinity" $nodeAffinity -}}
{{- end -}}
{{- if or $v.podLabels $v.podAnnotations }}
metadata:
  {{- with $v.podLabels }}
  labels:
    {{- toYaml . | nindent 4 }}
  {{- end }}
  {{- with $v.podAnnotations }}
  annotations:
    {{- toYaml . | nindent 4 }}
  {{- end }}
{{- end }}
{{- with $affinity }}
affinity:
  {{- toYaml . | nindent 2 }}
{{- end }}
{{- with $v.tolerations }}
tolerations:
  {{- toYaml . | nindent 2 }}
{{- end }}
{{- with $v.topologySpreadConstraints }}
topologySpreadConstraints:
  {{- toYaml . | nindent 2 }}
{{- end }}
{{- end -}}

{{/*
Target labels of every PodMonitor, as in Strimzi's examples: the Strimzi
dashboards filter on strimzi_io_cluster, strimzi_io_name and
kubernetes_pod_name.
*/}}
{{- define "kafka.relabelings" -}}
- action: labelmap
  regex: __meta_kubernetes_pod_label_(strimzi_io_.+)
- sourceLabels: [__meta_kubernetes_pod_name]
  targetLabel: kubernetes_pod_name
- sourceLabels: [__meta_kubernetes_pod_node_name]
  targetLabel: node_name
- sourceLabels: [__meta_kubernetes_pod_host_ip]
  targetLabel: node_ip
{{- end -}}
