# Runtime image: the server and the load generator (one image, the command
# selects the binary; see the Helm charts' `binaries` values).
#
# The binaries are built by CI (ci/build-release.sh) and staged by ci/image.sh:
# everything except glibc is linked statically (libstdc++, libgcc), and they
# need the glibc of the build host (>= 2.38), which Debian 13 based distroless
# provides.
FROM gcr.io/distroless/cc-debian13:nonroot

COPY bin/ /usr/local/bin/

USER 65532:65532
# tcp, udp, ws, admin (server) and loadgen metrics.
EXPOSE 7200/tcp 7201/udp 7280/tcp 9555/tcp 9556/tcp
