read [`SPEC.md`](spec/SPEC.md) and read [`direct_ui_rendering.md`](spec/direct_ui_rendering.md)


## File Structure

```
direct_ui_rendering/
├── .github/
│   └── workflows/
│       └── cmake-multi-platform.yml  # CI: build + test on Ubuntu and Windows
├── assets/
│   └── atlas.png                     # Bitmap glyph atlas (fallback when no system font)
├── build/
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
│   ├── app.cpp
│   ├── main.cpp                      # Entry point: window creation, event loop, input handling
│   ├── metrics.h                     # Metrics tracking: frame timer, VMA stats, HUD rendering
│   ├── metrics.cpp
│   ├── renderer.h                    # Renderer class declaration: Vulkan device, pipelines, render passes
│   ├── renderer.cpp                  # Thin coordinator: includes the three renderer modules
│   ├── renderer_init.cpp             # Device, instance, swapchain, pipelines, render pass setup
│   ├── renderer_recording.cpp        # Per-frame command buffer recording and draw calls
│   ├── renderer_resources.cpp        # Buffer/image allocation, descriptor set updates, VMA wrappers
│   ├── scene.h                       # Scene class: room geometry, light, animation matrix
│   ├── scene.cpp
│   ├── ui_system.h                   # UISystem class: atlas, glyph quads, vertex buffer, SDF constants
│   ├── ui_system.cpp                 # stb_truetype SDF atlas generation or PNG fallback
│   ├── ui_surface.h                  # UI surface transforms: computeSurfaceTransforms(), computeClipPlanes()
│   ├── ui_surface.cpp
│   └── vk_utils.h                    # Thin Vulkan helpers: image barriers, buffer upload utilities
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
│   ├── perf_reference.h              # Hardcoded performance regression baselines
│   ├── test_containment.cpp          # tests_render: UI pixel containment, back wall shadow, clip-plane tracking
│   ├── test_metrics.cpp              # tests_unit: MetricsTest — ring buffer, HUD tessellation, frame timing
│   ├── test_perf.cpp                 # tests_render: performance regression tests
│   ├── test_scene.cpp                # tests_unit: SceneInit, WorldCorners, SceneAnimation, LightFrustum
│   ├── test_sdf.cpp                  # tests_sdf: SDF threshold/render tests with production shaders
│   ├── test_transforms.cpp           # tests_unit: TransformMath, ClipPlane, ClipPlane3D, ClipPlaneSymmetry
│   └── test_ui_system.cpp            # tests_unit: SDFConstants, UISystemUVTable, TessellateString, UISurface
└── CMakeLists.txt                    # tests/ subdirectory CMake configuration
```