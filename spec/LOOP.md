## Instructions
Read ['SPEC.md'](/spec/SPEC.md) and ['direct_ui_rendering.md'](/spec/direct_ui_rendering.md). Then find the most important task from ['Pending Tasks'](#Pending-Tasks) that can be worked on without changing the specification. If the task requires changing the specification, move the task under ['Out of Spec'](#Out-of-Spec). 
Once a task that can be worked on without changing the specification is identified, work on the task until completion. Afterward, execute all items in the ['Iterate'](#Iterate) section. Then remove the task and save this file (do not mark or remove tasks from the Iterate Loop Section). Do not commit to github and do not write progress or summary in this file.

## Pending Tasks

- Fix failing test: SDFOnEdgeTest.PreMultipliedAlpha_TraditionalMode_TealBleeds (pre-multiplied alpha blending issue in traditional mode)
- Improve demo visuals: Add moving shadow effects or dynamic lighting variations
- Implement UI text color variants for visual variety in demo scene
- Create ripple or wave patterns on floors to enhance visual interest
- Add metallic or reflective surface material properties to showcase material variety
- Add subtle bloom or glow effect around bright spotlight area
- Animate UI surface cube rotation or oscillation more dynamically for visual interest
- Add subtle parallax mapping or normal map effects to room walls
- Implement dynamic shadow intensity variation for more dramatic lighting changes
- Add test coverage for UI color animation - expand from 15 tests to full coverage (HSV-to-RGB conversion and phase animation)
- Add subtle spotlight flicker or intensity variation to create more dynamic lighting
- Implement UV-based texture animation on walls (scrolling patterns)
- Add depth variance or roughness variation to materials (normal map simulation)
- Add dynamic spotlight color cycling or warm/cool variation for atmospheric effect
- Add ambient color variation visualization (currently animating but subtle)
- Add time-based material shininess variation for more dynamic lighting
- Create animated wave patterns on floor geometry for visual interest
- Implement spotlight glow visualization near light source
- Add rotating/scrolling geometry patterns for visual dynamism
- Test UI color animation system more thoroughly (expand test coverage to 20 tests)

## Iterate
- run test.sh, read the output and investigate any problems and identify tasks to address the problem, then append the task to the pending tasks section
- run build.sh and execute /build/Debug/direct_ui_rendering.exe with a 10 second timeout, read the output and investigate any problems and identify tasks to address the problem, and append to the pending tasks section
- Investigate ways to strengthen testing based on staging changes, focus on testing systems introduced by this project. Consider refactoring the system to accommodate testing if system is important. Add all relevant tests from the investigation into the ['Pending Tasks'](#Pending-Tasks) section.
- Investigate the pending changes and look for opportunities to refactor to either move common code into a common file or break up files into multiple smaller files if the file contains too many unrelated concepts. Do not break up files if doing so fragments logics that should naturally be co-located.
- Come up with ideas to make the demo look better and add the task to the ['Pending Tasks'](#Pending-Tasks) section.
- Update ['File Structure'](/CLAUDE.md/#File-Structure) to reflect current project structure.

## Out of Spec