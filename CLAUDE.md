read [`SPEC.md`](spec/SPEC.md) and read [`direct_ui_rendering.md`](spec/direct_ui_rendering.md)


## File Structure

```
direct_ui_rendering/
├── .github/
│   └── workflows/
│       └── cmake-multi-platform.yml # CI: build + test on Ubuntu and Windows
├── assets/
│   └── atlas.png                    # Bitmap glyph atlas (fallback when no system font)
├── build/
│   ├── build.sh                     # Build the project
│   ├── commit.sh                    # Stage and commit changes with a generated message
│   ├── ralph.sh                     # Automated iterate-loop runner (Ralph agent)
│   ├── run.sh                       # Run the project
│   └── test.sh                      # Run tests
├── CLAUDE.md                        # Codebase instructions for Claude Code
├── CMakeLists.txt                   # CMake build configuration
├── README.md                        # Project overview and math framework summary
├── spec/
│   ├── direct_ui_rendering.md       # Original math reference
│   ├── LOOP.md                      # Iterate loop task list (active task tracking)
│   └── SPEC.md
├── src/
│   ├── app.h                        # Top-level app: init, frame loop, cleanup
│   ├── app.cpp
│   ├── main.cpp                     # Entry point, window loop, input
│   ├── metrics.h                    # Frame timer, VMA stats, HUD draw
│   ├── metrics.cpp
│   ├── renderer.h                   # Vulkan device, pipelines, render passes
│   ├── renderer.cpp                 # Thin coordinator: includes the three renderer modules
│   ├── renderer_init.cpp            # Device, instance, swapchain, pipelines, render pass setup
│   ├── renderer_recording.cpp       # Per-frame command buffer recording and draw calls
│   ├── renderer_resources.cpp       # Buffer/image allocation, descriptor set updates, VMA wrappers
│   ├── scene.h                      # Room geometry, light, animation matrix
│   ├── scene.cpp
│   ├── ui_system.h                  # Atlas, glyph quads, vertex buffer; SDF constants
│   ├── ui_system.cpp                # stb_truetype SDF atlas generation or PNG fallback
│   ├── ui_surface.h                 # computeSurfaceTransforms(), computeClipPlanes()
│   ├── ui_surface.cpp
│   └── vk_utils.h                   # Thin helpers: image barriers, buffer upload
├── shaders/
│   ├── composite.frag               # Traditional mode: blend UI RT onto teal quad
│   ├── quad.vert                    # Vertex shader for surface/composite quad
│   ├── room.vert                    # Blinn-Phong room vertex shader
│   ├── room.frag                    # Blinn-Phong + PCF shadow fragment shader
│   ├── shadow.vert                  # Depth-only shadow pass vertex shader
│   ├── surface.frag                 # Opaque teal quad (direct mode base layer)
│   ├── ui.frag                      # UI atlas sampling; SDF smoothstep; UI_TEST_COLOR hook
│   ├── ui_direct.frag               # Direct-mode UI frag: SDF + PCF shadow
│   ├── ui_direct.vert               # Direct-mode UI vert: M_total transform + clip distances
│   └── ui_ortho.vert                # Orthographic UI vertex shader (RT pass + metrics)
├── tests/
│   ├── CMakeLists.txt
│   ├── perf_reference.h             # Hardcoded performance regression baselines
│   ├── test_containment.cpp         # tests_render: UI pixel containment check, back wall shadow test, non-uniform-quad-scale clip-plane tracking
│   ├── test_metrics.cpp             # tests_unit: MetricsTest — ring-buffer wrap, HUD tessellation (Direct/Traditional/inputModeStr/null allocator/append-guard/unbuilt-guard/position-bounds/UV-bounds/line-height-spacing/5th-line-separation/all-five-lines-y-spacing/all-five-lines-x-positions/four-lines-y-spacing/four-lines-x-positions/traditional-mode-all-lines-y-spacing/traditional-mode-all-lines-x-positions/traditional-mode-with-input-mode-str-all-five-lines-x-positions/traditional-mode-with-input-mode-str-all-five-lines-y-spacing/non-standard-MSAA-vertex-count/single-digit-MSAA-same-vertex-count/large-MSAA-sample-count-no-buffer-overflow/empty-input-mode-str-five-lines-y-spacing/empty-input-mode-str-lines-x-positions/traditional-mode-empty-input-mode-str-five-lines-y-spacing/traditional-mode-empty-input-mode-str-lines-x-positions), averageFrameMs zero-state/single-frame
│   ├── test_perf.cpp                # tests_render: performance regression
│   ├── test_scene.cpp               # tests_unit: SceneInit, WorldCorners, SceneAnimation (incl. normal-wiggle peak/zero tests), LightFrustum
│   ├── test_sdf.cpp                 # tests_sdf: SDF threshold/render tests (production shaders, real atlas)
│   ├── test_transforms.cpp          # tests_unit: TransformMath (M_us/M_sw/M_total/font-size-invariance), ClipPlane, DepthBias, ShadowBias
│   └── test_ui_system.cpp           # tests_unit: SDFConstants, UISystemUVTable, TessellateString, UISurface
└── CMakeLists.txt
```