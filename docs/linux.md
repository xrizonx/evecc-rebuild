# Native Linux build (Debian 12 / Ubuntu 22.04+)

The executable produced by this procedure is a **Linux ELF binary**, not a
Windows binary under Wine. It statically embeds the EVE-compatible Python 2.7.13
runtime, and bundles its Python standard library and decompilers next to the
executable. Its C++ build uses Linux filesystem APIs and OpenSSL. The native\nLinux standard library directory is `Python/lib/python2.7/` (Windows uses\n`Python/Lib/`), because the CPython 2.7 startup expects this Linux layout.

## Install dependencies

On Debian/Ubuntu:

```bash
sudo apt update
sudo apt install -y git build-essential cmake ninja-build pkg-config \
  libssl-dev zlib1g-dev libbz2-dev libffi-dev libreadline-dev \
  libsqlite3-dev libncurses-dev libgdbm-dev libgdbm-compat-dev \
  liblzma-dev libexpat1-dev libcrypt-dev libdb-dev
```

## Clone, configure, build

```bash
git clone --recursive https://github.com/xrizonx/evecc-rebuild.git
cd evecc-rebuild

# The pinned Python 2.7 code cannot compile its obsolete _ssl module
# against modern OpenSSL 3. The EVECC encryption backend still links
# to native OpenSSL and is unaffected.
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SSL=OFF -DENABLE_HASHLIB=OFF -DUSE_SYSTEM_OpenSSL=OFF
cmake --build build --parallel 2

# Run from any directory; the runtime is located beside the executable.
./build/evecc --help

# Optional: portable ELF + Python directory in a compressed archive.
cmake --build build --target package
```

You can use GCC or Clang. CMake downloads the **exact pinned Python 2.7.13**
source distribution during configuration; network access is necessary the first
time unless you provide a local Python source tree. The `vendor` submodules are
required. The package contains the compiled ELF executable, Python runtime
data and decompiler archives.

## Generate keys and compile client code

Run commands in a **writable working directory**. Key files are loaded and
saved relative to the current working directory, not the executable.

```bash
mkdir -p work
cd work
../build/evecc genkeys --password 'your-secret'
../build/evecc dumpkeys -i '/path/to/EVE/bin/blue.dll'
../build/evecc compilecode --password 'your-secret' \
  -o compiled.code \
  -I '/path/to/source/script/carbon/client' \
  -I '/path/to/source/script/carbon/common' \
  -I '/path/to/source/script/eve/common' \
  -I '/path/to/source/script/eve/client'

../build/evecc dumpcode --password 'your-secret' \
  -i '/path/to/original/compiled.code' -o source
```

`blue.dll` is read strictly as a **binary data file** to locate the original
CryptoAPI key blobs. It is not loaded or executed on Linux. Keep
`evecc.keys.priv`, `ccp.keys.crypt` and your password private.

The source `*.py` path layout must be preserved under the `script/carbon`
and `script/eve` roots. Python 2.7 code is required; Python 3 bytecode and
marshal files are incompatible with this historical EVE client.

## What is / is not ported

- The EVECC command-line application, native C++17 file/cache operations,
  embedded Python runtime, RSA/SHA-1 signing, 3DES encryption, key generation
  and key-blob parsing have Linux implementations.
- `genkeys`, `dumpkeys`, `compilecode`, `dumpcode`, `unpyj`, `runscript` and
  `console` are available as commands. `compilelib` and `dumplib` were
  already marked non-functional in the original project and have not been
  declared fixed by this port.
- The external Windows-only **eveloader2** and the EVE game client are
  **not in this repository**. This work does not turn those programs into
  Linux executables; they require separate source and porting.
- CryptoAPI compatibility must be tested with **real extracted client
  keys and known-good compiled.code vectors** before using output against
  a running EVE client. Synthetic unit smoke tests do not prove
  byte-for-byte interoperability.

## Security

Python 2.7 and the original SHA-1/3DES formats are obsolete. They are
retained solely for compatibility with historical client data. Do not
expose EVECC or its embedded interpreter as a network service, and do not
reuse its passwords for modern accounts.
