#!/usr/bin/env bash
# Optional: cross-compile a Windows x86_64 binary from a Linux host using
# the MinGW-w64 toolchain (package `mingw-w64` on Debian/Ubuntu/Fedora).
# Produces a static .exe with no MSVC runtime dependency.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/windows-mingw"
TOOLCHAIN="$ROOT/cmake/toolchain-mingw64.cmake"

if [[ ! -f "$TOOLCHAIN" ]]; then
    echo "Missing toolchain file: $TOOLCHAIN" >&2
    exit 1
fi

cmake -S "$ROOT" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" "$@"
cmake --build "$BUILD" --config Release -j

echo
echo "Built: $BUILD/TemplateTCPServer.exe"
