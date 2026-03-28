## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and implement it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github.

## Pending Tasks
- Add unit test `UISystemUVTable.RebuildGlyphTable_ConsistentAfterMultipleCalls`: call `buildGlyphTable()` twice on the same `UISystem` and verify the resulting UV table is identical both times, guarding against non-idempotent initialization or state pollution.
- Add unit test `UISystemUVTable.AllCharacterIndexSpacing_NoDuplicatesOrGaps`: verify all 95 glyph UV rects tile the atlas grid without overlap or gap, checking that each rect is exactly `(GLYPH_CELL/ATLAS_SIZE)²` in area and adjacent entries share an edge.
- Add unit test `TessellateStringTest.RepeatedCharsSameGlyph_AllVerticesIdenticalUVs`: tessellate `"AAAA"` and verify all 24 vertices share the same UV rect as `uvForChar('A')`, guarding against permutation or ordering bugs in quad assembly.
- Add unit test `MetricsTest.AverageFrameMs_ExactlyZeroWhenNoFrames`: verify a freshly constructed `Metrics` object returns exactly `0.0f` from `averageFrameMs()` before any frames are recorded, guarding against uninitialized ring-buffer data.
- Add unit test `MetricsTest.HUDTessellation_LineHeightSpacing_WithInputModeStr_FifthLineSeparation`: when `inputModeStr` is supplied, verify the 5th line's first vertex TL y equals `leftMargin + 4 * lineHeight` (128.0f), catching regressions where the optional line uses a wrong index or a hard-coded offset.
- Add unit test `MetricsTest.AverageFrameMs_SingleFrame_ReturnsPositiveValue`: after exactly one call to `beginFrame` / `endFrame` (with a brief sleep), verify `averageFrameMs()` returns a strictly positive value, guarding against the single-sample path in `averageFrameMs()` dividing by zero or discarding the first sample.
- Refactor `src/renderer.cpp` (≈2086 lines): split into `renderer_init.cpp` (device/pipeline/swapchain setup), `renderer_recording.cpp` (per-frame command-buffer recording), and `renderer_resources.cpp` (GPU resource management), keeping `renderer.cpp` as the thin orchestration layer. Only proceed if the split does not fragment logic that is naturally co-located.

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor and break up big files into multiple smaller files. Do not break up files if doing so fragments logics that should naturally be co-located.
- Update ['File Structure'](SPEC.md/#File-Structure) to reflect current project structure.
