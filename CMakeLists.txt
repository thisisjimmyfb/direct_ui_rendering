cmake_minimum_required(VERSION 3.25)
project(direct_ui_rendering LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Dependencies via FetchContent
# ---------------------------------------------------------------------------
include(FetchContent)

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

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

# Production shader SPIRVs (no test defines)
set(PROD_SPIRV_OUTPUTS "")
foreach(SRC ${SHADER_SOURCES})
    get_filename_component(NAME ${SRC} NAME)
    set(SPV ${SHADER_OUTPUT_DIR}/${NAME}.spv)
    add_custom_command(
        OUTPUT  ${SPV}
        COMMAND Vulkan::glslc ${SRC} -o ${SPV}
        DEPENDS ${SRC}
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
    glfw
    GPUOpen::VulkanMemoryAllocator
    glm::glm
    stb_image
    validation_layer_config
)

target_compile_definitions(direct_ui_rendering_lib PUBLIC
    SHADER_DIR="${SHADER_OUTPUT_DIR}/"
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

# ---------------------------------------------------------------------------
# App executable
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# Tests subdirectory
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
