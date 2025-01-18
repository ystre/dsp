# syntax=docker/dockerfile:1

FROM archlinux:latest

RUN pacman --sync --refresh --noconfirm \
    binutils \
    cmake \
    debugedit \
    fakeroot \
    gcc \
    git \
    make \
    sudo \
    cargo && \
        cargo install baldr && \
        mv /root/.cargo/bin/baldr /usr/bin/baldr

RUN useradd --create-home builder
RUN echo "builder ALL=(ALL) NOPASSWD: /usr/bin/pacman" >> /etc/sudoers

USER builder
RUN git clone https://aur.archlinux.org/yay-bin.git /tmp/yay && \
    cd /tmp/yay && \
    makepkg --syncdeps

USER root
RUN pacman --upgrade --noconfirm /tmp/yay/*.pkg.tar.zst

USER builder
RUN yay --sync --refresh --noconfirm conan
