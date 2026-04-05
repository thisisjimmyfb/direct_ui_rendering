## Instructions
Read ['SPEC.md'](/spec/SPEC.md) and ['direct_ui_rendering.md'](/spec/direct_ui_rendering.md). Then find the most important task from ['Pending Tasks'](#Pending-Tasks), but don't implement it yet. If the task affects the core execution of the demo, then please create tests that will fail without implementing the task. After tests are created, work on the implementation. Afterward, execute all items in the ['Iterate'](#Iterate) section. Then remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit and do not write progress or summary in this file.

## Pending Tasks
- Make sure the room ceilings are cleanly attached to the walls at the edges and don't have visible gaps.
- Make sure the spotlight stay inside the room and doesn't clip the ceiling.
- Update ['test_msaa_quality'](/tests/test_msaa_quality.cpp)'s comment to say that direct UI rendering image quality is due to msaa coverage done in view clip space rather than UI clip space. Even if the off screen rendertarget has high quality anti-aliasing applied, the transformation from UI clip space to world space can introduce aliasing if the UI geometry is rendered at a steep angle in relation to the viewer. Direct UI rendering eliminates this problem by applying the anti-aliasing technique in the final view clip space.
- Improve demo visuals: Add moving shadow effects or dynamic lighting variations
- Implement UI text color variants for visual variety in demo scene
- Implement dynamic shadow intensity variation for more dramatic lighting changes
- Add subtle spotlight flicker or intensity variation to create more dynamic lighting
- Add dynamic spotlight color cycling or warm/cool variation for atmospheric effect
- Add ambient color variation visualization (currently animating but subtle)
- Add time-based material shininess variation for more dynamic lighting
- Implement spotlight glow visualization near light source
- Add animated spotlight position (move light in a circular arc for dramatic effect)
- Add subtle camera position animation (oscillating viewpoint for parallax effect)
- Implement time-based ambient color cycling to complement light intensity animation
- Add metallic or reflective surface material properties to showcase material variety
- Add subtle bloom or glow effect around bright spotlight area
- Create ripple or wave patterns on floors to enhance visual interest
- Add subtle parallax mapping or normal map effects to room walls
- Implement UV-based texture animation on walls (scrolling patterns)
- Add depth variance or roughness variation to materials (normal map simulation)
- Create animated wave patterns on floor geometry for visual interest
- Add rotating/scrolling geometry patterns for visual dynamism
- Add comprehensive command-line parameter tests: --timeout parameter validation and edge cases (NEW - completed tests added)

## Iterate
- Run tests by running /scripts/test.sh. If there are any test failures, please investigate and fix the failure if the fix is small. If the fix will be big, please identify tasks to address the problem, and then append the task to the ['Pending Tasks'](#Pending-Tasks).
- Check Vulkan errors by running /scripts/build.sh and execute /build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and fix all Vulkan Validation Layer errors.
- Investigate the pending changes and look for opportunities to refactor to either move common code into a common file or break up files into multiple smaller files if the file contains too many unrelated concepts. Do not break up files if doing so fragments logics that should naturally be co-located.
- Investigate ways to strengthen testing based on pending changes, focus on testing systems from this repo. Consider refactoring the system to accommodate testing if system is important. Add all relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Update ['File Structure'](/CLAUDE.md/#File-Structure) to reflect current project structure.
- If pending changes introduce any conflicts with ['SPEC.md'](/spec/SPEC.md), please update the spec.
- Come up with ideas to make the demo look better and add the task to the ['Pending Tasks'](#Pending-Tasks) section.

## Out of Spec
