## Instructions
Read ['SPEC.md'](/spec/SPEC.md) and ['direct_ui_rendering.md'](/spec/direct_ui_rendering.md). Then find the most important task from ['Pending Tasks'](#Pending-Tasks), but don't implement it yet. If the task affects the core execution of the demo, then please create tests that will fail without implementing the task. After tests are created, work on the implementation. Afterward, execute all items in the ['Iterate'](#Iterate) section. Then remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Be concise and only write comments if the logic is complex. Do not commit and do not write progress or summary in this file.

## Pending Tasks
- Fix metric UI now appearing all white.
- Add a direct-vs-traditional lighting parity test: render the same surface with the same surfaceNormal/vertex normal in both modes and assert their center-pixel brightness is within a small tolerance. This validates the core claim that both modes produce identical lighting.

## Iterate
- Make sure constants, functions or classes are not duplicated in multiple files. If identical constructs exist in multiple files, consider steps to refactor and share the common construct. 
- Run tests by running /scripts/test.sh. If there are any test failures, please investigate and fix the failure if the fix is small. If the fix will be big, please identify tasks to address the problem, and then append the task to the ['Pending Tasks'](#Pending-Tasks). 
- Check Vulkan errors by running /scripts/build.sh and execute /build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and fix all Vulkan Validation Layer errors. 
- Investigate the pending changes and look for opportunities to refactor to either move common code into a common file or break up files into multiple smaller files if the file contains too many unrelated concepts. Do not break up files if doing so fragments logics that should naturally be co-located. 
- Investigate ways to strengthen testing based on pending changes, focus on testing systems from this repo. Consider refactoring the system to accommodate testing if system is important. Add all relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- If pending changes introduce any conflicts with ['SPEC.md'](/spec/SPEC.md), please update the spec.

## Out of Spec
