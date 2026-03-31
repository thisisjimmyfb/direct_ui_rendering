## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and work on it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github and do not write progress or summary in this file.

## Pending Tasks
- Make all toggle obvious on the overlay UI. Tell the user which key to press to toggle each mode.
- Make the shadowmap a spotlight (instead of a parallel light) that casts a shadow inside the room. The spotlight should be casting a shadow of the floating UI quad onto the wall.

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, then append the task to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes, focus on testing systems introduced by this project. Consider refactoring the system to accommodate testing if system is important. Add all relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor and break up files into multiple smaller files if the file contains too many unrelated concepts. Do not break up files if doing so fragments logics that should naturally be co-located.
- Update ['File Structure'](../CLAUDE.md/#File-Structure) to reflect current project structure.


