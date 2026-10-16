# Runtime images: one per binary, built from the targets below
# (`docker build --target server|loadgen`; ci/image.sh). The Helm charts'
# `binaries` values name the executable each one runs.
#
# The binaries are built by CI (ci/build-release.sh) and staged by ci/image.sh
# under bin/<target>/: everything except glibc is linked statically
# (libstdc++, libgcc), and they need the glibc of the build host (>= 2.38),
# which Debian 13 based distroless provides.
FROM gcr.io/distroless/cc-debian13:nonroot AS base
USER 65532:65532

FROM base AS server
COPY bin/server/ /usr/local/bin/
# tcp, udp, ws, admin.
EXPOSE 7200/tcp 7201/udp 7280/tcp 9555/tcp

FROM base AS loadgen
COPY bin/loadgen/ /usr/local/bin/
# Metrics.
EXPOSE 9556/tcp
