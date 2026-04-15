#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------
PLATFORM="auto"
BUILD_TYPE="Debug"
ANDROID_ABI="arm64-v8a"
ANDROID_API="26"

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
for arg in "$@"; do
    case $arg in
        --platform=*)  PLATFORM="${arg#*=}"      ;;
        --build-type=*) BUILD_TYPE="${arg#*=}"   ;;
        --android-abi=*) ANDROID_ABI="${arg#*=}" ;;
        --android-api=*) ANDROID_API="${arg#*=}" ;;
        Debug|Release|RelWithDebInfo|MinSizeRel) BUILD_TYPE="$arg" ;;
        *)
            echo "Unknown argument: $arg" >&2
            echo "Usage: $0 [--platform=auto|android|windows|linux|macos] [--build-type=Debug|Release] [Debug|Release]" >&2
            echo "       [--android-abi=arm64-v8a|x86_64] [--android-api=26]" >&2
            exit 1
            ;;
    esac
done

# ---------------------------------------------------------------------------
# Auto-detect host platform
# ---------------------------------------------------------------------------
if [[ "$PLATFORM" == "auto" ]]; then
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
        Darwin)               PLATFORM="macos"   ;;
        Linux)                PLATFORM="linux"   ;;
        *)                    PLATFORM="linux"   ;;
    esac
fi

# ---------------------------------------------------------------------------
# Build cmake arguments based on platform
# ---------------------------------------------------------------------------
CMAKE_ARGS=()
BUILD_DIR="$ROOT/build"

if [[ "$PLATFORM" == "android" ]]; then
    # Resolve NDK root from common environment variables
    NDK_ROOT="${ANDROID_NDK_ROOT:-${ANDROID_NDK:-${NDK_HOME:-}}}"
    if [[ -z "$NDK_ROOT" ]]; then
        echo "Error: set ANDROID_NDK_ROOT (or ANDROID_NDK / NDK_HOME) to the NDK root." >&2
        exit 1
    fi
    TOOLCHAIN="$NDK_ROOT/build/cmake/android.toolchain.cmake"
    if [[ ! -f "$TOOLCHAIN" ]]; then
        echo "Error: Android toolchain not found at $TOOLCHAIN" >&2
        exit 1
    fi
    BUILD_DIR="$ROOT/build_android_${ANDROID_ABI}"
    CMAKE_ARGS+=(
        "-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN"
        "-DANDROID_ABI=$ANDROID_ABI"
        "-DANDROID_PLATFORM=android-${ANDROID_API}"
        "-DANDROID_STL=c++_shared"
		"-G MSYS Makefiles"
    )
    echo "Platform: Android  ABI=$ANDROID_ABI  API=$ANDROID_API"
else
    echo "Platform: $PLATFORM"
fi

echo "Build type: $BUILD_TYPE"

cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel
