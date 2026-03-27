## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and implement it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). If changes there are repo changes as a result of task completion, prepend a brief summary of the completed task along with completion date and timestamp in ['PROGRESS.md'](PROGRESS.md).

## Pending Tasks
- Add Unit Tests
	- Add unit test for clip planes with rotated/transformed surface (non-axis-aligned quad)
	- Add unit test for M_total with non-identity view-projection matrix
	- Add unit test for depth bias sensitivity (test various bias values)
	- Add unit test for surface transform with non-uniform scaling
	- Add unit test for clip plane boundary conditions (exactly on edge)
- Add More Tests
	- Add render test for traditional mode (composite pass) UI containment
	- Add render test for UI at extreme angles (near edge-on view)
	- Add performance test with longer animation duration to detect memory leaks
- Add SDF Tests
	- Add unit test verifying SDF_THRESHOLD_DEFAULT equals SDF_ON_EDGE_VALUE/255.0 within tolerance
	- Add unit test that UISystem::sdfThreshold() returns 0.0 when isSDF() is false
	- Add render test for SDF mode: verify non-zero alpha pixels exist in expected text area of the atlas

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Update the ['File Structure'](SPEC.md/#File-Structure) section to reflect current project structure.
