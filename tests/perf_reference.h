#pragma once

// Performance regression reference values.
// Fill in after the first successful headless run on the development machine.
// Tests assert: measured <= reference * (1 + tolerance).

namespace perf_ref {

// GPU memory (bytes) — TBD after first run
constexpr uint64_t GPU_MEM_DIRECT_BYTES      = 0;   // TODO
constexpr uint64_t GPU_MEM_TRADITIONAL_BYTES = 0;   // TODO

// CPU frame time (ms) — TBD after first run
constexpr float FRAME_TIME_DIRECT_MS      = 0.0f;   // TODO
constexpr float FRAME_TIME_TRADITIONAL_MS = 0.0f;   // TODO

// Tolerances
constexpr float MEM_TOLERANCE   = 0.10f;   // +10%
constexpr float TIME_TOLERANCE  = 0.20f;   // +20%

} // namespace perf_ref
