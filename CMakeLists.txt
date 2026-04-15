cmake_minimum_required(VERSION 3.25)
project(direct_ui_rendering LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Detect Android build (NDK sets CMAKE_SYSTEM_NAME to "Android")
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(PLATFORM_ANDROID TRUE)
else()
    set(PLATFORM_ANDROID FALSE)
endif()

# ---------------------------------------------------------------------------
# Dependencies via FetchContent
# ---------------------------------------------------------------------------
include(FetchContent)

# GLFW — desktop only (Android uses ANativeWindow directly)
if(NOT PLATFORM_ANDROID)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
    )
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
endif()

# VulkanMemoryAllocator (VMA)
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
)
FetchContent_MakeAvailable(vma)

# GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)
FetchContent_MakeAvailable(glm)

# stb (header-only, we only need stb_image.h)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(stb)
add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${stb_SOURCE_DIR})

# GoogleTest
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# Vulkan
# ---------------------------------------------------------------------------
find_package(Vulkan REQUIRED)

# ---------------------------------------------------------------------------
# Validation layers: enabled in Debug, disabled in Release
# ---------------------------------------------------------------------------
add_library(validation_layer_config INTERFACE)
target_compile_definitions(validation_layer_config INTERFACE
    $<$<CONFIG:Debug>:ENABLE_VALIDATION_LAYERS>
)

# ---------------------------------------------------------------------------
# MSAA sample count configuration (default 4x, override with -DMSAA_SAMPLES=N)
# ---------------------------------------------------------------------------
set(MSAA_SAMPLES 4 CACHE STRING "MSAA sample count: 1, 2, 4, 8, or 16")
set_property(CACHE MSAA_SAMPLES PROPERTY STRINGS 1 2 4 8 16)

add_library(msaa_config INTERFACE)
target_compile_definitions(msaa_config INTERFACE
    MSAA_SAMPLES=${MSAA_SAMPLES}
)

# ---------------------------------------------------------------------------
# glslc shader compilation helper
# ---------------------------------------------------------------------------
# Usage: compile_shader(target shader_src output_spv [DEFINES ...])
function(compile_shader TARGET SHADER_SRC OUTPUT_SPV)
    cmake_parse_arguments(ARG "" "" "DEFINES" ${ARGN})
    set(DEFINE_FLAGS "")
    foreach(DEF ${ARG_DEFINES})
        list(APPEND DEFINE_FLAGS "-D${DEF}")
    endforeach()
    add_custom_command(
        OUTPUT  ${OUTPUT_SPV}
        COMMAND Vulkan::glslc ${DEFINE_FLAGS} ${SHADER_SRC} -o ${OUTPUT_SPV}
        DEPENDS ${SHADER_SRC}
        COMMENT "Compiling shader ${SHADER_SRC}"
        VERBATIM
    )
    target_sources(${TARGET} PRIVATE ${OUTPUT_SPV})
endfunction()

# ---------------------------------------------------------------------------
# Shader sources
# ---------------------------------------------------------------------------
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

set(SHADER_SOURCES
    ${SHADER_DIR}/room.vert
    ${SHADER_DIR}/room.frag
    ${SHADER_DIR}/ui_direct.vert
    ${SHADER_DIR}/ui_direct.frag
    ${SHADER_DIR}/ui_ortho.vert
    ${SHADER_DIR}/ui.frag
    ${SHADER_DIR}/composite.frag
    ${SHADER_DIR}/surface.frag
    ${SHADER_DIR}/quad.vert
    ${SHADER_DIR}/shadow.vert
)

# Shared shader headers
set(SHADER_HEADERS
    ${SHADER_DIR}/common.glsl
    ${SHADER_DIR}/ubo_structs.glsl
)

# Production shader SPIRVs (no test defines)
set(PROD_SPIRV_OUTPUTS "")
foreach(SRC ${SHADER_SOURCES})
    get_filename_component(NAME ${SRC} NAME)
    set(SPV ${SHADER_OUTPUT_DIR}/${NAME}.spv)
    add_custom_command(
        OUTPUT  ${SPV}
        COMMAND Vulkan::glslc -I${SHADER_DIR} ${SRC} -o ${SPV}
        DEPENDS ${SRC} ${SHADER_HEADERS}
        COMMENT "Compiling shader ${NAME}"
        VERBATIM
    )
    list(APPEND PROD_SPIRV_OUTPUTS ${SPV})
endforeach()

add_custom_target(shaders ALL DEPENDS ${PROD_SPIRV_OUTPUTS})

# ---------------------------------------------------------------------------
# direct_ui_rendering_lib — static library (main.cpp excluded)
# ---------------------------------------------------------------------------
set(LIB_SOURCES
    src/app.cpp
    src/renderer.cpp
    src/renderer_init.cpp
    src/renderer_pipelines.cpp
    src/renderer_renderpasses.cpp
    src/renderer_recording.cpp
    src/renderer_resources.cpp
    src/scene.cpp
    src/ui_system.cpp
    src/ui_surface.cpp
    src/metrics.cpp
)

add_library(direct_ui_rendering_lib STATIC ${LIB_SOURCES})
add_dependencies(direct_ui_rendering_lib shaders)

target_include_directories(direct_ui_rendering_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SHADER_OUTPUT_DIR}
)

target_link_libraries(direct_ui_rendering_lib PUBLIC
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
    glm::glm
    stb_image
    validation_layer_config
    msaa_config
)

if(PLATFORM_ANDROID)
    # android_native_app_glue is provided as NDK source — build it as a static library.
    add_library(android_native_app_glue STATIC
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
    )
    target_include_directories(android_native_app_glue PUBLIC
        ${ANDROID_NDK}/sources/android/native_app_glue
    )

    find_library(ANDROID_LIB android)
    find_library(LOG_LIB log)
    target_link_libraries(direct_ui_rendering_lib PUBLIC
        android_native_app_glue
        ${ANDROID_LIB}
        ${LOG_LIB}
    )
    target_compile_definitions(direct_ui_rendering_lib PUBLIC
        VK_USE_PLATFORM_ANDROID_KHR
        VMA_STATIC_VULKAN_FUNCTIONS=0
        VMA_DYNAMIC_VULKAN_FUNCTIONS=1
    )
else()
    target_link_libraries(direct_ui_rendering_lib PUBLIC glfw)
endif()

target_compile_definitions(direct_ui_rendering_lib PUBLIC
    SHADER_DIR="${SHADER_OUTPUT_DIR}/"
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

# ---------------------------------------------------------------------------
# App executable (or shared library on Android)
# ---------------------------------------------------------------------------
if(PLATFORM_ANDROID)
    # Android requires a shared library loaded by NativeActivity / GameActivity.
    add_library(direct_ui_rendering SHARED src/main.cpp)
    target_link_libraries(direct_ui_rendering PRIVATE direct_ui_rendering_lib)

    # ---------------------------------------------------------------------------
    # APK packaging (signing included) — runs as part of the cmake build
    # ---------------------------------------------------------------------------
    set(ANDROID_API_LEVEL "27" CACHE STRING "Android API level used for APK packaging")

    set(_ANDROID_HOME "$ENV{ANDROID_HOME}")
    if(NOT _ANDROID_HOME)
        set(_ANDROID_HOME "$ENV{ANDROID_SDK_ROOT}")
    endif()

    if(_ANDROID_HOME)
        # Locate the newest build-tools directory
        file(GLOB _BUILD_TOOLS_DIRS LIST_DIRECTORIES true "${_ANDROID_HOME}/build-tools/*")
        if(_BUILD_TOOLS_DIRS)
            list(SORT _BUILD_TOOLS_DIRS COMPARE NATURAL ORDER DESCENDING)
            list(GET _BUILD_TOOLS_DIRS 0 _BUILD_TOOLS_DIR)
        else()
            message(WARNING "No Android build-tools found — APK packaging disabled")
            set(_ANDROID_HOME "")
        endif()
    endif()

    if(_ANDROID_HOME)
        # Locate android.jar for the target API
        set(_ANDROID_JAR "${_ANDROID_HOME}/platforms/android-${ANDROID_API_LEVEL}/android.jar")
        if(NOT EXISTS "${_ANDROID_JAR}")
            file(GLOB_RECURSE _ANDROID_JARS "${_ANDROID_HOME}/platforms/*/android.jar")
            if(_ANDROID_JARS)
                list(SORT _ANDROID_JARS COMPARE NATURAL ORDER DESCENDING)
                list(GET _ANDROID_JARS 0 _ANDROID_JAR)
                message(WARNING "android-${ANDROID_API_LEVEL} not found; using ${_ANDROID_JAR}")
            else()
                message(WARNING "android.jar not found — APK packaging disabled")
                set(_ANDROID_HOME "")
            endif()
        endif()
    endif()

    if(_ANDROID_HOME)
        if(WIN32)
            set(_AAPT2    "${_BUILD_TOOLS_DIR}/aapt2.exe")
            set(_ZIPALIGN "${_BUILD_TOOLS_DIR}/zipalign.exe")
            if(EXISTS "${_BUILD_TOOLS_DIR}/apksigner.bat")
                set(_APKSIGNER "${_BUILD_TOOLS_DIR}/apksigner.bat")
            else()
                set(_APKSIGNER "${_BUILD_TOOLS_DIR}/apksigner.exe")
            endif()
        else()
            set(_AAPT2    "${_BUILD_TOOLS_DIR}/aapt2")
            set(_ZIPALIGN "${_BUILD_TOOLS_DIR}/zipalign")
            set(_APKSIGNER "${_BUILD_TOOLS_DIR}/apksigner")
        endif()

        find_program(KEYTOOL_EXEC keytool HINTS "$ENV{JAVA_HOME}/bin")
        if(NOT KEYTOOL_EXEC)
            message(WARNING "keytool not found — APK signing will fail at build time")
        endif()

        find_package(Python3 QUIET COMPONENTS Interpreter)
        if(NOT Python3_FOUND)
            message(WARNING "Python3 not found — APK packaging disabled")
            set(_ANDROID_HOME "")
        endif()
    endif()

    if(_ANDROID_HOME)
        set(_APK_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/direct_ui_rendering.apk")

        add_custom_command(
            OUTPUT "${_APK_OUTPUT}"
            COMMAND ${CMAKE_COMMAND}
                "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
                "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DABI=${ANDROID_ABI}"
                "-DAPI=${ANDROID_API_LEVEL}"
                "-DAAPT2=${_AAPT2}"
                "-DZIPALIGN=${_ZIPALIGN}"
                "-DAPKSIGNER=${_APKSIGNER}"
                "-DKEYTOOL=${KEYTOOL_EXEC}"
                "-DANDROID_JAR=${_ANDROID_JAR}"
                "-DSHADER_OUTPUT_DIR=${SHADER_OUTPUT_DIR}"
                "-DAPK_OUTPUT=${_APK_OUTPUT}"
                "-DPYTHON3=${Python3_EXECUTABLE}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_apk.cmake"
            DEPENDS
                direct_ui_rendering
                shaders
            COMMENT "Packaging and signing APK"
            VERBATIM
        )
        add_custom_target(package_apk ALL DEPENDS "${_APK_OUTPUT}")
    endif()
else()
    add_executable(direct_ui_rendering src/main.cpp)
    target_link_libraries(direct_ui_rendering PRIVATE direct_ui_rendering_lib)

    # Copy glyph atlas to the build output directory so the app can find it at
    # "assets/atlas.png" relative to the executable.
    add_custom_command(TARGET direct_ui_rendering POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:direct_ui_rendering>/assets
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/atlas.png
            $<TARGET_FILE_DIR:direct_ui_rendering>/assets/atlas.png
        COMMENT "Copying glyph atlas to build output"
    )
endif()

# ---------------------------------------------------------------------------
# Tests subdirectory (desktop only)
# ---------------------------------------------------------------------------
if(NOT PLATFORM_ANDROID)
    enable_testing()
    add_subdirectory(tests)
endif()
