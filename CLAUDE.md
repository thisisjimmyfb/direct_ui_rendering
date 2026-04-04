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
│   ├── app.cpp                       # App implementation: window, event loop, render loop (519 lines)
│   ├── main.cpp                      # Entry point: window creation, event loop, input handling (22 lines)
│   ├── metrics.h                     # Metrics tracking: frame timer, VMA stats, HUD rendering
│   ├── metrics.cpp                   # Metrics implementation (105 lines)
│   ├── renderer.h                    # Renderer class declaration: Vulkan device, pipelines, render passes
│   ├── renderer.cpp                  # Thin coordinator: includes the four renderer modules (6 lines)
│   ├── renderer_init.cpp             # Device, instance, swapchain, render pass setup (475 lines)
│   ├── renderer_pipelines.cpp        # Pipeline creation: createPipelines() (471 lines)
│   ├── renderer_renderpasses.cpp     # Render pass definitions: createRenderPasses() (223 lines)
│   ├── renderer_recording.cpp        # Per-frame command buffer recording and draw calls (238 lines)
│   ├── renderer_resources.cpp        # Buffer/image allocation, descriptor set updates, VMA wrappers (854 lines)
│   ├── scene.h                       # Scene class: room geometry, light, animation matrix, UISurface (6 faces)
│   ├── scene.cpp                     # Scene implementation (158 lines)
│   ├── ui_system.h                   # UISystem class: atlas, glyph quads, vertex buffer, SDF constants
│   ├── ui_system.cpp                 # stb_truetype SDF atlas generation or PNG fallback (357 lines)
│   ├── ui_surface.h                  # UI surface transforms: computeSurfaceTransforms(), computeClipPlanes(), computeFaceTransforms()
│   ├── ui_surface.cpp                # UI surface transform calculations (100 lines)
│   └── vk_utils.h                    # Thin Vulkan helpers: image barriers, buffer upload utilities (102 lines)
├── shaders/
│   ├── composite.frag                # Traditional mode: blend UI render target onto teal quad
│   ├── quad.vert                     # Vertex shader for surface/composite quad geometry
│   ├── room.vert                     # Blinn-Phong room geometry vertex shader
│   ├── room.frag                     # Blinn-Phong + PCF shadow fragment shader
│   ├── shadow.vert                   # Depth-only shadow pass vertex shader
│   ├── surface.frag                  # Opaque teal quad fragment shader (direct mode base layer)
│   ├── ui.frag                       # UI atlas sampling with SDF smoothstep
│   ├── ui_direct.frag                # Direct-mode UI fragment: SDF + PCF shadow
│   ├── ui_direct.vert                # Direct-mode UI vertex: M_total transform + clip distances
│   └── ui_ortho.vert                 # Orthographic UI vertex shader (RT pass + metrics overlay)
├── tests/
│   ├── CMakeLists.txt                # Test target configuration (tests_unit, tests_render, tests_sdf)
│   ├── containment_fixture.h         # Shared ContainmentTest fixture + helpers for render tests
│   ├── perf_reference.h              # Hardcoded performance regression baselines
│   ├── test_clip_planes.cpp          # tests_unit: ClipPlane, ClipPlaneTilted, ClipPlane3D, ClipPlaneSymmetry, ClipPlaneYRotated, ClipPlane3DParallelogram (6 tests)
│   ├── test_containment.cpp          # tests_render: UI pixel containment in direct and traditional modes (4 tests)
│   ├── test_hud.cpp                  # tests_unit: MetricsTest HUDTessellation — vertex counts, positions, spacing
│   ├── test_matrix_math.cpp          # tests_unit: TransformMath (M_us, M_sw, M_total), FontSizeInvariance, Parallelogram
│   ├── test_metrics.cpp              # tests_unit: frame timing ring buffer and GPU memory tracking
│   ├── test_perf.cpp                 # tests_render: performance regression tests
│   ├── test_scale_render.cpp         # tests_render: non-uniform scale clip plane tracking and font-size invariance (2 tests)
│   ├── test_scene.cpp                # tests_unit: SceneInit, WorldCorners, WorldCubeCorners (7 tests), SceneAnimation, LightFrustum, UISurface (6 faces)
│   ├── test_sdf.cpp                  # tests_sdf: SDF threshold/render tests with production shaders
│   ├── test_shadow_render.cpp        # tests_render: back wall self-shadow, PCF kernel symmetry, UI quad shadow casting (3 tests)
│   └── test_ui_system.cpp            # tests_unit: SDFConstants, UISystemUVTable, TessellateString, UISurface
```