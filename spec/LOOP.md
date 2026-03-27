## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and implement it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). If changes there are repo changes as a result of task completion, prepend a brief summary of the completed task along with completion date and timestamp in ['PROGRESS.md'](PROGRESS.md).

## Pending Tasks
- Add non-empty pixel assertion to `ExtremeAngle_DirectMode_MagentaPixels_InsideSurfaceQuad` — the test currently passes vacuously (0 violations, but potentially 0 magenta pixels in the thin sliver). Mirror the `EXPECT_GT(countMagentaPixels(pixels), 0)` guard added to the traditional-mode and animation tests.
- Update SPEC.md Section 6.2 Pipelines table to add `pipe_surface` entry (opaque teal quad drawn before direct-mode UI geometry; uses `quad.vert` + `surface.frag`; currently undocumented).
- Update SPEC.md Section 10.2 Headless Renderer Design Constraint to document the `shaderDir` parameter added to `Renderer::init()`: tests must pass `TEST_SHADER_DIR` at runtime so the library resolves test shaders rather than defaulting to the production `SHADER_DIR`.

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Update ['SPEC.md'](SPEC.md)'s ['File Structure'](SPEC.md/#File-Structure) section to reflect current project structure.
