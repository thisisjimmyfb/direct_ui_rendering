## Instruction
Read ['SPEC.md'](SPEC.md) and ['direct_ui_rendering.md'](direct_ui_rendering.md).
Find the most important task from the following list and implement it. After task completion, execute all items in the ['Iterate Loop'](#Iterate-Loop) section, remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). If changes there are repo changes as a result of task completion, prepend a brief summary of the completed task along with completion date and timestamp in ['PROGRESS.md'](PROGRESS.md).

## Pending Tasks
- The entry in the Overlay for input mode toggle is overlapping the msaa mode, please make sure the overlay layout is consistent and follows the same style applied to all elements.
- Investigate on how to support signed distance field fonts for UI rendering. Come up with a list of tasks to add signed distance field support and add them to this list.

## Iterate Loop
- run build/test.sh, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- run build/build.sh and execute build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes. Add relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
