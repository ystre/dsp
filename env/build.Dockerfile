# Build and CI image: everything ci/e2e.sh needs, so the pipeline runs the same
# on a laptop and on any CI runner (ci/in-container.sh).
#
# GCC 16 is not packaged by Debian/Ubuntu yet; Arch Linux ships it.
FROM archlinux:base-devel

RUN pacman -Syu --noconfirm --needed \
        cmake ninja git python python-pipx python-yaml \
        curl unzip openssl jq yq \
        clang shellcheck asciidoctor ruby-rouge \
        helm kubectl docker \
    && pacman -Scc --noconfirm

ARG USERNAME=builder
ARG USER_UID=1000
ARG USER_GID=$USER_UID

RUN groupadd --gid $USER_GID $USERNAME \
    && useradd --uid $USER_UID --gid $USER_GID -m -s /bin/bash $USERNAME

USER $USER_UID
ENV PATH=/home/$USERNAME/.local/bin:$PATH
ENV CONAN_HOME=/home/$USERNAME/.conan2
RUN pipx install conan
# Mount point of the persistent package cache (ci/in-container.sh). Created here so that a new
# Docker volume inherits the builder's ownership.
RUN mkdir -p $CONAN_HOME/p

COPY --chown=$USER_UID:$USER_GID conan-profile $CONAN_HOME/profiles/default

WORKDIR /workspace
