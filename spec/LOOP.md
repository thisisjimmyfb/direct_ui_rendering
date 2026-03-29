## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and implement it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github and do not write progress updates in this file.

## Pending Tasks
- Add test for computeClipPlanes with a Y-axis-rotated 3D surface in test_clip_planes.cpp (current ClipPlane3DTest only covers Z-translation; a Y-rotated surface exercises the full 3D normal computation path).
- Add test for M_sw correctness with a non-orthogonal parallelogram surface (e_u and e_v not perpendicular) in test_matrix_math.cpp, verifying all four corners map correctly.

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor and break up files into multiple smaller files if the file contains too many unrelated concepts. Do not break up files if doing so fragments logics that should naturally be co-located.
- Update ['File Structure'](../CLAUDE.md/#File-Structure) to reflect current project structure.
