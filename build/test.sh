#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build-$OSTYPE"

ctest --test-dir "$BUILD_DIR" --output-on-failure "$@"
