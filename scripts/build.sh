#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------
PLATFORM="auto"
BUILD_TYPE="Debug"
ANDROID_ABI="arm64-v8a"
ANDROID_API="27"

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
# package_android_apk — build a signed debug APK from the compiled .so
# ---------------------------------------------------------------------------
package_android_apk() {
    local BUILD_DIR="$1"
    local ABI="$2"
    local API="$3"
    local APK_OUT="$BUILD_DIR/direct_ui_rendering.apk"
    local APK_ALIGNED="$BUILD_DIR/direct_ui_rendering_aligned.apk"
    local MANIFEST="$ROOT/android/AndroidManifest.xml"

    # Locate Android SDK
    local ANDROID_HOME_VAL="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
    if [[ -z "$ANDROID_HOME_VAL" ]]; then
        echo "Error: set ANDROID_HOME (or ANDROID_SDK_ROOT) to the Android SDK root." >&2
        exit 1
    fi

    # Find the latest build-tools version
    local BUILD_TOOLS_VER
    BUILD_TOOLS_VER=$(ls "$ANDROID_HOME_VAL/build-tools" 2>/dev/null | sort -V | tail -1)
    if [[ -z "$BUILD_TOOLS_VER" ]]; then
        echo "Error: No Android build-tools found in $ANDROID_HOME_VAL/build-tools" >&2
        exit 1
    fi
    local BUILD_TOOLS_DIR="$ANDROID_HOME_VAL/build-tools/$BUILD_TOOLS_VER"
    local AAPT2="$BUILD_TOOLS_DIR/aapt2"
    local ZIPALIGN="$BUILD_TOOLS_DIR/zipalign"
    local APKSIGNER="$BUILD_TOOLS_DIR/apksigner"

    # Locate android.jar for the target API
    local ANDROID_JAR="$ANDROID_HOME_VAL/platforms/android-${API}/android.jar"
    if [[ ! -f "$ANDROID_JAR" ]]; then
        local FOUND_JAR
        FOUND_JAR=$(find "$ANDROID_HOME_VAL/platforms" -name "android.jar" 2>/dev/null | sort -V | tail -1)
        if [[ -z "$FOUND_JAR" ]]; then
            echo "Error: android.jar not found for API $API in $ANDROID_HOME_VAL/platforms" >&2
            exit 1
        fi
        ANDROID_JAR="$FOUND_JAR"
        echo "Warning: android-${API} not found, using $ANDROID_JAR"
    fi

    echo "Packaging APK (build-tools $BUILD_TOOLS_VER, API $API)..."

    local STAGING_DIR="$BUILD_DIR/apk_staging"
    rm -rf "$STAGING_DIR"
    mkdir -p "$STAGING_DIR/res_compiled"
    mkdir -p "$STAGING_DIR/lib/$ABI"
    mkdir -p "$STAGING_DIR/assets/shaders"

    # Compile resources
    "$AAPT2" compile \
        --dir "$ROOT/android/res" \
        -o "$STAGING_DIR/res_compiled"

    # Link resources and manifest into a base APK
    local BASE_APK="$STAGING_DIR/base.apk"
    "$AAPT2" link \
        --manifest "$MANIFEST" \
        -I "$ANDROID_JAR" \
        "$STAGING_DIR/res_compiled/"*.flat \
        -o "$BASE_APK" \
        --min-sdk-version "$API" \
        --target-sdk-version 34 \
        --version-code 1 \
        --version-name "1.0"

    # Locate native library
    local SO_SRC="$BUILD_DIR/libdirect_ui_rendering.so"
    if [[ ! -f "$SO_SRC" ]]; then
        SO_SRC=$(find "$BUILD_DIR" -name "libdirect_ui_rendering.so" | head -1)
    fi
    if [[ -z "$SO_SRC" || ! -f "$SO_SRC" ]]; then
        echo "Error: libdirect_ui_rendering.so not found in $BUILD_DIR" >&2
        exit 1
    fi

    cp "$SO_SRC" "$STAGING_DIR/lib/$ABI/libdirect_ui_rendering.so"

    # Copy compiled shaders and atlas as APK assets
    cp "$BUILD_DIR/shaders/"*.spv "$STAGING_DIR/assets/shaders/"
    cp "$ROOT/assets/atlas.png" "$STAGING_DIR/assets/atlas.png"

    # Assemble final APK: start from base.apk, add lib/ and assets/
    cp "$BASE_APK" "$APK_OUT"
    (cd "$STAGING_DIR" && zip -r "$APK_OUT" lib/ assets/)

    # Zipalign (4-byte alignment required by Android)
    rm -f "$APK_ALIGNED"
    "$ZIPALIGN" -v 4 "$APK_OUT" "$APK_ALIGNED"
    mv "$APK_ALIGNED" "$APK_OUT"

    # Sign with debug keystore (auto-create if absent)
    local DEBUG_KEYSTORE="$HOME/.android/debug.keystore"
    if [[ ! -f "$DEBUG_KEYSTORE" ]]; then
        echo "Creating debug keystore..."
        mkdir -p "$HOME/.android"
        keytool -genkey -v \
            -keystore "$DEBUG_KEYSTORE" \
            -storepass android \
            -alias androiddebugkey \
            -keypass android \
            -keyalg RSA \
            -keysize 2048 \
            -validity 10000 \
            -dname "CN=Android Debug,O=Android,C=US" \
            -storetype pkcs12
    fi

    "$APKSIGNER" sign \
        --ks "$DEBUG_KEYSTORE" \
        --ks-pass pass:android \
        --key-pass pass:android \
        --ks-key-alias androiddebugkey \
        "$APK_OUT"

    rm -rf "$STAGING_DIR"
    echo "APK packaged: $APK_OUT"
}

# ---------------------------------------------------------------------------
# Build cmake arguments based on platform
# ---------------------------------------------------------------------------
CMAKE_ARGS=()
BUILD_DIR="$ROOT/build"

if [[ "$PLATFORM" == "windows" ]]; then
	BUILD_DIR="$ROOT/build_windows"
elif [[ "$PLATFORM" == "android" ]]; then
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
		"-G" "MSYS Makefiles"
        "-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN"
        "-DANDROID_ABI=$ANDROID_ABI"
        "-DANDROID_PLATFORM=android-${ANDROID_API}"
        "-DANDROID_STL=c++_shared"
    )
    echo "Platform: Android  ABI=$ANDROID_ABI  API=$ANDROID_API"
else
    echo "Platform: $PLATFORM"
fi

echo "Build type: $BUILD_TYPE"

cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel

# ---------------------------------------------------------------------------
# Post-build: package APK for Android
# ---------------------------------------------------------------------------
if [[ "$PLATFORM" == "android" ]]; then
    package_android_apk "$BUILD_DIR" "$ANDROID_ABI" "$ANDROID_API"
fi
