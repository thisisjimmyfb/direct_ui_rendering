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
        Darwin)               PLATFORM="macos"   ;;
        Linux)                PLATFORM="linux"   ;;
        *)                    PLATFORM="linux"   ;;
    esac
fi

# ---------------------------------------------------------------------------
# Android: verify APK packaging output
# ---------------------------------------------------------------------------
if [[ "$PLATFORM" == "android" ]]; then
    BUILD_DIR="$ROOT/build_android_${ANDROID_ABI}"
    APK_PATH="$BUILD_DIR/direct_ui_rendering.apk"
    PASS=0
    FAIL=0

    run_test() {
        local name="$1"
        local result="$2"
        if [[ "$result" == "pass" ]]; then
            echo "PASS: $name"
            PASS=$((PASS + 1))
        else
            echo "FAIL: $name"
            FAIL=$((FAIL + 1))
        fi
    }

    # Test 1: APK exists
    if [[ -f "$APK_PATH" ]]; then
        run_test "APK exists at build_android_${ANDROID_ABI}/direct_ui_rendering.apk" pass
    else
        run_test "APK exists at build_android_${ANDROID_ABI}/direct_ui_rendering.apk" fail
    fi

    # Test 2: APK contains the native library
    if [[ -f "$APK_PATH" ]] && unzip -l "$APK_PATH" 2>/dev/null | grep -q "lib/${ANDROID_ABI}/libdirect_ui_rendering.so"; then
        run_test "APK contains lib/${ANDROID_ABI}/libdirect_ui_rendering.so" pass
    else
        run_test "APK contains lib/${ANDROID_ABI}/libdirect_ui_rendering.so" fail
    fi

    # Test 3: APK contains the glyph atlas
    if [[ -f "$APK_PATH" ]] && unzip -l "$APK_PATH" 2>/dev/null | grep -q "assets/atlas.png"; then
        run_test "APK contains assets/atlas.png" pass
    else
        run_test "APK contains assets/atlas.png" fail
    fi

    # Test 4: APK contains compiled shaders
    if [[ -f "$APK_PATH" ]] && unzip -l "$APK_PATH" 2>/dev/null | grep -q "assets/shaders/"; then
        run_test "APK contains assets/shaders/" pass
    else
        run_test "APK contains assets/shaders/" fail
    fi

    # Test 5: APK is zipaligned (4-byte alignment required for Android)
    ANDROID_HOME_VAL="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
    if [[ -f "$APK_PATH" && -n "$ANDROID_HOME_VAL" ]]; then
        BUILD_TOOLS_VER=$(ls "$ANDROID_HOME_VAL/build-tools" 2>/dev/null | sort -V | tail -1)
        ZIPALIGN="$ANDROID_HOME_VAL/build-tools/${BUILD_TOOLS_VER}/zipalign"
        if [[ -x "$ZIPALIGN" ]]; then
            if "$ZIPALIGN" -c 4 "$APK_PATH" 2>/dev/null; then
                run_test "APK is zipaligned" pass
            else
                run_test "APK is zipaligned" fail
            fi
        else
            echo "SKIP: zipalign not found — skipping alignment check"
        fi
    else
        echo "SKIP: ANDROID_HOME not set — skipping zipalign check"
    fi

    echo ""
    echo "Results: $PASS passed, $FAIL failed"
    [[ "$FAIL" -eq 0 ]]
    exit $?
fi

# ---------------------------------------------------------------------------
# Desktop: run ctest
# ---------------------------------------------------------------------------
case "$PLATFORM" in
    windows) BUILD_DIR="$ROOT/build_windows" ;;
    *)       BUILD_DIR="$ROOT/build"         ;;
esac

ctest --test-dir "$BUILD_DIR" --build-config "$BUILD_TYPE" --output-on-failure
