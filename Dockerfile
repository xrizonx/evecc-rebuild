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
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src
RUN test -f vendor/python-cmake-buildsystem/CMakeLists.txt \
    && test -f vendor/uncompyle6/uncompyle6/__init__.py \
    && test -f vendor/uncompyle2/uncompyle2/__init__.py \
    || (echo "Missing git submodules. Clone with --recurse-submodules first." >&2; exit 2)

# - /build: CMake + native object files + cached CPython source archive
# - ccache: GCC/G++ compilation results (fast rebuilds after source edits)
# - /wheel-cache: hash-verified Python 2 wheels; works offline on subsequent runs
RUN --mount=type=cache,id=evecc-linux-build,target=/build,sharing=locked \
    --mount=type=cache,id=evecc-compiler-cache,target=/root/.cache/ccache,sharing=locked \
    --mount=type=cache,id=evecc-py2-wheels,target=/wheel-cache,sharing=locked \
    cmake -S /src -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DEVECC_WHEEL_CACHE=/wheel-cache \
        -DENABLE_SSL=OFF -DENABLE_HASHLIB=OFF \
        -DUSE_SYSTEM_OpenSSL=OFF \
    && cmake --build /build --parallel "${BUILD_JOBS}" \
    && if [ "${RUN_TESTS}" = "1" ]; then \
         python3 /src/tests/native_smoke.py /build/evecc; \
       else \
         echo "WARNING: Integration tests were explicitly disabled."; \
       fi \
    && cmake --build /build --target package \
    && cmake --install /build --prefix /opt/evecc \
    && mkdir -p /artifacts \
    && cp /build/evecc-Linux-*.tar.gz /artifacts/

# Export the native .tar.gz with `docker build --target release-archive
# --output type=local,dest=dist .` without running any hosted CI.
FROM scratch AS release-archive
COPY --from=builder /artifacts/ /

# Small Linux runtime image. No compiler, CMake, pip, Python3, or Wine.
FROM debian:12-slim AS runtime
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates libstdc++6 libgcc-s1 libssl3 zlib1g \
    libbz2-1.0 libffi8 libreadline8 libsqlite3-0 libncurses6 \
    libgdbm6 libgdbm-compat4 liblzma5 libexpat1 libcrypt1 libdb5.3 \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /work \
    && chown 10001:10001 /work

COPY --from=builder /opt/evecc/ /opt/evecc/
ENV HOME=/work
WORKDIR /work
USER 10001:10001
ENTRYPOINT ["/opt/evecc/evecc"]
CMD ["--help"]
