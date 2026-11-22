# EVECC on Linux — complete manual setup guide

This guide targets **64-bit Debian 12** and **Ubuntu 22.04 / 24.04**.
It uses only your own computer's CPU, disk, and network connection.
**GitHub Actions/CI/CD is not required, configured, or invoked.**

**Status:** The Linux port is a work in progress. Its ELF compilation reached
the smoke-test stage on Ubuntu 22.04/24.04 in an earlier run, but recent
runtime and CryptoAPI compatibility fixes have **not** received a complete
new local build. Before relying on compiled code, carry out Step 5 and check
the results against your own known-good EVE client files.

## Before you begin

You need a 64-bit Linux machine, a terminal, an Internet connection for the
first source download, and permission to work with the historical EVE files
you use. This repository builds the **EVECC command-line compiler**, not
the EVE client or the separate Windows-only `eveloader2` launcher.

| Component | Purpose |
| --- | --- |
| Git and CA certificates | Download EVECC and pinned submodules |
| GCC/G++ or Clang | Compile native Linux C and C++ |
| CMake 3.20+ and Ninja | Configure and build all targets |
| pkg-config | Resolve native build libraries |
| OpenSSL development libraries | RSA/SHA-1 and 3DES compatibility backend |
| zlib development libraries | Decompress/encrypt legacy EVE code |
| Python 2.7.13 source | Built into EVECC to preserve original bytecode |
| Python 3 and OpenSSL CLI (optional) | Run standalone local smoke tests |

Python 2.7.13 is **embedded by the build**. You do not need to install a
system-wide Python 2 interpreter. Python 3 cannot replace Python 2 for
`compiled.code`, because marshal and bytecode formats differ.

## Step 1 — Install the required packages

Open a terminal and run these commands. You may need an administrator password
for `sudo`.

```bash
sudo apt update
sudo apt install -y --no-install-recommends \
  git ca-certificates file build-essential cmake ninja-build pkg-config \
  libssl-dev zlib1g-dev libbz2-dev libffi-dev \
  libreadline-dev libsqlite3-dev libncurses-dev \
  libgdbm-dev libgdbm-compat-dev liblzma-dev libexpat1-dev \
  libcrypt-dev libdb-dev
```

If you want to run the optional local tests, also install:

```bash
sudo apt install -y python3 openssl
```

Check the machine and development tools:

```bash
uname -s -m
cmake --version
g++ --version
ninja --version
```

Expected: `Linux x86_64`, CMake at least 3.20, and installed GCC/Ninja.
Other CPU architectures are not yet tested.

## Step 2 — Download the correct EVECC branch

The Linux port is on `linux-native-port`, not the original `main` branch.

```bash
git clone --branch linux-native-port --recurse-submodules \
  https://github.com/xrizonx/evecc-rebuild.git
cd evecc-rebuild
git submodule update --init --recursive
git branch --show-current
```

Expected branch: `linux-native-port`. If you already have a checkout:

```bash
git switch linux-native-port
git pull --ff-only
git submodule update --init --recursive
```

The first build downloads the **Python 2.7.13** source archive through its
pinned CMake subproject. Keep network access available, or provide a local
extracted Python source tree and use the `SRC_DIR` and
`EVECC_PYTHON_LIB_SOURCE` CMake options.

## Step 3 — Build EVECC directly on your Linux machine

The easiest option is the repository's local build helper:

```bash
bash scripts/build-linux.sh --jobs 2
```

`--jobs 2` limits concurrent compilation to reduce memory usage. To build
a portable `.tar.gz` at the same time:

```bash
bash scripts/build-linux.sh --jobs 2 --package
```

To choose Clang, install it and use a **separate** build directory:

```bash
sudo apt install -y clang
bash scripts/build-linux.sh --clang --build-dir build-clang --jobs 2
```

Alternatively, the equivalent manual CMake commands are:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SSL=OFF \
  -DENABLE_HASHLIB=OFF \
  -DUSE_SYSTEM_OpenSSL=OFF

cmake --build build --parallel 2
```

Python 2.7's `_ssl`, `_hashlib` and `_curses` extensions have compatibility
problems with recent system headers. Those optional Python modules are disabled
in this build; **EVECC's** encryption is separately linked to OpenSSL.
The remaining required Python and C/C++ components are built natively.

If you already tried an older version and your CMake cache is out of date,
configure from a fresh directory rather than modifying the previous build:

```bash
cmake -S . -B build-clean -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SSL=OFF -DENABLE_HASHLIB=OFF -DUSE_SYSTEM_OpenSSL=OFF
cmake --build build-clean --parallel 2
```

## Step 4 — Check the executable and Python directory

After a successful normal build, you should have:

```text
build/
  evecc
  Python/
    lib/
      python2.7/
        encodings/
        os.py
        ...
    uncompyle2.zip
    uncompyle6.zip   # Includes xdis, spark_parser, click and six
```

Linux uses `Python/lib/python2.7/`, unlike Windows' `Python/Lib/`.
**Do not move `evecc` away from its sibling `Python/` directory.**

From the root of the checkout, run:

```bash
file build/evecc
ldd build/evecc
./build/evecc --help
```

`file` should report an ELF x86-64 executable. The `ldd` output should
contain no `not found` dependencies. `--help` should display command usage.
Note that Python is **statically embedded**, but the whole EVECC binary is not
fully static and needs compatible Linux runtime libraries.

## Step 5 — Run manual integration tests (no GitHub Actions)

The repository includes a synthetic test of key generation, extracting
binary key blobs, compiling and decrypting a test source file, and checking
the RSA signature.

```bash
python3 tests/native_smoke.py "$(realpath build/evecc)"
```

Or combine build and test in one local invocation:

```bash
bash scripts/build-linux.sh --jobs 2 --test
```

The test prints `Native Linux integration smoke test passed` when successful.
A **synthetic test does not prove** that a genuine legacy EVE build accepts
generated `compiled.code` or imported keys. Test with original client data
you are authorized to access before using it in a client.

## Step 6 — Use the EVECC commands

Choose a writable directory so your key files do not get mixed with the
compiler's source code. The commands below assume you are in the EVECC
repository directory before running `cd work`.

```bash
mkdir -p work
cd work

# 1. Generate your own RSA signing keys; remember the password.
../build/evecc genkeys --password 'YOUR_UNIQUE_PASSWORD'

# 2. Read historical client key blobs from blue.dll as binary data.
#    The Windows DLL is NOT executed on Linux.
../build/evecc dumpkeys -i '/path/to/EVE/bin/blue.dll'

# 3. Decompile the client's original compiled.code into a source folder.
../build/evecc dumpcode --password 'YOUR_UNIQUE_PASSWORD' \
  -i '/path/to/EVE/script/compiled.code' -o eve_code

# 4. Compile edited Python 2.7 sources (replace paths with real folders).
../build/evecc compilecode --password 'YOUR_UNIQUE_PASSWORD' \
  -o compiled.code \
  -I '/path/to/source/script/carbon/client' \
  -I '/path/to/source/script/carbon/common' \
  -I '/path/to/source/script/eve/common' \
  -I '/path/to/source/script/eve/client'
```

If working with sources produced by `dumpcode`, inspect its directory tree.
Use the `carbon` and `eve` directories actually present, and omit missing
`-I` entries. The output directory should contain the code in its original
EVE-compatible directory structure.

**File safety:** EVECC will not silently overwrite existing signing keys or
the extracted `ccp.keys.*` files. Back up existing keys before generating or
extracting replacements. Replacing signing keys also changes which compiled
code the client can verify.

**Password safety:** CLI passwords can appear in your terminal history and
process arguments. Use a unique password, protect the private key files,
and do not use these historical formats for modern security-sensitive data.
`dumpcode` uses Python's pickle format: only process trusted client files.

## Step 7 — Package and move your Linux build

From the repository root:

```bash
cmake --build build --target package
ls -lh build/evecc-Linux-*.tar.gz
```

The CPack archive contains the executable, its sibling Python runtime,
README, Linux guide and license. On the destination Linux computer, extract
the archive and preserve its `Python/` directory relative to `evecc`.
Shared libraries used by the executable still need to be available on that
computer. Use `ldd evecc` there to find any missing dependencies.

## Troubleshooting

| Problem | What to do |
| --- | --- |
| `git submodule` folders missing | Run `git submodule update --init --recursive` |
| Python 2.7 source cannot be downloaded | Confirm Internet access or provide a local source tree through CMake options |
| `Python.h` or `pyconfig.h` missing | Use the Linux branch and reconfigure in a clean build directory |
| `io.h` missing | Windows Python include paths leaked into Linux; update branch and clean your build directory |
| `major`, `minor` or `makedev` undefined | Update branch; Linux build injects `sys/sysmacros.h` into Python compilation |
| `_cursesmodule.c` errors | The old ncurses extension should be disabled; confirm you pulled latest CMake files |
| `_ssl`/`_hashlib` errors | Use the CMake options from Step 3 |
| `LookupError: no codec search functions registered` | Verify `build/Python/lib/python2.7/encodings/` exists; refresh build after pulling latest |
| `Cannot download xdis/spark-parser/click/six` | Restore network access or supply checksum-matching wheels in `build/wheel-cache/` |
| `Unable to find key blob` | Use a matching, unmodified `blue.dll`; inspect file/size/version |
| `Keys already exist` | Back up the existing key pair and carefully remove old files before regenerating |
| `failed to decrypt/decompress` | Confirm the key matches this client, password is correct and input data is valid |
| `ldd` shows `not found` | Install the missing system library; do not copy random `.so` files from another distro |
| Out of memory during compilation | Rebuild with `--jobs 1` |

## Scope and known limitations

- EVECC native Linux build and OpenSSL compatibility code are under active
  validation; **no complete current manual end-to-end build has been verified**.
- The historical EVE `compiled.code` requires embedded **Python 2.7.13**;
  replacing it with Python 3 would change the bytecode format.
- The C++ application uses native Linux APIs. `blue.dll` is scanned as a file,
  never executed through Wine.
- The optional `uncompyle6` archive now includes `xdis`, `spark_parser`,
  `click`, and `six` from checksum-pinned Python 2 wheels. The default
  decompiler tries `uncompyle2` first and falls back to `uncompyle6`.
  This dependency integration requires a successful local smoke test before
  being considered verified.
- `compilelib` and `dumplib` were already considered non-functional upstream.
- The repository now has a multi-stage native Linux Dockerfile and an
  optional Compose workflow. See [Docker instructions](docker.md).
- The separate game client and external Windows-only `eveloader2` are not
  contained in this repository and have not been ported here.
- RSA/SHA-1, RC4 and 3DES are obsolete and retained **only** for legacy
  binary interoperability, not new security systems.

For a concise overview and Windows notes, return to [the README](../readme.md).
