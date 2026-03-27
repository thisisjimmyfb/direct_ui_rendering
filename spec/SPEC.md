## 12. File Structure

```
direct_ui_rendering/
├── .github/
│   └── workflows/
│       └── cmake-multi-platform.yml # CI: build + test on Ubuntu and Windows
├── assets/
│   └── atlas.png                    # Bitmap glyph atlas (fallback when no system font)
├── build/
│   ├── build.sh                     # Build the project
│   ├── run.sh                       # Run the project
│   └── test.sh                      # Run tests
├── spec/
│   ├── direct_ui_rendering.md       # Original math reference
│   ├── LOOP.md                      # Iterate loop task list (active task tracking)
│   ├── PROGRESS.md                  # Completed work log
│   └── SPEC.md
├── src/
│   ├── main.cpp                     # Entry point, window loop, input
│   ├── app.h                        # Top-level app: init, frame loop, cleanup
│   ├── app.cpp
│   ├── metrics.h                    # Frame timer, VMA stats, HUD draw
│   ├── metrics.cpp
│   ├── renderer.h                   # Vulkan device, pipelines, render passes
│   ├── renderer.cpp
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
│   ├── test_math.cpp                # tests_unit: matrix construction, clip plane signs
│   ├── test_perf.cpp                # tests_unit: performance regression
│   └── test_containment.cpp         # tests_render: UI pixel containment check
└── CMakeLists.txt
```