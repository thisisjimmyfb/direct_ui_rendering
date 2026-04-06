## Instructions
Read ['SPEC.md'](/spec/SPEC.md) and ['direct_ui_rendering.md'](/spec/direct_ui_rendering.md). Then find the most important task from ['Pending Tasks'](#Pending-Tasks), but don't implement it yet. If the task affects the core execution of the demo, then please create tests that will fail without implementing the task. After tests are created, work on the implementation. Afterward, execute all items in the ['Iterate'](#Iterate) section. Then remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit and do not write progress or summary in this file.

## Pending Tasks

- Enable anisotropic filtering on the shadow comparison sampler (`renderer_resources.cpp:255–266`): set `anisotropyEnable = VK_TRUE` and `maxAnisotropy` clamped to `VkPhysicalDeviceLimits::maxSamplerAnisotropy` (query via `vkGetPhysicalDeviceProperties`). The Vulkan spec has no VUID forbidding `anisotropyEnable` alongside `compareEnable`; drivers that honour it will adapt the PCF footprint to the texel-projection aspect ratio on oblique surfaces, directly attacking the barcode pattern. Drivers that ignore it incur no cost.

- Add a `glm::vec3 normal` field to `QuadVertex` (renderer.h) and emit it in `quad.vert` (shaders/quad.vert) as a new output location.
- Update `Renderer::updateCubeSurface` (renderer_resources.cpp) to compute and store the outward face normal for each of the 6 cube faces (same cross-product logic already used in `updateUIShadowCube`).
- Update `surface.frag` to receive the normal, compute NdotL and a spotlight cone factor (matching the approach in shaders/room.frag:128–132), and switch to the slope-scaled 3-arg `sampleShadowPCF`.
- Update `ui_direct.frag` similarly so that lit and shadowed text is consistent with the surface underneath.


## Iterate
- Make sure constants, functions or classes are not duplicated in multiple files. If identical constructs exist in multiple files, consider steps to refactor and share the common construct.
- Run tests by running /scripts/test.sh. If there are any test failures, please investigate and fix the failure if the fix is small. If the fix will be big, please identify tasks to address the problem, and then append the task to the ['Pending Tasks'](#Pending-Tasks).
- Check Vulkan errors by running /scripts/build.sh and execute /build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and fix all Vulkan Validation Layer errors.
- Investigate the pending changes and look for opportunities to refactor to either move common code into a common file or break up files into multiple smaller files if the file contains too many unrelated concepts. Do not break up files if doing so fragments logics that should naturally be co-located.
- Investigate ways to strengthen testing based on pending changes, focus on testing systems from this repo. Consider refactoring the system to accommodate testing if system is important. Add all relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Update ['File Structure'](/CLAUDE.md/#File-Structure) to reflect current project structure.
- If pending changes introduce any conflicts with ['SPEC.md'](/spec/SPEC.md), please update the spec.

## Out of Spec
