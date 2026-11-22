# EVECC Rebuild — second-pass source and documentation audit

**Branch:** `linux-native-port`  
**Date:** 2026-11-22  
**Execution policy:** Local/manual builds only. The repository's Linux GitHub
Actions workflow was removed; do not enable or run CI/CD.

## What was reviewed

This review covered the repository's **first-party source and packaging
configuration**: `CMakeLists.txt`, `cmake/`, `src/`, `scripts/`,
`build_and_run.bat`, `tests/`, `readme.md`, `docs/` and `.gitmodules`.

The `vendor/` entries are **external git submodules** (CPython CMake build
system, crc32c, loguru, inih, uncompyle2 and uncompyle6). Their integration
and required files were checked, but this is **not a line-by-line audit of
every upstream third-party dependency**.

## Defects found and corrected

| Area | Issue | Change |
| --- | --- | --- |
| RSA private keys | Linux CryptoAPI-compatible RC4 wrapping incorrectly began at offset 20 and inspected RSA fields before decryption | Encrypt/decrypt everything after the 8-byte PUBLICKEYSTRUC header, as documented by Microsoft |
| Key output | Key generation and key extraction could overwrite key files or report write failures as success | Reject existing keys; propagate write errors; secure Linux private key creation with restrictive mode and exclusive file creation |
| RSA parsing | OpenSSL BIGNUM allocations could leak on malformed or partially imported keys | RAII ownership until transferred to RSA |
| 3DES/zlib on Windows | `-k ccp` could resolve to an unset key; zip paths leaked memory or returned invalid unsigned lengths | Fallback to the CCP key, use bounded zlib helpers, free state and return failure consistently |
| Linux zlib | Repository contained zlib 1.2.8 headers despite linking a modern system zlib | Remove obsolete headers and use the header matching the linked package |
| Python runtime | Linux standard-library layout and codec import were incorrect | Native `Python/lib/python2.7/` layout and explicit codec check |
| CLI | Missing input, zero-length operations and failed Python commands could be reported incorrectly | Validate input and propagate failures; isolate standalone `console`/`runscript` from unnecessary crypto-key initialization |
| Cache | Windows file-handle code was not portable | C++17 filesystem and bounded cache I/O |
| Extraction | `tmp_out` collided across runs; existing output could be mixed silently | Unique staging directories, no overwrite, clear partial-failure reporting |
| Extraction security | Embedded filenames and archive entries were not reliably checked for path traversal | Normalize paths and reject unsafe components |
| Shell builder | Wrong platform, build directories and test prerequisites were not checked consistently | Linux-only validation, distinct Clang build directory and local test tool checks |
| Windows example | Hard-coded machine paths and credentials; deleted a DLL and launched a client | Safe configurable example, no destructive deletion or automatic game launch |
| Packaging | Readme referred to a Linux guide that was not in the package | Package Linux guide, root README, license and runtime |
| Secret hygiene | Generated private key files could be accidentally committed | `.gitignore` exclusions for keys, artifacts, build output and logs |
| Documentation | Linux guide had malformed escape sequences and incomplete copy/paste commands | Rewrite step by step, with expected results, troubleshooting and non-CI tests |

## Documentation review: end-user workflow

The **README** serves as the quick-start guide and
**[Linux instructions](linux.md)** provide full explanations.

1. Install Debian/Ubuntu build tools, development headers and optional test tools.
2. Clone the correct `linux-native-port` branch and recursively initialize submodules.
3. Run `bash scripts/build-linux.sh --jobs 2`, or the equivalent manual CMake commands.
4. Check that `build/evecc` is native ELF and its sibling `Python/` tree exists.
5. Test locally via `python3 tests/native_smoke.py "$(realpath build/evecc)"`.
6. In a separate working folder, generate signing keys and extract key blobs
   from an authorized historical `blue.dll` file.
7. Decompile and compile the legacy EVE Python 2.7 code with explicit paths.
8. Package the native executable and Python tree using CPack.
9. Use troubleshooting commands for missing headers, libraries, encoding and keys.

Every operation is manual and runs on the user's computer. No cloud build
workflow or CI/CD is required.

## Remaining validation and scope limits

**A clean end-to-end native build has not been run after these fixes.**
This environment could not fetch a complete GitHub checkout for local
compilation. Earlier Ubuntu 22.04 and 24.04 builds progressed to smoke
tests but failed on codec initialization, which has since been changed.

The following still require manual verification before declaring the
port finished:

- Complete Linux GCC and Clang builds with submodules and Python 2.7.13.
- CryptoAPI key-import and RSA-signature compatibility using **known-good
  Windows-created blobs** as well as known-good legacy client code.
- Original `compiled.code` decompile/recompile round-trip accepted by a
  real historical EVE client.
- Native Windows build regression tests, particularly key generation,
  crypto operations and CMake packaging.
- The upstream `compilelib` and `dumplib` modes, originally marked nonfunctional.
- The optional `uncompyle6` Python dependencies (`xdis` and `spark-parser`).
- External EVE client/eveloader2 Linux support: **out of scope**, because
  neither project is part of this repository.

These are not claimed to be fixed or passing merely because source changes
were committed. Python 2.7, RC4, SHA-1 and 3DES remain obsolete and are
retained only to match legacy file formats.

## Manual verification checklist (no CI/CD)

Run these commands on your own Debian/Ubuntu machine:

```bash
git switch linux-native-port
git pull --ff-only
git submodule update --init --recursive

bash scripts/build-linux.sh --jobs 2 --test --package
file build/evecc
ldd build/evecc
./build/evecc --help
```

Expected test message: `Native Linux integration smoke test passed`.
Do not merge the branch until this works and client-specific vector tests
have passed.

For real-client testing, **back up source files and existing key files first**.
Use only trusted `compiled.code` inputs because the historical format is
pickle-based and can execute hostile payloads during deserialization.

## References

Microsoft's [Base Provider Key BLOBs documentation](https://learn.microsoft.com/en-us/windows/win32/seccrypto/base-provider-key-blobs)
specifies that encrypted PRIVATEKEYBLOB data begins after its eight-byte
PUBLICKEYSTRUC header. This was the basis for correcting the Linux RSA key
import/export implementation.
