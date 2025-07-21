FROM debian:testing

ENV DEBIAN_FRONTEND=noninteractive

# Install gcc-14, g++-14, cmake, build tools, and Protobuf
RUN apt-get update && \
    apt-get install -y net-tools cmake build-essential curl wget git protobuf-compiler libprotobuf-dev gdb && \
    apt-get install -y gcc-14 g++-14 && \
    update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100 && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

# Create vscode user if not exists and set ownership
RUN useradd -m vscode

# give sudo permissions
RUN apt-get update && apt-get install -y sudo && rm -rf /var/lib/apt/lists/*
RUN echo "vscode ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

USER vscode

WORKDIR /home/vscode/app

# Copy code and build as vscode user
COPY --chown=vscode:vscode . .