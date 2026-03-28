## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and implement it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github.

## Pending Tasks
- Add unit tests for `UISystem::tessellateString`: verify empty string → 0 vertices, N characters → 6N vertices, quad corner positions advance by GLYPH_CELL per character from the starting (x,y), and UV coordinates match `uvForChar` for each character in the string.
- Add unit test `UISurfaceTest.LocalCorners_CorrectDimensions`: verify the default `UISurface` local corner positions are exactly P_00=(-2,1,0), P_10=(2,1,0), P_01=(-2,-1,0), P_11=(2,-1,0) — a 4m×2m quad centered at the origin.
- Add unit test `SceneAnimationTest.ZTranslation_AlwaysFixedAt_Neg2_5`: verify that `animationMatrix(t)[3][2]` equals -2.5 for multiple values of t (0, π/0.18, π/0.22, 2π), guarding the invariant that the surface depth offset is constant.

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor and break up big files into multiple smaller files. Do not break up files if doing so fragments logics that should naturally be co-located.
- Update ['File Structure'](SPEC.md/#File-Structure) to reflect current project structure.
