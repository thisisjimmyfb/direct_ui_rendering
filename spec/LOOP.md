## Instructions
- Read ['SPEC.md'](/spec/SPEC.md) and ['direct_ui_rendering.md'](/spec/direct_ui_rendering.md).
- Find the most important task from ['Pending Tasks'](#Pending-Tasks) and work on it using the best decision based on the specifications. If the task requires changing the specification, move the task under ['Out of Spec'](#Out-of-Spec) instead and find the next most important task and repeat.
- Afterward, execute all items in the ['Iterate'](#Iterate) section.
- Remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github and do not write progress or summary in this file.

## Pending Tasks

## Iterate
- run test.sh, read the output and investigate any problems and identify tasks to address the problem, then append the task to the pending tasks section
- run build.sh and execute /build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes, focus on testing systems introduced by this project. Consider refactoring the system to accommodate testing if system is important. Add all relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor and break up files into multiple smaller files if the file contains too many unrelated concepts. Do not break up files if doing so fragments logics that should naturally be co-located.
- Update ['File Structure'](/CLAUDE.md/#File-Structure) to reflect current project structure.

## Out of Spec
- Refactor: split `renderer_init.cpp` — completed. Extracted pipeline creation into `renderer_pipelines.cpp` and render pass definitions into `renderer_renderpasses.cpp`.
- Bug fix: Added `m_setLayout2` to pipeline layout to fix descriptor set mismatch errors in tests.
- Completed all edge case tests:
  - `Scene::worldCorners` with `scaleW=0.0f` — documents NaN/finite behavior with zero-length edge vectors
  - `UISystem::tessellateString` with 1000+ character strings — verifies no integer overflow in uint32_t vertex count
  - `computeClipPlanes` with collinear corners — documents zero normal behavior
  - `computeM_us` with zero canvas dimensions (W_ui=0, H_ui=0) — documents infinity behavior
  - `computeM_sw` with zero-length edges (P_10==P_00, P_01==P_00) — documents zero column behavior
