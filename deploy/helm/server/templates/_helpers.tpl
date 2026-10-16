{{/* Resource base name: "<release>-server" unless overridden. */}}
{{- define "server.fullname" -}}
{{- if .Values.fullnameOverride -}}
{{- .Values.fullnameOverride | trunc 63 | trimSuffix "-" -}}
{{- else -}}
{{- printf "%s-%s" .Release.Name (default "server" .Values.nameOverride) | trunc 63 | trimSuffix "-" -}}
{{- end -}}
{{- end -}}

{{- define "server.selectorLabels" -}}
app.kubernetes.io/name: {{ .Chart.Name }}
app.kubernetes.io/instance: {{ .Release.Name }}
app.kubernetes.io/component: server
{{- end -}}

{{- define "server.labels" -}}
{{ include "server.selectorLabels" . }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
helm.sh/chart: {{ printf "%s-%s" .Chart.Name .Chart.Version }}
{{- end -}}

{{/* "<registry>/<repository>:<tag>" of an image value (dict "image" <value> "root" .); global.image overrides. */}}
{{- define "server.imageRef" -}}
{{- $global := default dict .root.Values.global.image -}}
{{- $registry := default .image.registry $global.registry -}}
{{- $tag := default (default .root.Chart.AppVersion .image.tag) $global.tag -}}
{{- if $registry -}}
{{- printf "%s/%s:%s" $registry .image.repository $tag -}}
{{- else -}}
{{- printf "%s:%s" .image.repository $tag -}}
{{- end -}}
{{- end -}}

{{- define "server.image" -}}
{{- include "server.imageRef" (dict "image" .Values.image "root" .) -}}
{{- end -}}

{{- define "server.testImage" -}}
{{- include "server.imageRef" (dict "image" .Values.tests.image "root" .) -}}
{{- end -}}

{{- define "server.serviceAccountName" -}}
{{- if .Values.serviceAccount.create -}}
{{- default (include "server.fullname" .) .Values.serviceAccount.name -}}
{{- else -}}
{{- default "default" .Values.serviceAccount.name -}}
{{- end -}}
{{- end -}}

{{/* Container resources; Guaranteed QoS copies the requests into the limits. */}}
{{- define "server.resources" -}}
{{- $res := deepCopy .Values.resources -}}
{{- if .Values.performance.guaranteedQoS -}}
{{- $_ := set $res "limits" (merge (default dict $res.limits) $res.requests) -}}
{{- $_ := set $res.limits "cpu" $res.requests.cpu -}}
{{- $_ := set $res.limits "memory" (default $res.requests.memory $res.limits.memory) -}}
{{- $_ := set $res.requests "memory" $res.limits.memory -}}
{{- end -}}
{{- if .Values.performance.hugepages2Mi -}}
{{- $_ := set (default dict $res.requests) "hugepages-2Mi" .Values.performance.hugepages2Mi -}}
{{- $_ := set $res.limits "hugepages-2Mi" .Values.performance.hugepages2Mi -}}
{{- end -}}
{{- toYaml $res -}}
{{- end -}}
