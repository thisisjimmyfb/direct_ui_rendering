# APK packaging script — invoked at build time via cmake -P
# Required variables (passed with -D from CMakeLists.txt):
#   BUILD_DIR         — binary directory (contains .so and shaders/)
#   SOURCE_DIR        — source root (for android/, assets/)
#   ABI               — Android ABI (e.g. arm64-v8a)
#   API               — Android API level (e.g. 27)
#   AAPT2             — path to aapt2
#   ZIPALIGN          — path to zipalign
#   APKSIGNER         — path to apksigner or apksigner.bat (fallback)
#   APKSIGNER_JAR     — path to apksigner.jar (preferred over APKSIGNER)
#   JAVA              — path to java executable
#   KEYTOOL           — path to keytool
#   ANDROID_JAR       — path to android.jar
#   SHADER_OUTPUT_DIR — directory containing compiled .spv files
#   APK_OUTPUT        — destination APK path
#   PYTHON3           — path to python3 interpreter

cmake_minimum_required(VERSION 3.25)

set(STAGING_DIR "${BUILD_DIR}/apk_staging")
set(BASE_APK    "${STAGING_DIR}/base.apk")
set(RES_ZIP     "${STAGING_DIR}/res_compiled.zip")
set(MANIFEST    "${SOURCE_DIR}/android/AndroidManifest.xml")

file(REMOVE_RECURSE "${STAGING_DIR}")
file(MAKE_DIRECTORY "${STAGING_DIR}/lib/${ABI}")
file(MAKE_DIRECTORY "${STAGING_DIR}/assets/shaders")

# Compile resources
execute_process(
    COMMAND "${AAPT2}" compile --dir "${SOURCE_DIR}/android/res" -o "${RES_ZIP}"
    RESULT_VARIABLE RC
)
if(NOT RC EQUAL 0)
    message(FATAL_ERROR "aapt2 compile failed (exit ${RC})")
endif()

# Link manifest + resources into base APK
execute_process(
    COMMAND "${AAPT2}" link
        --manifest "${MANIFEST}"
        -I "${ANDROID_JAR}"
        "${RES_ZIP}"
        -o "${BASE_APK}"
        --min-sdk-version "${API}"
        --target-sdk-version 34
        --version-code 1
        --version-name "1.0"
    RESULT_VARIABLE RC
)
if(NOT RC EQUAL 0)
    message(FATAL_ERROR "aapt2 link failed (exit ${RC})")
endif()

# Locate native library
file(GLOB SO_CANDIDATES
    "${BUILD_DIR}/libdirect_ui_rendering.so"
    "${BUILD_DIR}/**/libdirect_ui_rendering.so"
)
list(GET SO_CANDIDATES 0 SO_SRC)
if(NOT EXISTS "${SO_SRC}")
    message(FATAL_ERROR "libdirect_ui_rendering.so not found in ${BUILD_DIR}")
endif()

file(COPY "${SO_SRC}" DESTINATION "${STAGING_DIR}/lib/${ABI}")

# Copy compiled shaders and atlas
file(GLOB SPV_FILES "${SHADER_OUTPUT_DIR}/*.spv")
foreach(SPV ${SPV_FILES})
    file(COPY "${SPV}" DESTINATION "${STAGING_DIR}/assets/shaders")
endforeach()
file(COPY "${SOURCE_DIR}/assets/atlas.png" DESTINATION "${STAGING_DIR}/assets")

# Assemble APK: start from base.apk, add lib/ and assets/ as stored (uncompressed) entries
file(COPY_FILE "${BASE_APK}" "${APK_OUTPUT}")
execute_process(
    COMMAND "${PYTHON3}" -c
        "import zipfile,os,sys
apk=sys.argv[1]; staging=sys.argv[2]
with zipfile.ZipFile(apk,'a',compression=zipfile.ZIP_STORED) as zf:
    for d in ('lib','assets'):
        for root,_,files in os.walk(os.path.join(staging,d)):
            for f in files:
                fp=os.path.join(root,f)
                zf.write(fp,os.path.relpath(fp,staging).replace(os.sep,'/'))"
        "${APK_OUTPUT}" "${STAGING_DIR}"
    RESULT_VARIABLE RC
)
if(NOT RC EQUAL 0)
    message(FATAL_ERROR "APK assembly (zip) failed (exit ${RC})")
endif()

# Zipalign (4-byte alignment required by Android)
set(ALIGNED_APK "${APK_OUTPUT}.aligned")
execute_process(
    COMMAND "${ZIPALIGN}" -v 4 "${APK_OUTPUT}" "${ALIGNED_APK}"
    RESULT_VARIABLE RC
)
if(NOT RC EQUAL 0)
    message(FATAL_ERROR "zipalign failed (exit ${RC})")
endif()
file(RENAME "${ALIGNED_APK}" "${APK_OUTPUT}")

# Create debug keystore if absent
if(WIN32)
    set(HOME_DIR "$ENV{USERPROFILE}")
else()
    set(HOME_DIR "$ENV{HOME}")
endif()
set(DEBUG_KEYSTORE "${HOME_DIR}/.android/debug.keystore")

if(NOT EXISTS "${DEBUG_KEYSTORE}")
    message(STATUS "Creating debug keystore at ${DEBUG_KEYSTORE}")
    file(MAKE_DIRECTORY "${HOME_DIR}/.android")
    execute_process(
        COMMAND "${KEYTOOL}" -genkey -v
            -keystore "${DEBUG_KEYSTORE}"
            -storepass android
            -alias androiddebugkey
            -keypass android
            -keyalg RSA
            -keysize 2048
            -validity 10000
            -dname "CN=Android Debug,O=Android,C=US"
            -storetype pkcs12
        RESULT_VARIABLE RC
    )
    if(NOT RC EQUAL 0)
        message(FATAL_ERROR "keytool failed to create debug keystore (exit ${RC})")
    endif()
endif()

# Sign APK — prefer java -jar apksigner.jar to avoid .bat invocation issues
if(JAVA AND EXISTS "${APKSIGNER_JAR}")
    execute_process(
        COMMAND "${JAVA}" -jar "${APKSIGNER_JAR}" sign
            --ks "${DEBUG_KEYSTORE}"
            --ks-pass pass:android
            --key-pass pass:android
            --ks-key-alias androiddebugkey
            "${APK_OUTPUT}"
        RESULT_VARIABLE RC
    )
elseif(WIN32 AND APKSIGNER MATCHES "\\.bat$")
    file(TO_NATIVE_PATH "${APKSIGNER}" APKSIGNER_NATIVE)
    execute_process(
        COMMAND cmd.exe /c "${APKSIGNER_NATIVE}" sign
            --ks "${DEBUG_KEYSTORE}"
            --ks-pass pass:android
            --key-pass pass:android
            --ks-key-alias androiddebugkey
            "${APK_OUTPUT}"
        RESULT_VARIABLE RC
    )
else()
    execute_process(
        COMMAND "${APKSIGNER}" sign
            --ks "${DEBUG_KEYSTORE}"
            --ks-pass pass:android
            --key-pass pass:android
            --ks-key-alias androiddebugkey
            "${APK_OUTPUT}"
        RESULT_VARIABLE RC
    )
endif()
if(NOT RC EQUAL 0)
    message(FATAL_ERROR "apksigner sign failed (exit ${RC})")
endif()

file(REMOVE_RECURSE "${STAGING_DIR}")
message(STATUS "APK packaged: ${APK_OUTPUT}")
