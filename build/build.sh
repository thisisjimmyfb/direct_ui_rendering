#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE="${1:-Release}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

cmake -S "$ROOT" -B "$ROOT/build-$OSTYPE" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$ROOT/build-$OSTYPE" --parallel
