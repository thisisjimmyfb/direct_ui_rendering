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
// MetricsTest — pre-existing vertex VALUES are not modified by tessellateHUD
// ---------------------------------------------------------------------------

// HUDTessellation_AppendsToExistingVector confirms that the vector grows by
// the right number of elements, but a subtler regression is possible: the
// implementation could internally resize/reallocate and write zeros over the
// pre-existing slot range (e.g. via a clear() + resize() pattern) while still
// pushing the new vertices afterwards.  This test catches that by filling the
// initial slots with distinctive sentinel values and asserting every field of
// every sentinel vertex is unchanged after the call.
TEST(MetricsTest, HUDTessellation_PreExistingVertexValues_NotModified)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;

    // Use a non-power-of-two count so a resize-to-nearest-power-of-two
    // implementation is more likely to expose a bug.
    constexpr uint32_t SENTINEL_COUNT = 5u;

    // Sentinel values chosen to be far from zero so that any overwrite
    // (even with default-constructed UIVertex{}) is immediately detectable.
    const UIVertex sentinel{ {1234.0f, 5678.0f}, {0.111f, 0.999f} };
    for (uint32_t i = 0; i < SENTINEL_COUNT; ++i)
        verts.push_back(sentinel);

    // Call tessellateHUD; it must append, not overwrite.
    uint32_t appended = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts);

    // The appended count must be non-zero (same expectation as the sibling
    // tests), otherwise this test doesn't exercise the regression it intends to.
    ASSERT_GT(appended, 0u)
        << "tessellateHUD returned 0 vertices — sentinel check is vacuous";

    // Verify every sentinel vertex still holds its original values.
    for (uint32_t i = 0; i < SENTINEL_COUNT; ++i) {
        EXPECT_FLOAT_EQ(verts[i].pos.x, sentinel.pos.x)
            << "sentinel pos.x overwritten at index " << i;
        EXPECT_FLOAT_EQ(verts[i].pos.y, sentinel.pos.y)
            << "sentinel pos.y overwritten at index " << i;
        EXPECT_FLOAT_EQ(verts[i].uv.x, sentinel.uv.x)
            << "sentinel uv.x overwritten at index " << i;
        EXPECT_FLOAT_EQ(verts[i].uv.y, sentinel.uv.y)
            << "sentinel uv.y overwritten at index " << i;
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — HUD tessellation with unbuilt glyph table returns 0
// ---------------------------------------------------------------------------

// When buildGlyphTable() has not been called, tessellateHUD() must return 0
// and leave outVerts unmodified.  The guard in tessellateHUD checks
// uiSystem.isGlyphTableBuilt() and returns early, ensuring callers can safely
// pass a UISystem that was default-constructed without Vulkan init.
TEST(MetricsTest, HUDTessellation_ReturnsZeroForEmptyUISystem)
{
    // Intentionally do NOT call sys.buildGlyphTable().
    UISystem sys;

    Metrics metrics;
    std::vector<UIVertex> verts;
    // Pre-populate with a sentinel so we can verify the vector is unchanged.
    const UIVertex sentinel{{9.0f, 9.0f}, {0.5f, 0.5f}};
    verts.push_back(sentinel);

    uint32_t count = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts);

    EXPECT_EQ(count, 0u)
        << "tessellateHUD must return 0 when glyph table has not been built";
    ASSERT_EQ(verts.size(), 1u)
        << "tessellateHUD must not append vertices when glyph table is not built";
    EXPECT_FLOAT_EQ(verts[0].pos.x, sentinel.pos.x)
        << "pre-existing vertex pos.x was modified";
    EXPECT_FLOAT_EQ(verts[0].pos.y, sentinel.pos.y)
        << "pre-existing vertex pos.y was modified";
}

// ---------------------------------------------------------------------------
// MetricsTest — every appended vertex position is within the HUD region
// ---------------------------------------------------------------------------

// tessellateHUD places HUD lines starting at leftMargin (8.0f) in both axes.
// Every appended vertex must have pos.x >= 8.0f and pos.y >= 8.0f, catching
// regressions where line offsets are computed incorrectly and vertices land at
// negative or zero coordinates.
TEST(MetricsTest, HUDTessellation_AppendedVertexPositions_InHUDRegion)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;
    metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts);

    ASSERT_FALSE(verts.empty()) << "tessellateHUD produced no vertices";

    constexpr float leftMargin = 8.0f;
    for (size_t i = 0; i < verts.size(); ++i) {
        EXPECT_GE(verts[i].pos.x, leftMargin)
            << "vertex[" << i << "].pos.x = " << verts[i].pos.x
            << " is below leftMargin=" << leftMargin;
        EXPECT_GE(verts[i].pos.y, leftMargin)
            << "vertex[" << i << "].pos.y = " << verts[i].pos.y
            << " is below leftMargin=" << leftMargin;
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — every appended vertex UV is in the unit square [0, 1]
// ---------------------------------------------------------------------------

// After tessellateHUD() with a properly built glyph table, all UV coordinates
// must lie in [0.0f, 1.0f], ensuring the atlas lookup table never produces
// out-of-range UVs under normal operation.
TEST(MetricsTest, HUDTessellation_AppendedVertexUVs_InUnitSquare)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;
    metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts);

    ASSERT_FALSE(verts.empty()) << "tessellateHUD produced no vertices";

    for (size_t i = 0; i < verts.size(); ++i) {
        EXPECT_GE(verts[i].uv.x, 0.0f)
            << "vertex[" << i << "].uv.x = " << verts[i].uv.x << " < 0";
        EXPECT_LE(verts[i].uv.x, 1.0f)
            << "vertex[" << i << "].uv.x = " << verts[i].uv.x << " > 1";
        EXPECT_GE(verts[i].uv.y, 0.0f)
            << "vertex[" << i << "].uv.y = " << verts[i].uv.y << " < 0";
        EXPECT_LE(verts[i].uv.y, 1.0f)
            << "vertex[" << i << "].uv.y = " << verts[i].uv.y << " > 1";
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — HUD tessellation line height spacing
// ---------------------------------------------------------------------------

// Each HUD line is tessellated at y = leftMargin + lineIndex * lineHeight,
// where lineHeight == 40.0f (GLYPH_CELL=32 + 8px spacing).
// This test verifies that the first vertex of each line's first character
// has the expected y-coordinate, catching regressions in the line-offset
// computation where a fixed y or wrong lineHeight is used.
TEST(MetricsTest, HUDTessellation_LineHeightSpacing_VerticalSeparation)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;
    metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts);

    // Fresh Metrics, RenderMode::Direct, 4 MSAA samples → character counts:
    //   Line 0: "Mode: DIRECT"     = 12 chars  →  72 vertices (offset   0)
    //   Line 1: "Frame: 0.0 ms"    = 13 chars  →  78 vertices (offset  72)
    //   Line 2: "GPU Mem: 0.0 MB"  = 15 chars  →  90 vertices (offset 150)
    //   Line 3: "MSAA: 4x"         =  8 chars  →  48 vertices (offset 240)
    constexpr size_t line0Start = 0;
    constexpr size_t line1Start = 72;   // 12 * 6
    constexpr size_t line2Start = 150;  // (12 + 13) * 6
    constexpr size_t line3Start = 240;  // (12 + 13 + 15) * 6
    ASSERT_GE(verts.size(), line3Start + 6u)
        << "tessellateHUD produced too few vertices to check all line starts";

    // Line-height constants from metrics.cpp.
    constexpr float leftMargin = 8.0f;
    constexpr float lineHeight = 40.0f;  // GLYPH_CELL(32) + 8px spacing

    const float expectedY0 = leftMargin + 0 * lineHeight;  // 8.0f
    const float expectedY1 = leftMargin + 1 * lineHeight;  // 48.0f
    const float expectedY2 = leftMargin + 2 * lineHeight;  // 88.0f
    const float expectedY3 = leftMargin + 3 * lineHeight;  // 128.0f

    // Each line's first vertex is TL of the first character quad; its y must
    // equal the expected y for that line.
    EXPECT_NEAR(verts[line0Start].pos.y, expectedY0, 1e-5f)
        << "Line 0 TL y != " << expectedY0;
    EXPECT_NEAR(verts[line1Start].pos.y, expectedY1, 1e-5f)
        << "Line 1 TL y != " << expectedY1;
    EXPECT_NEAR(verts[line2Start].pos.y, expectedY2, 1e-5f)
        << "Line 2 TL y != " << expectedY2;
    EXPECT_NEAR(verts[line3Start].pos.y, expectedY3, 1e-5f)
        << "Line 3 TL y != " << expectedY3;

    // Cross-check: successive line y-values must differ by exactly lineHeight.
    float diff01 = verts[line1Start].pos.y - verts[line0Start].pos.y;
    float diff12 = verts[line2Start].pos.y - verts[line1Start].pos.y;
    float diff23 = verts[line3Start].pos.y - verts[line2Start].pos.y;

    EXPECT_NEAR(diff01, lineHeight, 1e-5f) << "Line 0->1 y gap != lineHeight";
    EXPECT_NEAR(diff12, lineHeight, 1e-5f) << "Line 1->2 y gap != lineHeight";
    EXPECT_NEAR(diff23, lineHeight, 1e-5f) << "Line 2->3 y gap != lineHeight";
}

// ---------------------------------------------------------------------------
// MetricsTest — 5th line y-position when inputModeStr is supplied
// ---------------------------------------------------------------------------

// When inputModeStr is supplied, tessellateHUD places the 5th line at
// y = leftMargin + 4 * lineHeight.  This test verifies the TL vertex of the
// 5th line's first character has exactly that y-coordinate, catching
// regressions where the optional line uses a wrong index (e.g. 3 instead of 4)
// or a hard-coded offset that diverges from the formula.
TEST(MetricsTest, HUDTessellation_LineHeightSpacing_WithInputModeStr_FifthLineSeparation)
{
    UISystem sys;
    sys.buildGlyphTable();

    // Fresh Metrics: averageFrameMs()==0.0f, gpuAllocatedBytes()==0.
    // With RenderMode::Direct, msaaSamples=4, inputModeStr="Input: keyboard":
    //   Line 0: "Mode: DIRECT"     = 12 chars  →  72 vertices (offset   0)
    //   Line 1: "Frame: 0.0 ms"    = 13 chars  →  78 vertices (offset  72)
    //   Line 2: "GPU Mem: 0.0 MB"  = 15 chars  →  90 vertices (offset 150)
    //   Line 3: "MSAA: 4x"         =  8 chars  →  48 vertices (offset 240)
    //   Line 4: "Input: keyboard"  = 15 chars  →  90 vertices (offset 288)
    const char* inputStr = "Input: keyboard";
    Metrics metrics;
    std::vector<UIVertex> verts;
    metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts, inputStr);

    // Line 4 starts after lines 0-3: (12+13+15+8)*6 = 288 vertices.
    constexpr size_t line4Start = (12u + 13u + 15u + 8u) * 6u; // 288

    ASSERT_GE(verts.size(), line4Start + 6u)
        << "tessellateHUD produced too few vertices to check line 4 start";

    // Expected y for line 4: leftMargin + 4 * lineHeight = 8.0f + 4 * 40.0f = 168.0f
    constexpr float leftMargin = 8.0f;
    constexpr float lineHeight = 40.0f;
    const float expectedY4 = leftMargin + 4.0f * lineHeight; // 168.0f

    EXPECT_NEAR(verts[line4Start].pos.y, expectedY4, 1e-5f)
        << "Line 4 TL y != " << expectedY4
        << " — optional inputModeStr line may be using wrong index or hard-coded offset";
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

// ---------------------------------------------------------------------------
// MetricsTest — averageFrameMs returns exactly zero when no frames recorded
// ---------------------------------------------------------------------------

// A freshly constructed Metrics object must return exactly 0.0f from
// averageFrameMs() before any beginFrame/endFrame calls.  This guards against
// using uninitialized ring-buffer data (garbage values from the stack) as
// the average, which would silently corrupt frame timing metrics.
TEST(MetricsTest, AverageFrameMs_ExactlyZeroWhenNoFrames)
{
    Metrics m;
    // Pre-condition: no frames recorded yet.
    EXPECT_FLOAT_EQ(m.averageFrameMs(), 0.0f)
        << "averageFrameMs() must return exactly 0.0f on a fresh Metrics object, "
           "not uninitialized ring-buffer data";
}
