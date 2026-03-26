Read spec.md and direct_ui_rendering.md for the background on this project.
Find the most important unmarked task from the following list and implement it. After task completion, mark the task and save the changes to this file.

## Build & Project Structure
- [x] CMakeLists.txt: C++20 project, FetchContent for GLFW, VMA, GLM, stb_image, GoogleTest; validation layers in Debug
- [x] CMakeLists.txt: direct_ui_rendering_lib static library + app executable; glslc shader compilation via add_custom_command
- [x] tests/CMakeLists.txt: tests_unit and tests_render targets linking direct_ui_rendering_lib; tests_render compiles shaders with -DUI_TEST_COLOR
- [x] Source file stubs for all files listed in spec §12
- [ ] Build scripts to build and test the project

## Core Transform Math
- [ ] M_us: scale matrix mapping UI canvas [0,W_ui]×[0,H_ui] to normalized surface space [0,1]×[0,1]
- [ ] M_sw: affine matrix from surface corners P_00/P_10/P_01 mapping normalized surface space to world space
- [ ] M_total = M_wc * M_sw * M_us and M_world = M_sw * M_us (per-frame, given view-projection)
- [ ] Clip planes: four inward-facing world-space planes from surface edges (left, right, top, bottom)

## Vulkan Device & Memory
- [ ] Vulkan instance, debug messenger, physical device selection, logical device, graphics/present queues
- [ ] VMA allocator init; expose total allocated bytes for metrics reporting
- [ ] Image memory barrier helper and one-shot staging buffer upload helper (vk_utils.h)

## Window, Swapchain & Presentation
- [ ] GLFW window creation, Vulkan surface, swapchain with image views
- [ ] RenderTarget abstraction: wraps VkImage+VkImageView; swapchain image in normal mode, plain VkImage in headless
- [ ] Renderer::init(headless=true): skip GLFW surface/swapchain; all pipelines and passes init identically

## Scene Geometry & Lighting
- [ ] Hardcoded room mesh: floor, ceiling, 4 walls as vertex/index data compiled into the binary
- [ ] UI surface quad: local corner geometry (P_00/P_10/P_01/P_11) and looping M_anim(t) animation
- [ ] Directional light: configurable direction hardcoded at startup, lightViewProj for shadow map

## Shadow Mapping
- [ ] Shadow pass: depth-only render pass + framebuffer (D32, 1024×1024) rendered from light POV
- [ ] room.vert/room.frag: Blinn-Phong + sampler2DShadow with 2×2 PCF tap
- [ ] Pipeline barrier: shadow depth DEPTH_STENCIL_WRITE → SHADER_READ before main scene pass

## UI System
- [ ] Glyph atlas: load 512×512 RGBA PNG via stb_image, upload to device-local VkImage (set 2 binding 0)
- [ ] ASCII UV lookup table: character code → UV rect in atlas (fixed 32×32 glyph cell)
- [ ] "Hello World" tessellation: per-glyph quads with inUIPos + inUITexCoord; upload once to device-local vertex buffer

## Shaders
- [ ] ui_direct.vert: M_total transform, depth bias, gl_ClipDistance[4] from precomputed clip planes
- [ ] ui_ortho.vert: push constant ortho matrix (used by RT pass and metrics overlay)
- [ ] ui.frag: atlas texture sample + #ifdef UI_TEST_COLOR magenta override for render tests
- [ ] composite.frag: sample offscreen UI RT (set 2 binding 1) onto surface quad
- [ ] quad.vert: passthrough with UVs for composite draw

## Render Passes & Pipelines
- [ ] SceneUBO and SurfaceUBO struct definitions and per-frame host-visible buffer allocation
- [ ] Descriptor set layouts for sets 0/1/2 per spec §6.3; descriptor pool and allocation
- [ ] MSAA main scene pass: 4x color + depth attachments with resolve-to-swapchain subpass
- [ ] Metrics overlay pass: render pass targeting swapchain image, no depth attachment
- [ ] pipe_room: Blinn-Phong room geometry with depth test
- [ ] pipe_ui_direct: direct-mode UI with clip distance enable and pre-multiplied alpha blend
- [ ] pipe_ui_rt: orthographic UI into offscreen RT (1x MSAA, alpha blend)
- [ ] pipe_composite: samples offscreen RT onto surface quad with alpha blend
- [ ] pipe_metrics: orthographic HUD reusing ui_ortho.vert / ui.frag

## Traditional Rendering Mode
- [ ] Offscreen UI RT: RGBA8 512×128 VkImage, allocated lazily on first toggle to traditional mode
- [ ] UI RT pass: render glyph quads into offscreen RT via pipe_ui_rt
- [ ] Image barrier: color attachment WRITE → SHADER_READ before main scene pass samples the RT
- [ ] Composite draw: surface quad rendered with pipe_composite sampling the UI RT

## Direct Rendering Mode
- [ ] Per-frame SurfaceUBO update: compute M_total, M_world, clip planes from M_anim(t) and current VP
- [ ] Glyph quads drawn directly in main scene pass via pipe_ui_direct (no offscreen RT)
- [ ] Depth bias: DEPTH_BIAS_DEFAULT = 0.0001; adjustable at runtime via +/- keys (print to stdout)

## Frame Loop & Synchronization
- [ ] Per-frame SceneUBO update: view/proj from camera, light params, lightViewProj
- [ ] Command buffer recording order: shadow → (UI RT if traditional) → main scene → metrics overlay
- [ ] Swapchain acquire/present semaphores; pipeline stage barriers per spec §6.6
- [ ] Mode toggle on Space: takes effect at frame start; RT allocated on demand

## Metrics Overlay
- [ ] CPU frame timer: std::chrono rolling average over last 60 frames
- [ ] VMA total allocated bytes queried each frame
- [ ] HUD tessellation: format Mode/Frame/GPU Mem/MSAA strings into glyph quads each frame
- [ ] HUD drawn via pipe_metrics in metrics overlay pass (top-left, one line per field)

## Testing
- [ ] tests/perf_reference.h: TBD placeholder values for GPU mem and frame time in both modes
- [ ] test_math.cpp: M_us, M_sw, M_total map corners to expected world positions (identity VP)
- [ ] test_math.cpp: clip plane signs — inside points all ≥ 0, outside ≥ 1 negative, boundary ~0 (±1e-5)
- [ ] test_perf.cpp: N headless frames; assert frame time and GPU mem ≤ reference × (1 + tolerance)
- [ ] test_containment.cpp: one direct-mode headless frame; readback; all magenta pixels inside screen-space quad (2px margin)
