#include <gtest/gtest.h>
#include "metrics.h"
#include "ui_system.h"

#include <thread>
#include <chrono>
#include <cstring>

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

// ---------------------------------------------------------------------------
// MetricsTest — Traditional mode produces correct vertex count
// ---------------------------------------------------------------------------

// tessellateHUD with RenderMode::Traditional formats the mode line as
// "Mode: TRADITIONAL" (17 chars) rather than "Mode: DIRECT" (12 chars).
// The total vertex count must equal 6 * sum-of-chars for the four known lines.
// This test catches regressions where the mode string is truncated or wrong.
TEST(MetricsTest, HUDTessellation_TraditionalMode_VertexCount)
{
    UISystem sys;
    sys.buildGlyphTable();

    // Fresh Metrics: averageFrameMs()==0.0f, gpuAllocatedBytes()==0.
    // With RenderMode::Traditional, msaaSamples=4, no inputModeStr:
    //   Line 0: "Mode: TRADITIONAL" = 17 chars
    //   Line 1: "Frame: 0.0 ms"     = 13 chars
    //   Line 2: "GPU Mem: 0.0 MB"   = 15 chars
    //   Line 3: "MSAA: 4x"          =  8 chars
    //   Total                        = 53 chars  →  318 vertices
    Metrics metrics;
    std::vector<UIVertex> verts;
    uint32_t count = metrics.tessellateHUD(sys, RenderMode::Traditional, 4u, verts);

    constexpr int expectedChars = 17 + 13 + 15 + 8;
    constexpr uint32_t expectedVerts = 6u * static_cast<uint32_t>(expectedChars);

    EXPECT_EQ(count, expectedVerts)
        << "tessellateHUD returned wrong vertex count for Traditional mode HUD";
    EXPECT_EQ(static_cast<uint32_t>(verts.size()), expectedVerts)
        << "outVerts.size() does not match the returned count";
}

// ---------------------------------------------------------------------------
// MetricsTest — optional inputModeStr adds a fifth line
// ---------------------------------------------------------------------------

// When a non-null inputModeStr is passed, tessellateHUD must append a 5th line
// and the returned vertex count must equal the 4-line total plus
// 6 * strlen(inputModeStr).  This catches cases where the optional line is
// skipped entirely or double-counted.
TEST(MetricsTest, HUDTessellation_WithInputModeStr_AddsExtraLine)
{
    UISystem sys;
    sys.buildGlyphTable();

    // Fresh Metrics: averageFrameMs()==0.0f, gpuAllocatedBytes()==0.
    // With RenderMode::Direct, msaaSamples=4:
    //   4-line base (same as HUDTessellation_VertexCountMatchesLineCount):
    //     "Mode: DIRECT"    = 12 chars
    //     "Frame: 0.0 ms"   = 13 chars
    //     "GPU Mem: 0.0 MB" = 15 chars
    //     "MSAA: 4x"        =  8 chars
    //   Line 4: "Input: keyboard" = 15 chars
    //   Total                      = 63 chars  →  378 vertices
    const char* inputStr = "Input: keyboard";
    Metrics metrics;
    std::vector<UIVertex> verts;
    uint32_t count = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts, inputStr);

    constexpr int baseLine4Chars = 12 + 13 + 15 + 8;
    const int extraChars = static_cast<int>(std::strlen(inputStr));
    const uint32_t expectedVerts = 6u * static_cast<uint32_t>(baseLine4Chars + extraChars);

    EXPECT_EQ(count, expectedVerts)
        << "tessellateHUD returned wrong vertex count when inputModeStr is set";
    EXPECT_EQ(static_cast<uint32_t>(verts.size()), expectedVerts)
        << "outVerts.size() does not match the returned count";
}

// ---------------------------------------------------------------------------
// MetricsTest — tessellateHUD appends to a pre-populated outVerts vector
// ---------------------------------------------------------------------------

// If outVerts is cleared inside tessellateHUD before tessellation begins,
// the pre-existing vertices would be lost and the final size would equal
// only the tessellated count.  This test guards against that regression by
// calling tessellateHUD with a non-empty vector and verifying that the total
// size equals the pre-existing element count plus the tessellated vertex count.
TEST(MetricsTest, HUDTessellation_AppendsToExistingVector)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;

    // Pre-populate the vector with 7 default-constructed vertices.
    constexpr uint32_t preExistingCount = 7u;
    verts.resize(preExistingCount);

    // tessellateHUD with RenderMode::Direct, msaaSamples=4, no inputModeStr
    // produces 288 vertices (same as HUDTessellation_VertexCountMatchesLineCount).
    uint32_t appended = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts);

    constexpr uint32_t expectedAppended = 6u * (12u + 13u + 15u + 8u); // 288
    EXPECT_EQ(appended, expectedAppended)
        << "tessellateHUD returned wrong appended vertex count";
    EXPECT_EQ(verts.size(), static_cast<size_t>(preExistingCount + expectedAppended))
        << "outVerts was cleared before tessellation — pre-existing vertices lost";
}

// ---------------------------------------------------------------------------
// MetricsTest — null VmaAllocator sets gpuAllocatedBytes to zero
// ---------------------------------------------------------------------------

// updateGPUMem(VK_NULL_HANDLE) must set gpuAllocatedBytes() to 0 rather than
// dereferencing the null handle.  This test guards against the null-check
// being accidentally removed in a future refactor.
TEST(MetricsTest, UpdateGPUMem_NullAllocator_SetsZero)
{
    Metrics m;
    m.updateGPUMem(VK_NULL_HANDLE);
    EXPECT_EQ(m.gpuAllocatedBytes(), 0u)
        << "gpuAllocatedBytes() must be 0 when allocator is VK_NULL_HANDLE";
}
