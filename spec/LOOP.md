## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and implement it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github.

## Pending Tasks
- Add unit test `MetricsTest.HUDTessellation_ReturnsZeroForEmptyUISystem`: call `tessellateHUD()` with a `UISystem` whose glyph table has not been built (`buildGlyphTable()` not called) and verify the return value is 0 and `outVerts` is not modified. NOTE: current `tessellateString` produces 6 vertices per character regardless of glyph table state (zeroed UVs). This test requires either (a) adding an `isGlyphTableBuilt()` guard to `UISystem` and an early-return in `tessellateHUD`, or (b) revising the expected behavior to match the current implementation.
- Add unit test `MetricsTest.HUDTessellation_AppendedVertexPositions_InHUDRegion`: after calling `tessellateHUD()` on a fresh vector, verify that every appended vertex has `pos.x >= 8.0f` (leftMargin) and `pos.y >= 8.0f` (leftMargin), catching regressions where line offsets are computed incorrectly and vertices land at negative or zero coordinates.
- Add unit test `MetricsTest.HUDTessellation_AppendedVertexUVs_InUnitSquare`: after calling `tessellateHUD()`, verify that every appended vertex has `uv.x` and `uv.y` in `[0.0f, 1.0f]`, ensuring the glyph atlas lookup table never produces out-of-range UVs under normal operation.

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor and break up big files into multiple smaller files. Do not break up files if doing so fragments logics that should naturally be co-located.
- Update ['File Structure'](SPEC.md/#File-Structure) to reflect current project structure.
