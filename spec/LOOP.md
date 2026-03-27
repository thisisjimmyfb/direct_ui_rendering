## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and implement it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github.

## Pending Tasks
- Add render test: back wall not self-shadowed — In `test_containment.cpp` (or a new `test_shadow.cpp`), add a headless render test that positions the camera facing the back wall, renders one frame, reads back the back wall region (~centre strip), and asserts that the mean luminance of those pixels exceeds `ambientColor + 0.1` (proving that at least some diffuse light contribution reaches the wall, i.e., the depth bias prevents total self-shadowing).
- Add render test: PCF shadow symmetry — Render a scene with the light at a known position casting a hard shadow edge across the floor. Sample luminance at two points equidistant from the shadow edge (one lit, one shadowed). Assert the PCF penumbra is symmetric: the lit sample and shadowed sample differ from the edge value by the same amount (±10%). This validates the centered `{-0.5, 0.5}` kernel produces no directional bias.
- Add unit test for `animationMatrix(t)` negative lateral peak — In `test_math.cpp`, add a `SceneAnimationTest` case at t=(3π/2)/0.18 so that `sin(t*0.18) = sin(3π/2) = -1`, giving `lateralX = -1.2`. Assert `M[3][0] ≈ -1.2` and `M[3][1] == 1.5 + 0.35*sin(t*0.22)` to 1e-5 tolerance. This covers the negative-peak oscillation branch (the mirror of the `AtSinPiOver2` test).
- Add unit test for `lightViewProj` NDC Z depth coverage — In `test_math.cpp`, extend `LightFrustumTest` with a case that projects all 8 room corners through `scene.lightViewProj()` and asserts each NDC z (`clip.z / clip.w`) lies in `[0, 1]` (Vulkan depth range). The existing tests only verify x/y extents; a corner with z outside [0,1] would be clipped from the shadow map, causing missing shadows.

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor and break up big files into multiple smaller files. Do not break up files if doing so fragments logics that should naturally be co-located.
- Update ['SPEC.md'](SPEC.md)'s ['File Structure'](SPEC.md/#File-Structure) section to reflect current project structure.
