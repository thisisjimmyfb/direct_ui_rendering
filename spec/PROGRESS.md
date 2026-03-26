## Progress History

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