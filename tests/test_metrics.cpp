#include <gtest/gtest.h>
#include "metrics.h"
#include "ui_system.h"

#include <thread>
#include <chrono>

// ---------------------------------------------------------------------------
// MetricsTest — frame timing ring buffer and HUD tessellation
// ---------------------------------------------------------------------------

// After recording exactly HISTORY_SIZE=60 frames the ring buffer is full.
// averageFrameMs() must use exactly 60 samples.  The guard here is the
// off-by-one case where m_filled is never set (or is set one frame too late),
// causing averageFrameMs() to return 0.0f because count collapses to 0.
TEST(MetricsTest, FrameTimingRollingAverage_WrapsCorrectly)
{
    Metrics m;

    // Pre-condition: no frames recorded yet.
    EXPECT_FLOAT_EQ(m.averageFrameMs(), 0.0f);

    // Record 59 near-instant frames followed by one frame that sleeps 1 ms.
    // This guarantees the average is non-zero after the buffer fills, while
    // keeping the total sleep time to a single millisecond.
    constexpr int HISTORY = 60;
    for (int i = 0; i < HISTORY - 1; ++i) {
        m.beginFrame();
        m.endFrame();
    }
    m.beginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    m.endFrame();

    // Buffer is now full (m_filled == true).  If the wrap index check is
    // off-by-one, m_filled stays false, count == 0, and the result is 0.
    float avg = m.averageFrameMs();
    EXPECT_GT(avg, 0.0f)
        << "averageFrameMs() returned 0 after " << HISTORY
        << " frames — m_filled was not set at the correct wrap index";

    // The average of 60 frames where only the last frame is ~1 ms should be
    // well below 1 ms.  If count were wrong (e.g. 1 instead of 60) the result
    // would be approximately 1 ms, catching the bug.
    EXPECT_LT(avg, 0.5f)
        << "average too large — count may be 1 instead of " << HISTORY;

    // Record a second full pass (second wrap-around).  The 1 ms frame is
    // overwritten; all current frames are near-instant.
    for (int i = 0; i < HISTORY; ++i) {
        m.beginFrame();
        m.endFrame();
    }

    float avg2 = m.averageFrameMs();
    EXPECT_GE(avg2, 0.0f)
        << "averageFrameMs() negative after second wrap-around";
    EXPECT_LT(avg2, 0.5f)
        << "averageFrameMs() unexpectedly large after second wrap-around";
}

// ---------------------------------------------------------------------------
// MetricsTest — HUD tessellation vertex count
// ---------------------------------------------------------------------------

// tessellateHUD formats 4 lines and tessellates them via
// UISystem::tessellateString, which produces 6 vertices per character.
// The total vertex count must equal 6 * (sum of character counts across all
// lines) for a known, fixed set of inputs.
TEST(MetricsTest, HUDTessellation_VertexCountMatchesLineCount)
{
    UISystem sys;
    sys.buildGlyphTable();

    // Fresh Metrics: averageFrameMs()==0.0f, gpuAllocatedBytes()==0.
    // With RenderMode::Direct, msaaSamples=4, no inputModeStr:
    //   Line 0: "Mode: DIRECT"     = 12 chars
    //   Line 1: "Frame: 0.0 ms"    = 13 chars
    //   Line 2: "GPU Mem: 0.0 MB"  = 15 chars
    //   Line 3: "MSAA: 4x"         =  8 chars
    //   Total                      = 48 chars  →  288 vertices
    Metrics metrics;
    std::vector<UIVertex> verts;
    uint32_t count = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts);

    constexpr int expectedChars = 12 + 13 + 15 + 8;
    constexpr uint32_t expectedVerts = 6u * static_cast<uint32_t>(expectedChars);

    EXPECT_EQ(count, expectedVerts)
        << "tessellateHUD returned wrong vertex count for 4 HUD lines";
    EXPECT_EQ(static_cast<uint32_t>(verts.size()), expectedVerts)
        << "outVerts.size() does not match the returned count";
}
