#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
BUILD_TYPE="${BUILD_TYPE:-Release}"
case "$BUILD_TYPE" in
    Release) OUTPUT="$(pwd)/bin/iidxfsms" ;;
    Debug) OUTPUT="$(pwd)/bin/iidxfsms/debug" ;;
    *) echo "Unsupported build type: $BUILD_TYPE" >&2; exit 1 ;;
esac
BUILDDIR="${BUILD_ROOT:-$(pwd)}/cmake-build-${BUILD_TYPE,,}-mingw64"
cmake -S . -B "$BUILDDIR" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$(pwd)/cmake/mingw64.cmake" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$OUTPUT"
cmake --build "$BUILDDIR" --parallel "$(nproc)"
echo "Built $OUTPUT/iidxfsms.dll"