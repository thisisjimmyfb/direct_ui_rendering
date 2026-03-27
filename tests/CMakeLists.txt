cmake_minimum_required(VERSION 3.25)

include(GoogleTest)

# ---------------------------------------------------------------------------
# tests_unit — pure CPU math tests (no Vulkan context required)
# Sources: test_math.cpp (matrix construction + clip plane signs)
# ---------------------------------------------------------------------------
add_executable(tests_unit
    test_math.cpp
)

target_link_libraries(tests_unit PRIVATE
    direct_ui_rendering_lib
    GTest::gtest_main
)

target_include_directories(tests_unit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

gtest_discover_tests(tests_unit)

# ---------------------------------------------------------------------------
# Test shaders for tests_render — compiled with -DUI_TEST_COLOR so ui.frag
# outputs solid magenta instead of sampling the atlas.
# ---------------------------------------------------------------------------
set(TEST_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/test_shaders)
file(MAKE_DIRECTORY ${TEST_SHADER_OUTPUT_DIR})

set(RENDER_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../shaders)

set(TEST_SHADER_SOURCES
    ${RENDER_SHADER_DIR}/room.vert
    ${RENDER_SHADER_DIR}/room.frag
    ${RENDER_SHADER_DIR}/ui_direct.vert
    ${RENDER_SHADER_DIR}/ui_direct.frag
    ${RENDER_SHADER_DIR}/ui_ortho.vert
    ${RENDER_SHADER_DIR}/ui.frag
    ${RENDER_SHADER_DIR}/composite.frag
    ${RENDER_SHADER_DIR}/quad.vert
    ${RENDER_SHADER_DIR}/shadow.vert
)

set(TEST_SPIRV_OUTPUTS "")
foreach(SRC ${TEST_SHADER_SOURCES})
    get_filename_component(NAME ${SRC} NAME)
    set(SPV ${TEST_SHADER_OUTPUT_DIR}/${NAME}.spv)
    add_custom_command(
        OUTPUT  ${SPV}
        COMMAND Vulkan::glslc -DUI_TEST_COLOR ${SRC} -o ${SPV}
        DEPENDS ${SRC}
        COMMENT "Compiling test shader (UI_TEST_COLOR) ${NAME}"
        VERBATIM
    )
    list(APPEND TEST_SPIRV_OUTPUTS ${SPV})
endforeach()

add_custom_target(test_shaders DEPENDS ${TEST_SPIRV_OUTPUTS})

# ---------------------------------------------------------------------------
# tests_render — headless Vulkan render tests
# Sources: test_containment.cpp (UI pixel containment check)
#          test_perf.cpp (performance regression with skip-if-unset guards)
# Uses test shaders (UI_TEST_COLOR) so magenta pixels can be identified.
# ---------------------------------------------------------------------------
add_executable(tests_render
    test_containment.cpp
    test_perf.cpp
)

add_dependencies(tests_render test_shaders)

target_link_libraries(tests_render PRIVATE
    direct_ui_rendering_lib
    GTest::gtest_main
)

target_include_directories(tests_render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# TEST_SHADER_DIR points to the UI_TEST_COLOR-compiled shaders.
# When Renderer::init(headless=true) is implemented, it should load from
# TEST_SHADER_DIR (if defined) instead of the default SHADER_DIR.
target_compile_definitions(tests_render PRIVATE
    TEST_SHADER_DIR="${TEST_SHADER_OUTPUT_DIR}/"
)

gtest_discover_tests(tests_render)

# ---------------------------------------------------------------------------
# tests_sdf — headless Vulkan SDF threshold render test
# Uses production shaders (no UI_TEST_COLOR) so the real atlas-sampling and
# SDF smoothstep logic in ui_direct.frag is exercised.
# SHADER_DIR is inherited from direct_ui_rendering_lib compile definitions.
# ---------------------------------------------------------------------------
add_executable(tests_sdf
    test_sdf.cpp
)

add_dependencies(tests_sdf shaders)

target_link_libraries(tests_sdf PRIVATE
    direct_ui_rendering_lib
    GTest::gtest_main
)

target_include_directories(tests_sdf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

gtest_discover_tests(tests_sdf)
