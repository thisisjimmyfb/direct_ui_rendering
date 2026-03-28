## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and implement it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github.

## Pending Tasks
- Add unit test `MetricsTest.FrameTimingRollingAverage_WrapsCorrectly`: verify that `Metrics::averageFrameMs()` produces the correct average after the circular history buffer wraps past `HISTORY_SIZE` frames, guarding against off-by-one errors in the ring-buffer index update.
- Add unit test `MetricsTest.HUDTessellation_VertexCountMatchesLineCount`: call `Metrics::tessellateHUD()` and verify the returned vertex count equals `6 * total_chars` for a known RenderMode string, catching missing or duplicated line tessellations.
- Delete `tests/test_math.cpp` — the file is no longer compiled (replaced by `test_transforms.cpp`, `test_scene.cpp`, and `test_ui_system.cpp`) and its presence in the repo is misleading.

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor and break up big files into multiple smaller files. Do not break up files if doing so fragments logics that should naturally be co-located.
- Update ['File Structure'](SPEC.md/#File-Structure) to reflect current project structure.
