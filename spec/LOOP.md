## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and implement it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github.

## Pending Tasks
- Add unit test `UISystemTest.TessellateString_EmptyString`: verify that `tessellateString("", ...)` returns 0 vertices and does not write to the output buffer, guarding against buffer-size math errors on empty input.
- Add unit test `UISystemTest.TessellateString_NonPrintableChampsFallBackToSpace`: verify that characters outside the printable ASCII range (e.g. `\t`, `\n`, code 0, code 127) produce the same quad UVs as the space glyph, guarding the lookup-table bounds check.
- Add unit test `SceneAnimationTest.WorldCorners_P11_AffineInvariance`: verify that `P_11 == P_00 + (P_10 - P_00) + (P_01 - P_00)` for several values of t, guarding the affine-frame invariant that `worldCorners` must satisfy.
- Add unit test `SceneLightTest.LightViewProj_ContainsAllRoomCorners`: transform all 8 room corners by `lightViewProj()` and verify each NDC coordinate falls in [-1, 1], ensuring no room geometry is clipped by the shadow frustum.

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor and break up big files into multiple smaller files. Do not break up files if doing so fragments logics that should naturally be co-located.
- Update ['File Structure'](SPEC.md/#File-Structure) to reflect current project structure.
