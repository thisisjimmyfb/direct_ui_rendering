## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and implement it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github and do not write progress updates in this file.

## Pending Tasks
- Add test for M_total (computeSurfaceTransforms) with a parallelogram surface using a real perspective VP matrix in test_matrix_math.cpp, verifying all four UI-space corners map to the correct NDC positions.
- Add test for computeClipPlanes with a 3D parallelogram (non-zero Z AND non-orthogonal e_u/e_v) in test_clip_planes.cpp — existing fixtures cover these properties separately but never combined.
- Add font-size invariance test for a parallelogram (skewed) surface in test_matrix_math.cpp, verifying that proportional canvas+quad scaling preserves world position when e_u ∦ e_v.
- Add test for M_sw with a 3D parallelogram (non-zero Z + non-orthogonal e_u/e_v) in test_matrix_math.cpp, verifying all four corners — existing Z-depth and parallelogram tests cover these properties separately.

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor and break up files into multiple smaller files if the file contains too many unrelated concepts. Do not break up files if doing so fragments logics that should naturally be co-located.
- Update ['File Structure'](../CLAUDE.md/#File-Structure) to reflect current project structure.
