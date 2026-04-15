#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE="${1:-Debug}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) BUILD_DIR="$ROOT/build_windows" ;;
    *)                    BUILD_DIR="$ROOT/build"          ;;
esac

ctest --test-dir "$BUILD_DIR" --build-config "$BUILD_TYPE" --output-on-failure
