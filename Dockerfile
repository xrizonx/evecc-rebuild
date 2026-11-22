# syntax=docker/dockerfile:1.7
# Native ELF build, not Wine. All compilation and testing happen locally.
# BuildKit cache mounts retain Python build output, ccache and pinned wheels
# between repeated `docker build` calls on the same Docker host.
FROM debian:12-slim AS builder
ARG DEBIAN_FRONTEND=noninteractive
ARG BUILD_JOBS=2
ARG RUN_TESTS=1

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates git build-essential cmake ninja-build ccache pkg-config \
    libssl-dev zlib1g-dev libbz2-dev libffi-dev libreadline-dev \
    libsqlite3-dev libncurses-dev libgdbm-dev libgdbm-compat-dev \
    liblzma-dev libexpat1-dev libcrypt-dev libdb-dev \
    python3 openssl file \
    && rm -rf /var/lib/apt/lists