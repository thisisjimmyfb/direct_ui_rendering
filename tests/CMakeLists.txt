cmake_minimum_required(VERSION 3.25)

include(GoogleTest)

# ---------------------------------------------------------------------------
# tests_unit — pure CPU tests (no Vulkan context required)
# Sources split by domain:
#   test_matrix_math.cpp — TransformMath, DepthBias, ShadowBias
#   test_clip_planes.cpp  — ClipPlane, ClipPlaneTilted, ClipPlane3D, ClipPlaneSymmetry
#   test_scene.cpp        — SceneInit, WorldCorners, SceneAnimation, LightFrustum
#   test_ui_system.cpp    — SDFConstants, UISystemUVTable, TessellateString, UISurface
#   test_metrics.cpp      — FrameTimingRollingAverage, UpdateGPUMem, AverageFrameMs
#   test_hud.cpp          — MetricsTest HUDTessellation_* tests
#   test_app_input.cpp    — AppInputHandler input handling, mode toggle, depth bias, terminal input
#   test_light_intensity.cpp — LightIntensityPulsing time-based pulsing effect validation
#   test_command_line.cpp — CommandLineTest --timeout parameter parsing validation
# ---------------------------------------------------------------------------
add_executable(tests_unit
    test_matrix_math.cpp
    test_clip_planes.cpp
    test_scene.cpp
    test_ui_system.cpp
    test_metrics.cpp
    test_hud.cpp
    test_app_input.cpp
    test_light_intensity.cpp
    test_command_line.cpp
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
    ${RENDER_SHADER_DIR}/surface.frag
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
# Sources split by theme:
#   test_containment.cpp     — UI pixel containment (Tests 1-4: direct/trad/angle/animation)
#   test_shadow_render.cpp   — Back wall shadow and PCF symmetry (Tests 5-6)
#   test_ui_cube_shadow.cpp  — Animated cube shadow consistency and stability (Tests 7-8)
#   test_scale_render.cpp    — Non-uniform scale and font-size invariance (Tests 9-10)
#   test_perf.cpp            — performance regression tests with skip-if-unset guards
#   test_renderer.cpp        — Renderer initialization, pipelines, descriptor binding tests
#   test_msaa_quality.cpp    — MSAA edge smoothness comparison (Tests 11-12)
#   test_depth_bias.cpp      — Depth bias effectiveness in direct mode (Tests 13-17)
#   test_spotlight_cone.cpp  — Spotlight cone angle attenuation (Tests 18-22)
#   test_static_floor_ceiling.cpp — Floor and ceiling remain static (no ripple effect)
#   test_shadow_acne_walls.cpp    — Shadow acne banding artifacts on walls (Tests ShadowAcneWall_*)
#   test_surface_face_lighting.cpp — Surface NdotL and spotlight cone per-face lighting (Tests SurfaceFaceLighting_*)
# ---------------------------------------------------------------------------
add_executable(tests_render
    test_containment.cpp
    test_shadow_render.cpp
    test_ui_cube_shadow.cpp
    test_scale_render.cpp
    test_perf.cpp
    test_renderer.cpp
    test_msaa_quality.cpp
    test_depth_bias.cpp
    test_spotlight_cone.cpp
    test_pbr.cpp
    test_metallic_materials.cpp
    test_static_floor_ceiling.cpp
    test_wall_normal_mapping.cpp
    test_ambient_shadows.cpp
    test_shadow_acne_walls.cpp
    test_surface_face_lighting.cpp
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
