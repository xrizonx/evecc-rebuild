# EVECC Docker — native Linux image and portable build

This repository includes a **local multi-stage Docker build** for the
historical EVECC command-line toolkit. The output is a real Linux ELF binary
that runs directly inside Linux containers. No Windows APIs, Wine, Windows
game client, Node.js server or .NET runtime is required for EVECC itself.

> **Status:** The Dockerfile and dependency bundle have been committed, but
> no Docker daemon is available in the development environment to compile
> and verify the finished image. Do not interpret the existence of this
> Dockerfile as proof that the complete build currently passes.
>
> **No GitHub Actions/CI/CD:** Every command below runs on your own machine.

## Step 1 — Install Docker

Install Docker Engine with BuildKit/buildx (and optionally the Compose plugin)
on a 64-bit Linux host. Follow the official
[Docker Engine installation instructions](https://docs.docker.com/engine/install/)
for your distribution.

Check your installation:

```bash
docker version
docker buildx version
docker compose version
```

If the Compose plugin is not installed, you can still use all of the basic
`docker build` and `docker run` commands below.

For Docker Desktop or a Linux host with rootless Docker, confirm that Docker
is running and your account has permission to use it. Do not run the EVECC
game/client itself in this image: that software is not provided here.

## Step 2 — Download the Linux port and its submodules

```bash
git clone --branch linux-native-port --recurse-submodules \
  https://github.com/xrizonx/evecc-rebuild.git
cd evecc-rebuild

git submodule update --init --recursive
```

**Required:** The checkout must contain files beneath
`vendor/python-cmake-buildsystem/`, `vendor/uncompyle2/` and
`vendor/uncompyle6/`. Docker does **not** fetch missing Git submodules
automatically; that would make builds less predictable.

## Step 3 — Build a native Linux image on your computer

```bash
docker build --target runtime \
  --build-arg BUILD_JOBS=2 \
  --build-arg RUN_TESTS=1 \
  -t evecc-native:local .
```

The builder:

1. Uses Debian 12 and installs native compilers, CMake/Ninja, OpenSSL and
   Python 2.7 build prerequisites.
2. Compiles the EVECC ELF executable using C++17 and statically embeds
   the historical CPython 2.7.13 runtime.
3. Downloads the exact pinned Python 2 wheels for `xdis`, `spark-parser`,
   `click` and `six` into an integrity-checked cache, then puts their
   Python source and `uncompyle6` in one ZIP archive.
4. Runs the local synthetic integration tests **by default**; if they fail,
   the image build stops and displays the error.
5. Packages and installs the program into `/opt/evecc`.
6. Creates a separate runtime stage without a compiler, build tools,
   `pip`, Python 3 or Wine, and runs as a non-root user.

`BUILD_JOBS=2` is suitable for modest-RAM machines. Change it to `1` if
your system runs out of memory. For diagnostic image builds, you can set
`RUN_TESTS=0`, but doing so **skips validation**; the result should not be
treated as a tested release.

If the Dockerfile parser reports unsupported cache mounts, enable BuildKit,
for example with `DOCKER_BUILDKIT=1`, and update Docker/buildx if necessary.
The Dockerfile uses the `docker/dockerfile:1.7` syntax.

## Step 4 — Run EVECC with persistent files

Keys and compiled files are saved to the **current working directory**.
Create a work folder and bind-mount it into the container:

```bash
mkdir -p work

docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$(pwd)/work:/work" \
  --network none \
  evecc-native:local --help
```

To generate signing keys in the mounted folder:

```bash
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$(pwd)/work:/work" \
  --network none \
  evecc-native:local genkeys --password 'YOUR_UNIQUE_PASSWORD'
```

Your new `work/evecc.keys.pub` and `work/evecc.keys.priv` should appear
on the **host**. Never publish these keys, especially the private key.
Passing a password on the command line can expose it to shell history and
process listings. Use a dedicated password.

For a client DLL, mount a trusted *read-only* input directory separately:

```bash
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$(pwd)/work:/work" \
  -v "/path/to/historical/client:/client:ro" \
  --network none \
  evecc-native:local dumpkeys -i /client/bin/blue.dll
```

`blue.dll` is read as binary data. The DLL is **never executed** in Linux.

## Step 5 — Export a portable Linux `.tar.gz`

The separate `release-archive` target produces the same CPack artifact
without publishing any Docker image or involving a hosted build service:

```bash
mkdir -p dist

docker build --target release-archive \
  --build-arg BUILD_JOBS=2 \
  --output type=local,dest=dist .

ls -lh dist/evecc-Linux-*.tar.gz
```

This package contains the executable, embedded Python standard library,
both decompilers (including the full `uncompyle6` dependency closure), the
README, the Linux guide, and license information. It can run on compatible
Linux hosts outside Docker after extraction, subject to their available
system shared libraries.

## Step 6 — Optional Docker Compose workflow

```bash
mkdir -p work
docker compose build
docker compose run --rm --user "$(id -u):$(id -g)" evecc --help
```

To compile sources, mount source directories read-only via an additional
Compose volume or use the explicit `docker run -v` form above.

The Compose service disables the **runtime** network with
`network_mode: none`. Docker **build** still requires network access the
first time it retrieves CPython source and pinned Python wheels.

## Build speed and reproducibility

The Dockerfile caches three things using local BuildKit cache mounts:

| Cache | Contents | Benefit |
| --- | --- | --- |
| `/build` | CMake/Ninja outputs and Python 2.7.13 source | Incremental compilation across `docker build` runs |
| `/root/.cache/ccache` | Native GCC C/C++ compiler results | Faster rebuilds of modified source |
| `/wheel-cache` | SHA-256-verified Python 2 wheels | Avoid re-downloading dependencies |

Reusing the **same Docker builder and build context** lets these caches
survive changes to source files. BuildKit may prune its caches under storage
pressure; a clean first build downloads and compiles dependencies again.

The lock file `cmake/uncompyle6-wheels.cmake` fixes the exact wheels and
their cryptographic checksums. No unpinned `pip install` occurs during a
build. CMake on the host uses `build/wheel-cache/` by default, or you can
set `-DEVECC_WHEEL_CACHE=/your/local/wheels` to reuse a prepared cache.

The entire `vendor/` source is obtained through pinned Git submodules.
The legacy Python 2.7.13 source is fetched by the pinned upstream
`python-cmake-buildsystem` integration with its own archive integrity check.

## Common problems

| Symptom | Resolution |
| --- | --- |
| Missing `vendor/...` file | `git submodule update --init --recursive` on the host, then rebuild |
| Docker daemon permission denied | Fix Docker Engine/rootless setup; do not indiscriminately use `sudo` |
| `RUN --mount` unsupported | Enable BuildKit/buildx |
| Build runs out of memory | Use `--build-arg BUILD_JOBS=1` |
| Checksum mismatch on a wheel | Check proxy/cache integrity and delete the mismatched cache file; do not disable verification |
| Wheel or Python source download fails | Confirm builder has Internet access or populate a trusted local cache |
| `runscript` cannot import `uncompyle6` | The build's bundle smoke test should catch this; inspect `Python/uncompyle6.zip` and rerun cleanly |
| Container cannot write to `/work` | Pass `--user "$(id -u):$(id -g)"` and verify host folder permissions |
| `ldd` reports a missing shared object outside Docker | Use a compatible Linux base/system library set or run within the container |
| Docker reports a failed smoke test | Read the actual failure; do not assume the native port has passed |

## Security and redistribution

The runtime runs as a non-root user and can be run with networking disabled.
The `.dockerignore` prevents private EVE keys, logs, client outputs and local
build folders from entering Docker's build context. Always mount input client
files read-only and never publish private signing keys or account passwords.

This bundle includes third-party software under separate licenses,
including GPL-3.0-licensed `uncompyle6`. The bundler preserves wheel
distribution metadata/licenses and a copy of uncompyle6's GPL license.
Review the applicable terms before distributing prebuilt images or archives.

The historic EVE client and external `eveloader2` are separate projects and
**are not Linux-ported by this Dockerfile**. Its sole purpose is to build
and run the EVECC command-line toolkit natively on Linux.
