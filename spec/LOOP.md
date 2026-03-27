## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and implement it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github.

## Pending Tasks
- Add render test: back wall not self-shadowed — In `test_containment.cpp` (or a new `test_shadow.cpp`), add a headless render test that positions the camera facing the back wall, renders one frame, reads back the back wall region (~centre strip), and asserts that the mean luminance of those pixels exceeds `ambientColor + 0.1` (proving that at least some diffuse light contribution reaches the wall, i.e., the depth bias prevents total self-shadowing).
- Add render test: PCF shadow symmetry — Render a scene with the light at a known position casting a hard shadow edge across the floor. Sample luminance at two points equidistant from the shadow edge (one lit, one shadowed). Assert the PCF penumbra is symmetric: the lit sample and shadowed sample differ from the edge value by the same amount (±10%). This validates the centered `{-0.5, 0.5}` kernel produces no directional bias.
- Add unit test for Scene::init() mesh integrity — In `test_math.cpp`, add a `SceneInit` test suite that calls `Scene::init()` and verifies: `roomMesh().vertices.size() > 0`, all vertex normals are unit-length (|n| ≈ 1.0 ± 0.001), all index values are in bounds (< vertices.size()), and the total triangle count equals 12 (6 quads × 2 triangles). These are pure CPU checks with no Vulkan context required.
- Add unit test for Scene::worldCorners() consistency — In `test_math.cpp`, add a `WorldCorners` test that calls `scene.worldCorners(0.0f, ...)` and asserts: `P_11 = P_00 + (P_10 - P_00) + (P_01 - P_00)` (parallelogram identity), and that the four world corners match the local surface corners transformed by `animationMatrix(0.0f)`. This validates the corner transform chain with no Vulkan context required.

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor and break up big files into multiple smaller files. Do not break up files if doing so fragments logics that should naturally be co-located.
- Update ['SPEC.md'](SPEC.md)'s ['File Structure'](SPEC.md/#File-Structure) section to reflect current project structure.
