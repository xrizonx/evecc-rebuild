#!/usr/bin/env bash
# Native Linux build helper; runs everything locally, NEVER invokes GitHub CI.
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
JOBS=2
BUILD_TYPE=Release
COMPILER=default
CUSTOM_BUILD_DIR=0
PACKAGE=0
TEST=0
USE_CCACHE=1
WHEEL_CACHE=""

usage() {
  cat <<'HELP'
Usage: bash scripts/build-linux.sh [options]

Options:
  --build-dir PATH   CMake build directory (default ./build)
  --jobs N           Parallel compilation jobs (default 2)
  --debug            Build with debug symbols
  --clang            Use native clang/clang++ (default build-clang directory)
  --package          Produce Linux .tar.gz package locally
  --test             Run Python 3 native smoke tests after building
  --wheel-cache PATH Reuse SHA-verified Python 2 wheels at this path
  --no-ccache        Disable automatic local ccache acceleration
  -h, --help         Show this message

No GitHub Actions or other CI/CD services are used.
Run on Debian/Ubuntu after installing README build dependencies.
HELP
}

while (($#)); do
  case "$1" in
    --build-dir)
      [[ $# -ge 2 ]] || { echo "Missing --build-dir value" >&2; exit 2; }
      BUILD_DIR="$2"; CUSTOM_BUILD_DIR=1; shift 2 ;;
    --jobs)
      [[ $# -ge 2 && "$2" =~ ^[1-9][0-9]*$ ]] ||
        { echo "--jobs requires an integer greater than zero" >&2; exit 2; }
      JOBS="$2"; shift 2 ;;
    --debug) BUILD_TYPE=Debug; shift ;;
    --wheel-cache)
      [[ $# -ge 2 ]] || { echo "Missing --wheel-cache value" >&2; exit 2; }
      WHEEL_CACHE="$2"; shift 2 ;;
    --no-ccache) USE_CCACHE=0; shift ;;
    --clang) COMPILER=clang; shift ;;
    --package) PACKAGE=1; shift ;;
    --test) TEST=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "This helper supports native Linux only; use native CMake on Windows" >&2
  exit 2
fi
if [[ "$COMPILER" == clang && "$CUSTOM_BUILD_DIR" -eq 0 ]]; then
  BUILD_DIR="${ROOT}/build-clang"
fi
if [[ "$BUILD_DIR" != /* ]]; then
  BUILD_DIR="${ROOT}/${BUILD_DIR}"
fi

for executable in cmake ninja git; do
  command -v "$executable" >/dev/null ||
    { echo "Missing tool: $executable (see README)" >&2; exit 1; }
done
command -v c++ >/dev/null ||
  { echo "Missing C++ compiler (install build-essential)" >&2; exit 1; }

for path in crc32c loguru python-cmake-buildsystem inih uncompyle2 uncompyle6; do
  [[ -e "${ROOT}/vendor/${path}" ]] ||
    { echo "Missing vendor/${path}; run git submodule update --init --recursive" >&2; exit 1; }
done

flags=(-S "${ROOT}" -B "${BUILD_DIR}" -G Ninja
       "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
       -DENABLE_SSL=OFF -DENABLE_HASHLIB=OFF
       -DUSE_SYSTEM_OpenSSL=OFF)
if [[ -n "$WHEEL_CACHE" ]]; then
  if [[ "$WHEEL_CACHE" != /* ]]; then
    WHEEL_CACHE="${ROOT}/${WHEEL_CACHE}"
  fi
  flags+=("-DEVECC_WHEEL_CACHE=${WHEEL_CACHE}")
fi

if [[ "$USE_CCACHE" -eq 1 ]] && command -v ccache >/dev/null; then
  flags+=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
  echo "[INFO] Using local ccache for faster repeat builds"
elif [[ "$USE_CCACHE" -eq 0 ]]; then
  flags+=(-DCMAKE_C_COMPILER_LAUNCHER= -DCMAKE_CXX_COMPILER_LAUNCHER=)
fi

if [[ "$COMPILER" == clang ]]; then
  command -v clang >/dev/null && command -v clang++ >/dev/null ||
    { echo "Install clang and clang++ first" >&2; exit 1; }
  flags+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
fi

cmake "${flags[@]}"
cmake --build "${BUILD_DIR}" --parallel "$JOBS"

if [[ "$TEST" -eq 1 ]]; then
  command -v python3 >/dev/null ||
    { echo "Install python3 to run smoke tests" >&2; exit 1; }
  command -v openssl >/dev/null ||
    { echo "Install openssl to run signature smoke tests" >&2; exit 1; }
  python3 "${ROOT}/tests/native_smoke.py" "${BUILD_DIR}/evecc"
fi

if [[ "$PACKAGE" -eq 1 ]]; then
  cmake --build "${BUILD_DIR}" --target package
fi

echo "Native Linux build complete: ${BUILD_DIR}/evecc"
if [[ "$PACKAGE" -eq 1 ]]; then
  echo "Package archives are in: ${BUILD_DIR}"
fi
