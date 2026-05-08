#!/usr/bin/env bash
# Native Linux build using whatever C++17 toolchain is on PATH (gcc/clang).
# Usage: ./scripts/build-linux.sh [extra cmake args...]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/linux"

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$BUILD" --config Release -j

echo
echo "Built: $BUILD/TemplateTCPServer"
