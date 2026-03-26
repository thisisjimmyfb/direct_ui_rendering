## Progress History

- **2026-03-26 19:00**: Added Tab-key input mode toggle (Camera ↔ UITerminal) and keyboard text editing on the floating world-space quad. In UITerminal mode: WASD/mouse camera is disabled; typed printable ASCII characters (up to 255) appear on the floating quad with a live `|` cursor; Backspace deletes the last character; Escape returns to Camera mode. In Camera mode the quad shows "Hello World" as before. The HUD now has a fifth line showing the current input mode (`Input: CAMERA [Tab]` / `Input: TERMINAL [Tab]`). Implementation: new `InputMode` enum, `m_terminalText` string, a CPU-to-GPU terminal vertex buffer (1536 UIVertex), `charCallback`/`onChar` (GLFW char callback), and per-frame tessellation of the display text. 17/17 tests pass, build clean, app exits code 0 after 10s timeout.

- **2026-03-26 18:30**: Made the moving world-space quad an opaque teal color in both rendering modes. In traditional mode, `composite.frag` now blends the UI RT texture on top of a teal background using premultiplied-alpha compositing (`out = ui.rgb + teal*(1-ui.a)`), so the UI appears texture-mapped onto the teal quad. In direct mode, a new `surface.frag` shader and `pipeSurface` pipeline draw the teal quad first (with depth-write enabled and PCF shadow), then the existing `pipeUIDirect` renders the UI geometry on top via the clip-space offset (depth bias). The composite pipeline was updated to use `opaqueBlendState` with `depthWriteEnable=VK_TRUE` for consistency. `surface.frag` added to shader build list in CMakeLists.txt. Iterate loop: 17/17 tests pass, build clean, app exits code 0 after 10s timeout.

- **2026-03-26 18:00**: Fixed UI atlas path to resolve relative to executable directory. Used `GetModuleFileNameA` (Windows) / `readlink /proc/self/exe` (Linux) in `app.cpp` to derive `exeDir()`, replacing the hardcoded relative `"assets/atlas.png"` path so the app finds its atlas regardless of the working directory when launched. 17/17 tests pass, build clean, app exits code 0 after 10s timeout. No new issues found.

- **2026-03-26 17:00**: Iterate loop run — no pending tasks. 17/17 tests pass, Debug build clean, app exits code 0 after 10s timeout. No issues found.

- **2026-03-26 (latest)**: Implemented WASD+mouse camera control and slower UI quad animation with bigger lateral translations. Right-click captures mouse for look; WASD moves camera at 3 m/s; ESC releases cursor. Scene animation slowed (0.25 rad/s rotation, 0.18/0.22 rad/s lateral/vertical) with ±1.2 m lateral and ±0.35 m vertical travel. All 17/17 tests pass, build clean, app exits code 0 after 10s timeout.

- **2026-03-26 (latest)**: Iterate loop run — no pending tasks. 17/17 tests pass, build clean, app runs 10s and exits code 0. No issues found.

- **2026-03-26**: Iterate loop run — no pending tasks. All 17/17 tests pass, build succeeds cleanly, app runs 10-second timeout and exits with code 0. No issues identified.

- **2026-03-26**: Fixed all three UI atlas/overlay rendering bugs. (1) Regenerated `assets/atlas.png` with Courier New glyphs rendered as premultiplied-alpha white-on-transparent — old atlas had near-invisible glyphs. (2) Fixed inverted ortho matrix in `app.cpp`: both `uiOrtho` and `hudOrtho` called `glm::ortho(0,W,H,0,...)` which mapped y=0→Vulkan-bottom and y=H→Vulkan-top, rendering glyphs upside-down and at the wrong screen edge; corrected to `glm::ortho(0,W,0,H,...)`. All 17/17 tests pass, build clean, app runs without crash.

- **2026-03-26 11:14AM**: Completed iterate loop tasks: all 17 tests pass, build succeeds without errors, and the application runs without crash or validation errors. No pending issues identified.
- **2026-03-26**: Implemented command-line timeout parameter for automated Ralph Loop testing. App now accepts `--timeout <seconds>` argument to exit after specified duration.
- **2026-03-26**: All 17/17 tests pass (12 unit math/clip-plane tests + 1 render containment test + 4 perf tests). No failures.
- **2026-03-26** (re-run after atlas fix): All 17/17 tests still pass. No regressions.
- **2026-03-26**: Debug build succeeds cleanly. App launches and runs without crash. No stdout/stderr output. Key issue found: missing atlas PNG (see Known Issues).
- **2026-03-26** (re-run after atlas fix): Build succeeds. App runs without crash or validation errors. `assets/atlas.png` loads correctly (present at repo root and in `build/Debug/assets/`). No new issues found. App renders silently — visual verification of glyph text on UI surface and shadow map requires manual inspection of the window.