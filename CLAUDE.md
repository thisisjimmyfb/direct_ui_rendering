read [`SPEC.md`](spec/SPEC.md) and read [`direct_ui_rendering.md`](spec/direct_ui_rendering.md)


## File Structure

```
direct_ui_rendering/
├── .github/
│   └── workflows/
│       └── cmake-multi-platform.yml  # CI: build + test on Ubuntu and Windows
├── assets/
│   └── atlas.png                     # Bitmap glyph atlas (fallback when no system font)
├── scripts/
│   ├── build.sh                      # Build the project (cmake + ninja/msbuild)
│   ├── commit.sh                     # Stage and commit changes with a generated message
│   ├── ralph.sh                      # Automated iterate-loop runner (Ralph agent)
│   ├── run.sh                        # Run the project
│   └── test.sh                       # Run tests (ctest)
├── CLAUDE.md                         # Codebase instructions for Claude Code
├── CMakeLists.txt                    # Root CMake configuration
├── README.md                         # Project overview and math framework summary
├── spec/
│   ├── direct_ui_rendering.md        # Mathematical framework for direct UI rendering
│   ├── LOOP.md                       # Iterate loop task list (active task tracking)
│   └── SPEC.md                       # Project specification
├── src/
│   ├── app.h                         # Top-level App class: init, frame loop, cleanup
│   ├── app.cpp                       # App implementation: window, event loop, render loop (521 lines)
│   ├── main.cpp                      # Entry point: window creation, event loop, input handling (23 lines)
│   ├── shader_uniforms.h             # GPU-side uniform buffer structs: SceneUBO, SurfaceUBO
│   ├── metrics.h                     # Metrics tracking: frame timer, VMA stats, HUD rendering
│   ├── metrics.cpp                   # Metrics implementation (105 lines)
│   ├── renderer.h                    # Renderer class declaration: Vulkan device, pipelines, render passes
│   ├── renderer.cpp                  # Thin coordinator: includes the four renderer modules (6 lines)
│   ├── renderer_init.cpp             # Device, instance, swapchain, render pass setup (475 lines)
│   ├── renderer_pipelines.cpp        # Pipeline creation: createPipelines() (471 lines)
│   ├── renderer_renderpasses.cpp     # Render pass definitions: createRenderPasses() (224 lines)
│   ├── renderer_recording.cpp        # Per-frame command buffer recording and draw calls (238 lines)
│   ├── renderer_resources.cpp        # Buffer/image allocation, descriptor set updates, VMA wrappers (860 lines)
│   ├── scene.h                       # Scene class: room geometry, light, animation matrix, UISurface (6 faces)
│   ├── scene.cpp                     # Scene implementation with oscillating animation (163 lines)
│   ├── ui_system.h                   # UISystem class: atlas, glyph quads, vertex buffer, SDF constants
│   ├── ui_system.cpp                 # stb_truetype SDF atlas generation or PNG fallback (357 lines)
│   ├── ui_surface.h                  # UI surface transforms: computeSurfaceTransforms(), computeClipPlanes(), computeFaceTransforms()
│   ├── ui_surface.cpp                # UI surface transform calculations (100 lines)
│   └── vk_utils.h                    # Thin Vulkan helpers: image barriers, buffer upload utilities (102 lines)
├── shaders/
│   ├── composite.frag                # Traditional mode: blend UI render target onto teal quad
│   ├── quad.vert                     # Vertex shader for surface/composite quad geometry
│   ├── room.vert                     # PBR room geometry vertex shader (transforms, material properties)
│   ├── room.frag                     # PBR (Cook-Torrance BRDF) with metallic/roughness properties + PCF shadow; Fresnel-Schlick approximation; GGX distribution; procedural roughness variation (normal map simulation); material variety
│   ├── shadow.vert                   # Depth-only shadow pass vertex shader
│   ├── surface.frag                  # Opaque teal quad fragment shader (direct mode base layer)
│   ├── ui.frag                       # UI atlas sampling with SDF smoothstep (white text) or bitmap mode
│   ├── ui_direct.frag                # Direct-mode UI fragment: SDF smoothstep, lighting model (ambient + spotlight), PCF shadow
│   ├── ui_direct.vert                # Direct-mode UI vertex: M_total transform, clip distances, shadow coordinates
│   └── ui_ortho.vert                 # Orthographic UI vertex shader (RT pass + metrics overlay)
├── tests/
│   ├── CMakeLists.txt                # Test target configuration (tests_unit, tests_render, tests_sdf)
│   ├── containment_fixture.h         # Shared ContainmentTest fixture + helpers for render tests
│   ├── perf_reference.h              # Hardcoded performance regression baselines
│   ├── test_app_input.cpp            # tests_unit: App input handling (key callbacks, mode toggle, depth bias edge cases, terminal input, cursor display, mouse) (74 tests)
│   ├── test_clip_planes.cpp          # tests_unit: ClipPlane, ClipPlaneTilted, ClipPlane3D, ClipPlaneSymmetry, ClipPlaneYRotated, ClipPlane3DParallelogram (6 tests)
│   ├── test_command_line.cpp         # tests_unit: CommandLineTest --timeout parameter parsing (10 tests)
│   ├── test_containment.cpp          # tests_render: UI pixel containment in direct and traditional modes (4 tests)
│   ├── test_depth_bias.cpp           # tests_render: depth bias effectiveness in direct mode (5 tests)
│   ├── test_hud.cpp                  # tests_unit: MetricsTest HUDTessellation — vertex counts, positions, spacing
│   ├── test_light_intensity.cpp      # tests_unit: LightIntensityPulsing time-based light animation validation (12 tests)
│   ├── test_matrix_math.cpp          # tests_unit: TransformMath (M_us, M_sw, M_total), FontSizeInvariance, Parallelogram
│   ├── test_metrics.cpp              # tests_unit: frame timing ring buffer and GPU memory tracking
│   ├── test_msaa_quality.cpp         # tests_render: MSAA edge smoothness comparison (direct vs traditional modes) (2 tests)
│   ├── test_metallic_materials.cpp   # tests_render: metallic material definitions, rendering validation (8 tests)
│   ├── test_pbr.cpp                  # tests_render: PBR material properties, roughness variation, rendering validation (10 tests)
│   ├── test_perf.cpp                 # tests_render: performance regression tests
│   ├── test_renderer.cpp             # tests_render: Renderer initialization, headless RT, UBO updates, geometry updates, render passes, pipelines, descriptor binding (30 tests)
│   ├── test_scale_render.cpp         # tests_render: non-uniform scale clip plane tracking and font-size invariance (2 tests)
│   ├── test_scene.cpp                # tests_unit: SceneInit, WorldCorners (animation continuity, extreme aspect ratios), WorldCubeCorners, SceneAnimation, LightFrustum, UISurface (63 tests)
│   ├── test_sdf.cpp                  # tests_sdf: SDF threshold/render tests with production shaders
│   ├── test_shadow_render.cpp        # tests_render: back wall self-shadow, PCF kernel symmetry, UI cube shadow casting (3 tests)
│   ├── test_spotlight_cone.cpp       # tests_render: spotlight cone angle attenuation (5 tests)
│   ├── test_ui_cube_shadow.cpp       # tests_render: animated UI cube shadow consistency, extreme rotation stability (2 tests)
│   └── test_ui_system.cpp            # tests_unit: SDFConstants, UISystemUVTable, TessellateString, UISurface
```