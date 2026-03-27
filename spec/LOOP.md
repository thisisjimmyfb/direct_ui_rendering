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
- Strengthen SDF Coverage Further
	- Add render test: below-threshold pixels are fully transparent — atlas R=0.1 (well below threshold=0.5), verify total pixel diff vs background-only render equals zero (complements above-threshold saturation test)
	- Add render test: shadow-SDF interaction in direct mode — place UI surface fully in shadow, render with on-edge atlas (R=128) and sdfThreshold=0.5, verify alpha channel matches smoothstep(0.43,0.57,0.502)≈0.521 independently of shadow lighting applied to RGB
	- Add render test: pre-multiplied alpha pipeline correctness — render with above-threshold atlas (R=220) in traditional composite mode, verify teal background bleeds through at (1 - alpha) proportion and UI RGB contribution matches pre-multiplied expected value

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Update ['SPEC.md'](SPEC.md)'s ['File Structure'](SPEC.md/#File-Structure) section to reflect current project structure.
