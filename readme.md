# EVECC Rebuild — legacy EVE compiled.code toolkit

EVECC reads and generates `compiled.code` for the legacy EVE Online **360229**
client. The **Linux-native port is under development**: it is intended to
compile as a Linux ELF executable with an embedded Python 2.7.13 runtime and
native OpenSSL cryptography. It does **not** run the EVE game or the external
Windows-only `eveloader2` launcher on Linux.

> **Build status:** Work in progress; Debian/Ubuntu compilation and
> CryptoAPI interoperability with real client key blobs still require
> local verification. Do not assume production/client compatibility until
> the full round-trip tests pass. The original `compilelib`/`dumplib` modes
> were already marked non-functional.
>
> **CI/CD:** No Linux GitHub Actions workflow is used. Linux builds are
> performed manually on your PC or server to avoid GitHub Actions minutes.

## Native Linux quick start — Debian 12 / Ubuntu 22.04 and 24.04

### 1. Install development tools and libraries

Use a modern 64-bit Debian/Ubuntu installation. Install GCC, the C/C++ toolchain,
CMake (3.20+), Ninja, Git, native OpenSSL, zlib and headers used by the
pinned Python source. No Visual Studio, .NET, Wine, Windows SDK or
Windows `python27.lib` is needed to compile the Linux version.

```bash
sudo apt update
sudo apt install -y --no-install-recommends \
  git ca-certificates file build-essential cmake ninja-build pkg-config \
  libssl-dev zlib1g-dev libbz2-dev libffi-dev \
  libreadline-dev libsqlite3-dev libncurses-dev \
  libgdbm-dev libgdbm-compat-dev liblzma-dev libexpat1-dev \
  libcrypt-dev libdb-dev
```

Python **3** is optional for the standalone smoke test; the application itself
uses **embedded Python 2.7.13** to preserve historical bytecode compatibility.
Python 2.7 is no longer maintained and is deliberately isolated inside EVECC.

### 2. Clone the Linux development branch and all submodules

Until the native code is merged, you **must check out `linux-native-port`**.
Cloning the default `main` branch alone will give you the older Windows-only code.

```bash
git clone --branch linux-native-port --recurse-submodules \
  https://github.com/xrizonx/evecc-rebuild.git
cd evecc-rebuild
git submodule update --init --recursive
```

The required submodules include `python-cmake-buildsystem`, `crc32c`, `loguru`,
`inih`, `uncompyle2`, and `uncompyle6`. For the initial configure, CMake also
downloads Python 2.7.13 source from python.org (unless already provided locally).

### 3. Configure and compile a native Linux binary

For the simplest **manual local build** (without GitHub Actions), run:

```bash
bash scripts/build-linux.sh --jobs 2
# Optional local packaging:
bash scripts/build-linux.sh --jobs 2 --package
# Optional local tests (requires python3 and openssl CLI):
bash scripts/build-linux.sh --jobs 2 --test
```

The script validates the essential tools, configures native dependencies and
builds on your own machine. For direct CMake control, use the equivalent steps
below.

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SSL=OFF \
  -DENABLE_HASHLIB=OFF \
  -DUSE_SYSTEM_OpenSSL=OFF

cmake --build build --parallel 2
```

**Why disable Python `_ssl` and `_hashlib`?** Those old Python 2.7 modules
do not compile cleanly against current OpenSSL. They are not EVECC's crypto
engine; the Linux EVECC executable separately links native `OpenSSL::Crypto`.
The obsolete `_curses` modules are also disabled internally by CMake.

Use `--parallel 2` on machines with limited RAM; raise it if you have more
memory. To build using Clang instead, configure a new build directory:

```bash
sudo apt install -y clang
CC=clang CXX=clang++ cmake -S . -B build-clang -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-clang --parallel 2
```

The program and bundled runtime are expected at:

```text
build/
├── evecc                     # Native Linux ELF binary
└── Python/
    ├── lib/python2.7/        # Linux Python 2.7.13 standard library
    ├── uncompyle2.zip
    └── uncompyle6.zip
```

### 4. Check the locally built binary (no CI/CD)

```bash
file build/evecc
ldd build/evecc
./build/evecc --help
```

`file` should report an **ELF 64-bit** executable. `ldd` should list Linux
system libraries and **must not** report `not found` for any dependency.

Once compiled, optional synthetic end-to-end validation is available with:

```bash
sudo apt install -y python3 openssl
python3 tests/native_smoke.py "$(realpath build/evecc)"
```

This test uses generated keys; it does not prove compatibility with your real
client `blue.dll` or `compiled.code`. You should validate those separately
using files you are authorized to use.

### 5. Build a portable Linux archive

```bash
cmake --build build --target package
ls -lh build/evecc-Linux-*.tar.gz
```

This produces a `.tar.gz` containing the Linux ELF executable and Python
runtime directory. It is **not a fully static executable**: the host still
needs compatible Linux C/C++ and crypto shared libraries. Packaging does not
require 7-Zip, GitHub Actions or Docker.

### 6. Work with legacy EVE client code

Run commands in a writable working directory because key files and build cache
are stored relative to the current directory.

```bash
mkdir -p work
cd work

# Create your own signing key pair.
../build/evecc genkeys --password 'choose-a-unique-password'

# Extract *data* blobs from a client blue.dll; the DLL is never executed.
../build/evecc dumpkeys -i '/path/to/EVE/bin/blue.dll'

# Extract/decompile an original file.
../build/evecc dumpcode --password 'choose-a-unique-password' \
  -i '/path/to/EVE/script/compiled.code' -o eve_code

# Recompile source trees, preserving their script/carbon/eve layout.
../build/evecc compilecode --password 'choose-a-unique-password' \
  -o compiled.code \
  -I '/path/to/source/script/carbon/client' \
  -I '/path/to/source/script/carbon/common' \
  -I '/path/to/source/script/eve/common' \
  -I '/path/to/source/script/eve/client'
```

You can also run `./build/evecc console`, `runscript -i <file>`,
`unpyj -i <file> -o <file>`, and `--help` (when their required keys and runtime
are present). Use `--no-cache` to force recompilation. `cleancache` removes
the local `.build_cache` folder.

**Keep `evecc.keys.priv` and `ccp.keys.crypt` private.** Generated `compiled.code`
may still need the **external** `eveloader2` public-key patch on its supported
platform. The EVE game client and `eveloader2` are separate projects and are
not part of this native Linux build.

### Troubleshooting local builds

| Error or symptom | Solution |
| --- | --- |
| `vendor/...` directory missing | Run `git submodule update --init --recursive` |
| `Python.h: No such file` | Ensure you are on `linux-native-port`; use a clean build directory |
| `io.h: No such file` | Your build must not include Python's `PC/pyconfig.h` on Linux; pull the latest branch |
| `makedev` / `major` / `minor` undefined | Pull latest CMake compatibility fixes; remove old `build` and reconfigure |
| `_cursesmodule.c` fails against ncurses | Use latest branch; CMake sets `ENABLE_CURSES=OFF` |
| `_ssl` / `_hashlib` against OpenSSL 3 fails | Use the exact CMake options shown above |
| `Missing embedded Python runtime` | Keep `build/evecc` beside `build/Python/`; on Linux the standard library lives under `Python/lib/python2.7/` |
| Missing shared library reported by `ldd` | Install the matching library package on the target Linux system |
| Key loading/signature fails | Validate `blue.dll` version, generated key blobs and password; CryptoAPI compatibility needs real-client testing |
| Build runs out of RAM | Retry with `cmake --build build --parallel 1` |

For additional rationale, caveats and details, see
**[docs/linux.md](docs/linux.md)**.

## Windows (legacy tooling)

The historical Windows build requires Visual Studio 2022 C++ Build Tools,
the Windows 10/11 SDK, CMake and Ninja. Its previous instructions used
`C:\Program Files\7-Zip\7z.exe` and `python27.lib`; the cross-platform CMake
work replaces the hard-coded Windows packaging path with CPack.

```bat
git clone --branch linux-native-port --recurse-submodules https://github.com/xrizonx/evecc-rebuild.git
cd evecc-rebuild
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-win --parallel 2
```

Run the Windows commands from an appropriate **Visual Studio x64 Developer
Command Prompt**. Windows builds after the CMake refactor have not yet been
re-validated; use the last known-good Windows release if you need it immediately.

## Port progress

| Area | State |
| --- | --- |
| Cross-platform CLI / Windows-specific arguments | Implemented; needs build verification |
| Portable filesystem, timestamps and cache | Implemented; needs runtime verification |
| Linux CMake, GCC/Clang and CPack archive | Implemented; needs local compile validation |
| Embedded Python 2.7.13 | Configured; modern Linux compatibility work ongoing |
| OpenSSL RSA/SHA1 and 3DES legacy key support | Implemented experimentally; verify against original client files |
| Synthetic local test harness | Added; not yet fully validated |
| `compilelib` / `dumplib` | Still unverified/nonfunctional upstream |
| External `eveloader2` and EVE client | Out of scope for this repository |
| GitHub Actions CI/CD | Disabled; manual local builds only |
| `scripts/build-linux.sh` helper | Implemented; no hosted build service required |

**Important:** EVECC preserves historical Python 2.7, SHA-1 and 3DES for
format compatibility. Do not expose it as a network service or reuse these
algorithms for new security-sensitive applications.
