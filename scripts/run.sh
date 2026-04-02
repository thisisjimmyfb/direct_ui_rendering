#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build-$OSTYPE"

"$BUILD_DIR"/Debug/direct_ui_rendering
