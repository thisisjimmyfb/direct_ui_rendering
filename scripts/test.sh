#!/usr/bin/env bash
set -euo pipefail

PLATFORM="auto"
BUILD_TYPE="Debug"
ANDROID_ABI="arm64-v8a"

for arg in "$@"; do
    case $arg in
        --platform=*)    PLATFORM="${arg#*=}"    ;;
        --build-type=*)  BUILD_TYPE="${arg#*=}"  ;;
        --android-abi=*) ANDROID_ABI="${arg#*=}" ;;
        Debug|Release|RelWithDebInfo|MinSizeRel) BUILD_TYPE="$arg" ;;
        *)
            echo "Unknown argument: $arg" >&2
            echo "Usage: $0 [--platform=auto|android|windows|linux|macos] [--build-type=Debug|Release]" >&2
            echo "       [--android-abi=arm64-v8a|x86_64]" >&2
            exit 1
            ;;
    esac
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [[ "$PLATFORM" == "auto" ]]; then
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
        *)                    PLATFORM="unknown"   ;;
    esac
fi

# ---------------------------------------------------------------------------
# Android: APK packaging checks + CTest via ADB
# ---------------------------------------------------------------------------
if [[ "$PLATFORM" == "android" ]]; then
 
    # ---------------------------------------------------------------------------
    # CTest via ADB — run if a device is connected
    # ---------------------------------------------------------------------------
    if adb get-state >/dev/null 2>&1; then
        echo ""
        echo "Android device detected — running CTest suites via ADB ..."
    else
        echo ""
        echo "SKIP: no Android device connected — skipping CTest execution"
        echo "      Connect a device or start an emulator, then re-run with --platform=android"
		exit $?
    fi
fi

# ---------------------------------------------------------------------------
# Desktop: run ctest
# ---------------------------------------------------------------------------
case "$PLATFORM" in
    windows) BUILD_DIR="$ROOT/build_windows" ;;
	android) BUILD_DIR="$ROOT/build_android_${ANDROID_ABI}" ;;
    *)       BUILD_DIR="$ROOT/build"         ;;
esac

ctest --test-dir "$BUILD_DIR" --build-config "$BUILD_TYPE" --output-on-failure
