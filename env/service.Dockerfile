# syntax=docker/dockerfile:1

FROM archlinux:latest
COPY install /opt/ystre/
WORKDIR /opt/ystre/bin
ENTRYPOINT ["./svc"]
