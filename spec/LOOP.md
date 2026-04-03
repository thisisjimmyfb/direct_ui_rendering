## Instructions
- Read ['SPEC.md'](/spec/SPEC.md) and ['direct_ui_rendering.md'](/spec/direct_ui_rendering.md).
- Find the most important task from ['Pending Tasks'](#Pending-Tasks) and work on it using the best decision based on the specifications. If the task requires changing the specification, move the task under ['Out of Spec'](#Out-of-Spec) instead and find the next most important task and repeat.
- Afterward, execute all items in the ['Iterate'](#Iterate) section.
- Remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github and do not write progress or summary in this file.

## Pending Tasks
- Update all comments to reflect what the code is actually doing.
- Add test: `Scene::worldCorners` with `scaleW=0.0f` — document and validate the behavior (NaN or crash) when edge vectors are exactly zero-length, establishing a baseline before any guard is added. Guards against regressions if a clamp is later introduced.
- Add test: `UISystem::tessellateString` with a very long string (1000+ characters) — verify no integer overflow in the returned `uint32_t` vertex count and that `outVerts.size()` equals the returned count.
- Add test: `computeClipPlanes` with collinear corners (P_00, P_10, P_01 on the same line) — the cross product is zero so the surface normal is undefined; document whether the function produces NaN, zero, or well-defined clip planes as a regression baseline.
- Add test: `computeM_us` with zero canvas dimension (W_ui=0) — the matrix entry 1/W_ui becomes infinity; document the behavior as a baseline before any guard is added.
- Add test: `computeM_sw` with zero-length `e_u` (P_10 == P_00) — basis is degenerate; document whether the resulting matrix produces NaN/inf column or a zero column, as a regression baseline before any guard is added.
- Add test: `computeM_sw` with zero-length `e_v` (P_01 == P_00) — vertical edge degenerate; document whether the resulting matrix produces NaN/inf in the second column, as a regression baseline before any guard is added.
- Add test: `computeM_us` with zero canvas height (H_ui=0) — the matrix entry 1/H_ui becomes infinity; document the behavior as a baseline before any guard is added.

## Iterate
- run test.sh, read the output and investigate any problems and identify tasks to address the problem, then append the task to the pending tasks section
- run build.sh and execute /build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes, focus on testing systems introduced by this project. Consider refactoring the system to accommodate testing if system is important. Add all relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor and break up files into multiple smaller files if the file contains too many unrelated concepts. Do not break up files if doing so fragments logics that should naturally be co-located.
- Update ['File Structure'](/CLAUDE.md/#File-Structure) to reflect current project structure.

## Out of Spec
- Refactor: split `renderer_init.cpp` — completed. Extracted pipeline creation into `renderer_pipelines.cpp` and render pass definitions into `renderer_renderpasses.cpp`.
- Bug fix: Added `m_setLayout2` to pipeline layout to fix descriptor set mismatch errors in tests.

