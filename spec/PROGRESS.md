Read spec.md and direct_ui_rendering.md for the background on this project.
Find the most important unmarked task from the following list and implement it. After task completion, remove the task and save this file.

## Known Issues

- [ ] **Missing glyph atlas** — `assets/atlas.png` does not exist. `UISystem::init()` falls back to a 1×1 white placeholder (`ui_system.cpp:45`), so "Hello World" UI text renders as blank white rectangles instead of actual glyphs. Need to generate a 512×512 RGBA PNG atlas with 32×32 glyph cells for ASCII 32–126 (row-major, 16 glyphs per row) and either embed it as a C array or add a CMake step to copy it to `build/Debug/assets/atlas.png` alongside the binary.

## Completed

- [x] Fixed Vulkan validation error: `m_renderFinished` semaphore was reused before the swapchain presentation engine was done with it. Changed from a single semaphore to a per-swapchain-image `std::vector<VkSemaphore>`. `getRenderFinishedSemaphore()` now takes `imageIndex`.

## Iterate (do not remove tasks in this section after completion)
- [x] run build/test.sh and update this doc with more tasks based on output
  - **2026-03-26**: All 17/17 tests pass (12 unit math/clip-plane tests + 1 render containment test + 4 perf tests). No failures.
- [x] run build/build.sh and execute build/Debug/direct_ui_rendering.exe and update this doc with more tasks based on output
  - **2026-03-26**: Debug build succeeds cleanly. App launches and runs without crash. No stdout/stderr output. Key issue found: missing atlas PNG (see Known Issues).