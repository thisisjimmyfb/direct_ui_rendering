#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE="${1:-Release}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build"

ctest --test-dir "$BUILD_DIR" --build-config "$BUILD_TYPE" --output-on-failure
