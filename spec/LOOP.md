## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and implement it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github and do not write progress updates in this file.

## Pending Tasks
- Refactor `src/renderer.cpp` (≈2086 lines): split into `renderer_init.cpp` (device/pipeline/swapchain setup), `renderer_recording.cpp` (per-frame command-buffer recording), and `renderer_resources.cpp` (GPU resource management), keeping `renderer.cpp` as the thin orchestration layer. Only proceed if the split does not fragment logic that is naturally co-located.
- Add unit test `MetricsTest.HUDTessellation_TraditionalMode_WithInputModeStr_AllFiveLinesYSpacing`: with `RenderMode::Traditional` and a non-null `inputModeStr`, verify that all five lines follow `y = leftMargin + i * lineHeight` and that successive pairs differ by exactly `lineHeight`; mirrors `WithInputModeStr_AllFiveLinesYSpacing` for the Traditional branch and completes the 5-line y-spacing coverage matrix alongside `TraditionalMode_AllLinesYSpacing`.

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor and break up big files into multiple smaller files. Do not break up files if doing so fragments logics that should naturally be co-located.
- Update ['File Structure'](SPEC.md/#File-Structure) to reflect current project structure.
