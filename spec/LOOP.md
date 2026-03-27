## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and implement it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github.

## Pending Tasks
- Add render test: back wall not self-shadowed — In `test_containment.cpp` (or a new `test_shadow.cpp`), add a headless render test that positions the camera facing the back wall, renders one frame, reads back the back wall region (~centre strip), and asserts that the mean luminance of those pixels exceeds `ambientColor + 0.1` (proving that at least some diffuse light contribution reaches the wall, i.e., the depth bias prevents total self-shadowing).
- Add render test: PCF shadow symmetry — Render a scene with the light at a known position casting a hard shadow edge across the floor. Sample luminance at two points equidistant from the shadow edge (one lit, one shadowed). Assert the PCF penumbra is symmetric: the lit sample and shadowed sample differ from the edge value by the same amount (±10%). This validates the centered `{-0.5, 0.5}` kernel produces no directional bias.
- Add unit test for `animationMatrix(0)` purity — In `test_math.cpp`, add a `SceneAnimation` test that calls `scene.animationMatrix(0.0f)` and asserts the rotation submatrix is identity (M[0][0]=M[1][1]=M[2][2]=1, off-diagonal rotation entries=0), confirming that at t=0 the matrix is a pure translation with no rotation. This validates the `sin(0)=0` base case of the animation.
- Add unit test for surface planarity preservation — In `test_math.cpp`, add a `WorldCorners` test that verifies all four world corners returned by `worldCorners(t, ...)` are coplanar at multiple t values: compute the surface normal as `normalize((P10-P00) × (P01-P00))` and assert `|dot(normal, P11-P00)| < 1e-4`. Since the animation is a rigid-body transform, planarity must always hold.

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor and break up big files into multiple smaller files. Do not break up files if doing so fragments logics that should naturally be co-located.
- Update ['SPEC.md'](SPEC.md)'s ['File Structure'](SPEC.md/#File-Structure) section to reflect current project structure.
