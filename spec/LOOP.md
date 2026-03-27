## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and implement it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). If changes there are repo changes as a result of task completion, prepend a brief summary of the completed task along with completion date and timestamp in ['PROGRESS.md'](PROGRESS.md).

## Pending Tasks
- Add More Tests
	- Add render test for traditional mode (composite pass) UI containment
	- Add render test for UI at extreme angles (near edge-on view)
	- Add performance test with longer animation duration to detect memory leaks
- Add SDF Tests
	- Add render test for SDF mode: verify non-zero alpha pixels exist in expected text area of the atlas
- Strengthen SDF Coverage (tests_sdf framework)
	- Add render test: SDF above-threshold atlas (all pixels R=220, well above sdfThreshold+spread=0.57) with sdfThreshold=0.5 should produce full-alpha pixels (alpha≈1.0), verifying smoothstep saturates at the high end (complements the on-edge and below-threshold tests)
	- Add render test: traditional mode on-edge atlas (R=SDF_ON_EDGE_VALUE) with sdfThreshold=0.5 via recordUIRTPass produces different composited output than a fully-zero atlas (exercises RT-pass SDF path at the threshold boundary)

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Update ['SPEC.md'](SPEC.md)'s ['File Structure'](SPEC.md/#File-Structure) section to reflect current project structure.
