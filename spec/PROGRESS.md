Read spec.md and direct_ui_rendering.md for the background on this project.
Find the most important unmarked task from the following list and implement it. After task completion, remove the task and save the changes to this file.

## Known Issues
- [ ] **Critical: Render pass initialization order bug** — `createFramebuffers()` is called from within `createSwapchain()` (line 658), which is invoked before `createRenderPasses()` in `init()`. This causes framebuffers to be created with `m_mainPass` and `m_metricsPass` still set to `VK_NULL_HANDLE`, triggering: `vkCreateFramebuffer(): pCreateInfo->renderPass is VK_NULL_HANDLE.`
  - **Location**: `renderer.cpp:658` calls `createFramebuffers()` before `renderer.cpp:46` calls `createRenderPasses()`
  - **Fix needed**: Restructure initialization so render passes are created before any framebuffers. Either: (a) move framebuffer creation to after `createRenderPasses()` in `init()`, or (b) have `createSwapchain()` only create swapchain images and views, deferring framebuffer creation to a separate call after render passes exist.

## Iterate (do not remove tasks in this section after completion)
- [ ] run build/test.sh and update this doc with more tasks based on output
- [ ] run build/build.sh and execute build/Debug/direct_ui_rendering.exe and update this doc with more tasks based on output