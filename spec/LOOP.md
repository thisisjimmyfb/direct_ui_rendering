## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and implement it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). If changes there are repo changes as a result of task completion, prepend a brief summary of the completed task along with completion date and timestamp in ['PROGRESS.md'](PROGRESS.md).

## Pending Tasks
- Add More Tests
	- Add memory stability test for traditional mode (mirrors MemoryStable_After300Frames_DirectMode)
	- Add non-empty pixel assertion to traditional mode containment test to prevent vacuous pass when surface quad is off-screen
	- Add multi-frame animation containment test: compute worldCorners() at t=0, t=1, t=2 and verify direct-mode magenta pixels are contained at each frame

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Update ['SPEC.md'](SPEC.md)'s ['File Structure'](SPEC.md/#File-Structure) section to reflect current project structure.
