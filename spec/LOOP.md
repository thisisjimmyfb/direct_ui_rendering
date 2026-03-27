## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and implement it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github.

## Pending Tasks
- Add render test: back wall not self-shadowed — In `test_containment.cpp` (or a new `test_shadow.cpp`), add a headless render test that positions the camera facing the back wall, renders one frame, reads back the back wall region (~centre strip), and asserts that the mean luminance of those pixels exceeds `ambientColor + 0.1` (proving that at least some diffuse light contribution reaches the wall, i.e., the depth bias prevents total self-shadowing).
- Add unit test for slope-scaled shadow bias formula — In `test_math.cpp`, add a `ShadowBias` test suite that validates the bias formula `max(0.005 * (1.0 - dot(N, L)), 0.001)`: assert the back wall case (N·L = 0.0) yields bias ≈ 0.005, the floor case (N·L = 1.0) yields bias ≈ 0.001 (the minimum clamp), and an intermediate grazing angle (N·L = 0.5) yields bias ≈ 0.0025 + 0.001 = 0.0035. These are pure arithmetic checks with no Vulkan context required.
- Add render test: PCF shadow symmetry — Render a scene with the light at a known position casting a hard shadow edge across the floor. Sample luminance at two points equidistant from the shadow edge (one lit, one shadowed). Assert the PCF penumbra is symmetric: the lit sample and shadowed sample differ from the edge value by the same amount (±10%). This validates the centered `{-0.5, 0.5}` kernel produces no directional bias.
- Add unit test for tight light frustum — In `test_math.cpp`, add a `LightFrustum` test suite that calls `Scene::lightViewProj()` on a default `Scene` (after `init()`), transforms all 8 room corners through the result, and asserts each NDC x and y coordinate falls within `[-1, 1]`. Also assert the frustum half-extents in light view space are tighter than the old ±5 m fixed bound (i.e., the computed AABB half-width in X or Y is less than 4.0). This validates the dynamic frustum derivation from room corners.

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor and break up big files into multiple smaller files. Do not break up files if doing so fragments logics that should naturally be co-located.
- Update ['SPEC.md'](SPEC.md)'s ['File Structure'](SPEC.md/#File-Structure) section to reflect current project structure.
