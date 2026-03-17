#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<EOF
Usage: $0 [--vcpkg-root PATH]

Builds micromamba on Linux using vcpkg with:
- all C/C++ dependencies built from source
- libsolv built from the local overlay with conda-forge patches
- frame pointers enabled for perf(1) profiling

Environment:
  VCPKG_ROOT   Path to vcpkg checkout (default: inferred from --vcpkg-root or required)

Example:
  VCPKG_ROOT=\$HOME/src/vcpkg \\
    $0
EOF
}

VCPKG_ROOT="${VCPKG_ROOT:-}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --vcpkg-root)
      VCPKG_ROOT="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ -z "${VCPKG_ROOT}" ]]; then
  echo "Error: VCPKG_ROOT is not set and --vcpkg-root was not provided." >&2
  exit 1
fi

if [[ ! -x "${VCPKG_ROOT}/vcpkg" && ! -x "${VCPKG_ROOT}/vcpkg.exe" ]]; then
  echo "Bootstrapping vcpkg in ${VCPKG_ROOT}..."
  pushd "${VCPKG_ROOT}" >/dev/null
  ./bootstrap-vcpkg.sh
  popd >/dev/null
fi

export VCPKG_ROOT
export VCPKG_OVERLAY_PORTS="${REPO_ROOT}/cmake/vcpkg-overlays"

# Enforce frame pointers in vcpkg-built dependencies.
export VCPKG_C_FLAGS="${VCPKG_C_FLAGS:-} -fno-omit-frame-pointer"
export VCPKG_CXX_FLAGS="${VCPKG_CXX_FLAGS:-} -fno-omit-frame-pointer"

echo "Using VCPKG_ROOT=${VCPKG_ROOT}"
echo "Using VCPKG_OVERLAY_PORTS=${VCPKG_OVERLAY_PORTS}"
echo "Ensuring dependencies are built for triplet x64-linux..."
"${VCPKG_ROOT}/vcpkg" install --triplet x64-linux || {
  echo "vcpkg install failed" >&2
  exit 1
}

echo "Configuring CMake with preset micromamba-vcpkg-linux-frameptr..."
cmake --preset micromamba-vcpkg-linux-frameptr \
  -D CMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
  -D VCPKG_TARGET_TRIPLET=x64-linux

echo "Building micromamba with preset micromamba-vcpkg-linux-frameptr-build..."
cmake --build --preset micromamba-vcpkg-linux-frameptr-build

echo
echo "micromamba built successfully."
echo "Binary should be at: $(pwd)/build/micromamba-vcpkg-linux-frameptr/micromamba/micromamba"
echo
echo "Example perf(1) usage:"
echo "  perf record ./build/micromamba-vcpkg-linux-frameptr/micromamba/micromamba --version"
