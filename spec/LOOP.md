## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and work on it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github and do not write progress or summary in this file.

## Pending Tasks
- Update all comments to reflect what the code is actually doing.
- Add test: `Scene::worldCorners` with `scaleW=0.0f` — document and validate the behavior (NaN or crash) when edge vectors are exactly zero-length, establishing a baseline before any guard is added. Guards against regressions if a clamp is later introduced.
- Add test: `UISystem::tessellateString` with a very long string (1000+ characters) — verify no integer overflow in the returned `uint32_t` vertex count and that `outVerts.size()` equals the returned count.
- Add test: `computeClipPlanes` with collinear corners (P_00, P_10, P_01 on the same line) — the cross product is zero so the surface normal is undefined; document whether the function produces NaN, zero, or well-defined clip planes as a regression baseline.
- Add test: `computeM_us` with zero canvas dimension (W_ui=0) — the matrix entry 1/W_ui becomes infinity; document the behavior as a baseline before any guard is added.
- Add test: `computeM_sw` with zero-length `e_u` (P_10 == P_00) — basis is degenerate; document whether the resulting matrix produces NaN/inf column or a zero column, as a regression baseline before any guard is added.
- Add test: `computeM_sw` with zero-length `e_v` (P_01 == P_00) — vertical edge degenerate; document whether the resulting matrix produces NaN/inf in the second column, as a regression baseline before any guard is added.
- Add test: `computeM_us` with zero canvas height (H_ui=0) — the matrix entry 1/H_ui becomes infinity; document the behavior as a baseline before any guard is added.
- Refactor: split `renderer_init.cpp` (1133 lines) — extract pipeline creation (~457 lines, `createPipelines`) into `renderer_pipelines.cpp` and render pass definitions (~211 lines, `createRenderPasses`) into `renderer_renderpasses.cpp`; keep device/swapchain/descriptor orchestration in `renderer_init.cpp`. Do not split if doing so requires shuffling large numbers of shared static helpers.

## Iterate Loop
- run test.sh, read the output and investigate any problems and identify tasks to address the problem, then append the task to the pending tasks section
- run build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes, focus on testing systems introduced by this project. Consider refactoring the system to accommodate testing if system is important. Add all relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor and break up files into multiple smaller files if the file contains too many unrelated concepts. Do not break up files if doing so fragments logics that should naturally be co-located.
- Update ['File Structure'](../CLAUDE.md/#File-Structure) to reflect current project structure.


