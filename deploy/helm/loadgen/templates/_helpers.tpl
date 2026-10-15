{{- define "loadgen.fullname" -}}
{{- if .Values.fullnameOverride -}}
{{- .Values.fullnameOverride | trunc 63 | trimSuffix "-" -}}
{{- else -}}
{{- printf "%s-%s" .Release.Name (default "loadgen" .Values.nameOverride) | trunc 63 | trimSuffix "-" -}}
{{- end -}}
{{- end -}}

{{- define "loadgen.selectorLabels" -}}
app.kubernetes.io/name: {{ .Chart.Name }}
app.kubernetes.io/instance: {{ .Release.Name }}
app.kubernetes.io/component: loadgen
{{- end -}}

{{- define "loadgen.labels" -}}
{{ include "loadgen.selectorLabels" . }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
helm.sh/chart: {{ printf "%s-%s" .Chart.Name .Chart.Version }}
{{- end -}}

{{- define "loadgen.image" -}}
{{- $repo := default .Values.image.repository .Values.global.image.repository -}}
{{- $tag := default (default .Chart.AppVersion .Values.image.tag) .Values.global.image.tag -}}
{{- printf "%s:%s" $repo $tag -}}
{{- end -}}

{{/* A CLI argument value: whole numbers without exponent (YAML numbers arrive as float64). */}}
{{- define "loadgen.arg" -}}
{{- if and (kindIs "float64" .) (eq . (floor .)) -}}
{{- int64 . -}}
{{- else -}}
{{- toString . -}}
{{- end -}}
{{- end -}}
