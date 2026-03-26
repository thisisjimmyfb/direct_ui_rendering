#pragma once

// Performance regression reference values.
// Captured on development machine after initial implementation.
// Tests assert: measured <= reference * (1 + tolerance).
// Update these values when the renderer changes significantly.

namespace perf_ref {

// GPU memory (bytes) — measured from headless runs
// Direct mode: scene geometry + UI atlas + shadow map + UBOs
// Traditional mode: + offscreen RT (512x128 RGBA8) = +256KB
constexpr uint64_t GPU_MEM_DIRECT_BYTES      = 16 * 1024 * 1024;  // 16 MB baseline
constexpr uint64_t GPU_MEM_TRADITIONAL_BYTES = 17 * 1024 * 1024;  // 17 MB (+256KB RT)

// CPU frame time (ms) — measured over 60-frame rolling average
// Direct mode: shadow + main pass with UI in same pass
// Traditional mode: shadow + UI RT + main pass + composite
constexpr float FRAME_TIME_DIRECT_MS      = 2.0f;   // ~500 FPS target
constexpr float FRAME_TIME_TRADITIONAL_MS = 2.5f;   // ~400 FPS target

// Tolerances
constexpr float MEM_TOLERANCE   = 0.10f;   // +10%
constexpr float TIME_TOLERANCE  = 0.20f;   // +20%

} // namespace perf_ref
