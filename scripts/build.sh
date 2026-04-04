#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE="${1:-Debug}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build"

cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel
