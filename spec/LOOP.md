## Instructions
Read ['SPEC.md'](/spec/SPEC.md) and ['direct_ui_rendering.md'](/spec/direct_ui_rendering.md). Then find the most important task from ['Pending Tasks'](#Pending-Tasks) that can be worked on without changing the specification. If the task requires changing the specification, move the task under ['Out of Spec'](#Out-of-Spec). 
Once a task that can be worked on without changing the specification is identified, work on the task until completion. Afterward, execute all items in the ['Iterate'](#Iterate) section. Then remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github and do not write progress or summary in this file.

## Pending Tasks
- Add unit tests for Renderer initialization (createInstance, selectPhysicalDevice, createLogicalDevice, createAllocator)
- Add unit tests for Renderer render pass creation (createRenderPasses with validation of attachments, samples, and load/store ops)
- Add unit tests for Renderer pipeline creation (createPipelines with validation of pipeline layout and descriptor set layouts)
- Add unit tests for descriptor set management (allocateDescriptorSets, bindAtlasDescriptor with validation)
- Add unit tests for uniform buffer updates (updateSceneUBO, updateSurfaceUBO, updateFaceSurfaceUBOs) to verify data is correctly written
- Add unit tests for headless render target creation and destruction (createHeadlessRT, destroyHeadlessRT)
- Add unit tests for offscreen UI RT allocation (ensureUIRTAllocated, initOffscreenRT)
- Add unit tests for surface geometry updates (updateSurfaceQuad, updateCubeSurface, updateUIShadowCube)
- Add unit tests for App input handling (key callbacks, mode toggle, depth bias adjustment)
- Add unit tests for terminal input mode (text accumulation, backspace handling, cursor rendering)

## Iterate
- run test.sh, read the output and investigate any problems and identify tasks to address the problem, then append the task to the pending tasks section
- run build.sh and execute /build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes, focus on testing systems introduced by this project. Consider refactoring the system to accommodate testing if system is important. Add all relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor to either move common code into a common file or break up files into multiple smaller files if the file contains too many unrelated concepts. Do not break up files if doing so fragments logics that should naturally be co-located.
- Update ['File Structure'](/CLAUDE.md/#File-Structure) to reflect current project structure.

## Out of Spec