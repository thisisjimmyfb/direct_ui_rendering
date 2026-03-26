Read spec.md and direct_ui_rendering.md for the background on this project.
Find the most important unmarked task from the following list and implement it. After task completion, remove the task and save this file.

## Known Issues

## Completed

- [x] Fixed Vulkan validation error: `m_renderFinished` semaphore was reused before the swapchain presentation engine was done with it. Changed from a single semaphore to a per-swapchain-image `std::vector<VkSemaphore>`. `getRenderFinishedSemaphore()` now takes `imageIndex`.
- [x] Generated glyph atlas `assets/atlas.png` (512×512 RGBA, 32×32 cells, ASCII 32–126, Courier New font). Added CMake `POST_BUILD` step to copy it to `$<TARGET_FILE_DIR>/assets/atlas.png` so the app finds it at runtime.

## Iterate (do not mark or remove tasks in this section after completion)
- [ ] run build/test.sh and update this doc with more tasks based on output
  - **2026-03-26**: All 17/17 tests pass (12 unit math/clip-plane tests + 1 render containment test + 4 perf tests). No failures.
- [ ] run build/build.sh and execute build/Debug/direct_ui_rendering.exe and update this doc with more tasks based on output
  - **2026-03-26**: Debug build succeeds cleanly. App launches and runs without crash. No stdout/stderr output. Key issue found: missing atlas PNG (see Known Issues).