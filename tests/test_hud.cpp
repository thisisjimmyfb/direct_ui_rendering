#include <gtest/gtest.h>
#include "metrics.h"
#include "ui_system.h"
#include <cstring>

// ---------------------------------------------------------------------------
// MetricsTest — HUD tessellation vertex count
// ---------------------------------------------------------------------------

// tessellateHUD formats 8 lines (no inputModeStr) and tessellates them via
// UISystem::tessellateString, which produces 6 vertices per character.
// The total vertex count must equal 6 * (sum of character counts across all
// lines) for a known, fixed set of inputs.
TEST(MetricsTest, HUDTessellation_VertexCountMatchesLineCount)
{
    UISystem sys;
    sys.buildGlyphTable();

    // Fresh Metrics: averageFrameMs()==0.0f, gpuAllocatedBytes()==0.
    // With RenderMode::Direct, msaaSamples=4, no inputModeStr:
    //   Line 0: "Mode: DIRECT"              = 12 chars
    //   Line 1: "  [Space] toggle render mode" = 28 chars
    //   Line 2: "  [+] [-] adjust depth bias"  = 27 chars
    //   Line 3: "  [Left] [Right] quad width"  = 27 chars
    //   Line 4: "  [O] [P] quad height"        = 21 chars
    //   Line 5: "Frame: 0.0 ms"                = 13 chars
    //   Line 6: "GPU Mem: 0.0 MB"              = 15 chars
    //   Line 7: "MSAA: 4x"                     =  8 chars
    //   Total                                   = 151 chars  ->  906 vertices
    Metrics metrics;
    std::vector<UIVertex> verts;
    uint32_t count = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts);

    constexpr int expectedChars = 12 + 28 + 27 + 27 + 21 + 13 + 15 + 8;
    constexpr uint32_t expectedVerts = 6u * static_cast<uint32_t>(expectedChars);

    EXPECT_EQ(count, expectedVerts)
        << "tessellateHUD returned wrong vertex count for 8 HUD lines";
    EXPECT_EQ(static_cast<uint32_t>(verts.size()), expectedVerts)
        << "outVerts.size() does not match the returned count";
}

// ---------------------------------------------------------------------------
// MetricsTest — Traditional mode produces correct vertex count
// ---------------------------------------------------------------------------

// tessellateHUD with RenderMode::Traditional formats the mode line as
// "Mode: TRADITIONAL" (17 chars) rather than "Mode: DIRECT" (12 chars).
// The total vertex count must equal 6 * sum-of-chars for the eight known lines.
TEST(MetricsTest, HUDTessellation_TraditionalMode_VertexCount)
{
    UISystem sys;
    sys.buildGlyphTable();

    // Fresh Metrics: averageFrameMs()==0.0f, gpuAllocatedBytes()==0.
    // With RenderMode::Traditional, msaaSamples=4, no inputModeStr:
    //   Line 0: "Mode: TRADITIONAL"           = 17 chars
    //   Line 1: "  [Space] toggle render mode" = 28 chars
    //   Line 2: "  [+] [-] adjust depth bias"  = 27 chars
    //   Line 3: "  [Left] [Right] quad width"  = 27 chars
    //   Line 4: "  [O] [P] quad height"        = 21 chars
    //   Line 5: "Frame: 0.0 ms"                = 13 chars
    //   Line 6: "GPU Mem: 0.0 MB"              = 15 chars
    //   Line 7: "MSAA: 4x"                     =  8 chars
    //   Total                                   = 156 chars  ->  936 vertices
    Metrics metrics;
    std::vector<UIVertex> verts;
    uint32_t count = metrics.tessellateHUD(sys, RenderMode::Traditional, 4u, verts);

    constexpr int expectedChars = 17 + 28 + 27 + 27 + 21 + 13 + 15 + 8;
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
// (between quad height and frame time) and the returned vertex count must equal
// the 8-line total plus 6 * strlen(inputModeStr).
TEST(MetricsTest, HUDTessellation_WithInputModeStr_AddsExtraLine)
{
    UISystem sys;
    sys.buildGlyphTable();

    // Fresh Metrics: averageFrameMs()==0.0f, gpuAllocatedBytes()==0.
    // With RenderMode::Direct, msaaSamples=4, inputModeStr="Input: CAMERA":
    //   8-line base (same as HUDTessellation_VertexCountMatchesLineCount):
    //     Mode: DIRECT              = 12 chars
    //     [Space] toggle...         = 28 chars
    //     [+] [-] adjust...         = 27 chars
    //     [Left] [Right]...         = 27 chars
    //     [O] [P] quad height       = 21 chars
    //     Frame: 0.0 ms             = 13 chars
    //     GPU Mem: 0.0 MB           = 15 chars
    //     MSAA: 4x                  =  8 chars
    //   Line 5: "Input: CAMERA"         = 13 chars
    //   Total                              = 164 chars  ->  984 vertices
    const char* inputStr = "Input: CAMERA";
    Metrics metrics;
    std::vector<UIVertex> verts;
    uint32_t count = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts, inputStr);

    constexpr int baseLine8Chars = 12 + 28 + 27 + 27 + 21 + 13 + 15 + 8;
    const int extraChars = static_cast<int>(std::strlen(inputStr));
    const uint32_t expectedVerts = 6u * static_cast<uint32_t>(baseLine8Chars + extraChars);

    EXPECT_EQ(count, expectedVerts)
        << "tessellateHUD returned wrong vertex count when inputModeStr is set";
    EXPECT_EQ(static_cast<uint32_t>(verts.size()), expectedVerts)
        << "outVerts.size() does not match the returned count";
}

// ---------------------------------------------------------------------------
// MetricsTest — tessellateHUD appends to a pre-populated outVerts vector
// ---------------------------------------------------------------------------

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
    // produces 918 vertices (same as HUDTessellation_VertexCountMatchesLineCount).
    uint32_t appended = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts);

    constexpr uint32_t expectedAppended = 6u * (12u + 28u + 27u + 27u + 21u + 13u + 15u + 8u); // 906
    EXPECT_EQ(appended, expectedAppended)
        << "tessellateHUD returned wrong appended vertex count";
    EXPECT_EQ(verts.size(), static_cast<size_t>(preExistingCount + expectedAppended))
        << "outVerts was cleared before tessellation — pre-existing vertices lost";
}

// ---------------------------------------------------------------------------
// MetricsTest — pre-existing vertex VALUES are not modified by tessellateHUD
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_PreExistingVertexValues_NotModified)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;

    constexpr uint32_t SENTINEL_COUNT = 5u;

    const UIVertex sentinel{ {1234.0f, 5678.0f}, {0.111f, 0.999f} };
    for (uint32_t i = 0; i < SENTINEL_COUNT; ++i)
        verts.push_back(sentinel);

    uint32_t appended = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts);

    ASSERT_GT(appended, 0u)
        << "tessellateHUD returned 0 vertices — sentinel check is vacuous";

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

TEST(MetricsTest, HUDTessellation_ReturnsZeroForEmptyUISystem)
{
    UISystem sys;

    Metrics metrics;
    std::vector<UIVertex> verts;
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

TEST(MetricsTest, HUDTessellation_LineHeightSpacing_VerticalSeparation)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;
    metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts);

    // Character counts for each line (no inputModeStr):
    //   Line 0: "Mode: DIRECT"              = 12 chars  ->  72 vertices (offset   0)
    //   Line 1: "  [Space] toggle render mode" = 28 chars  -> 168 vertices (offset  72)
    //   Line 2: "  [+] [-] adjust depth bias"  = 27 chars  -> 162 vertices (offset 240)
    //   Line 3: "  [Left] [Right] quad width"  = 27 chars  -> 162 vertices (offset 402)
    //   Line 4: "  [O] [P] quad height"        = 21 chars  -> 138 vertices (offset 564)
    //   Line 5: "Frame: 0.0 ms"                = 13 chars  ->  78 vertices (offset 690)
    //   Line 6: "GPU Mem: 0.0 MB"              = 15 chars  ->  90 vertices (offset 780)
    //   Line 7: "MSAA: 4x"                     =  8 chars  ->  48 vertices (offset 870)
    constexpr size_t line0Start = 0;
    constexpr size_t line1Start = 72;        // 12 * 6
    constexpr size_t line2Start = 240;       // (12 + 28) * 6
    constexpr size_t line3Start = 402;       // (12 + 28 + 27) * 6
    constexpr size_t line4Start = 564;       // (12 + 28 + 27 + 27) * 6
    constexpr size_t line5Start = 690;       // (12 + 28 + 27 + 27 + 21) * 6
    constexpr size_t line6Start = 768;       // (12 + 28 + 27 + 27 + 21 + 13) * 6
    constexpr size_t line7Start = 858;       // (12 + 28 + 27 + 27 + 21 + 13 + 15) * 6

    ASSERT_GE(verts.size(), line7Start + 6u)
        << "tessellateHUD produced too few vertices to check all line starts";

    constexpr float leftMargin = 8.0f;
    constexpr float lineHeight = 40.0f;  // GLYPH_CELL(32) + 8px spacing

    const float expectedY0 = leftMargin + 0 * lineHeight;   // 8.0f
    const float expectedY1 = leftMargin + 1 * lineHeight;   // 48.0f
    const float expectedY2 = leftMargin + 2 * lineHeight;   // 88.0f
    const float expectedY3 = leftMargin + 3 * lineHeight;   // 128.0f
    const float expectedY4 = leftMargin + 4 * lineHeight;   // 168.0f
    const float expectedY5 = leftMargin + 5 * lineHeight;   // 208.0f
    const float expectedY6 = leftMargin + 6 * lineHeight;   // 248.0f
    const float expectedY7 = leftMargin + 7 * lineHeight;   // 288.0f

    EXPECT_NEAR(verts[line0Start].pos.y, expectedY0, 1e-5f) << "Line 0 TL y != " << expectedY0;
    EXPECT_NEAR(verts[line1Start].pos.y, expectedY1, 1e-5f) << "Line 1 TL y != " << expectedY1;
    EXPECT_NEAR(verts[line2Start].pos.y, expectedY2, 1e-5f) << "Line 2 TL y != " << expectedY2;
    EXPECT_NEAR(verts[line3Start].pos.y, expectedY3, 1e-5f) << "Line 3 TL y != " << expectedY3;
    EXPECT_NEAR(verts[line4Start].pos.y, expectedY4, 1e-5f) << "Line 4 TL y != " << expectedY4;
    EXPECT_NEAR(verts[line5Start].pos.y, expectedY5, 1e-5f) << "Line 5 TL y != " << expectedY5;
    EXPECT_NEAR(verts[line6Start].pos.y, expectedY6, 1e-5f) << "Line 6 TL y != " << expectedY6;
    EXPECT_NEAR(verts[line7Start].pos.y, expectedY7, 1e-5f) << "Line 7 TL y != " << expectedY7;

    // Cross-check: successive line y-values must differ by exactly lineHeight.
    float diff01 = verts[line1Start].pos.y - verts[line0Start].pos.y;
    float diff12 = verts[line2Start].pos.y - verts[line1Start].pos.y;
    float diff23 = verts[line3Start].pos.y - verts[line2Start].pos.y;
    float diff34 = verts[line4Start].pos.y - verts[line3Start].pos.y;
    float diff45 = verts[line5Start].pos.y - verts[line4Start].pos.y;
    float diff56 = verts[line6Start].pos.y - verts[line5Start].pos.y;
    float diff67 = verts[line7Start].pos.y - verts[line6Start].pos.y;

    EXPECT_NEAR(diff01, lineHeight, 1e-5f) << "Line 0->1 y gap != lineHeight";
    EXPECT_NEAR(diff12, lineHeight, 1e-5f) << "Line 1->2 y gap != lineHeight";
    EXPECT_NEAR(diff23, lineHeight, 1e-5f) << "Line 2->3 y gap != lineHeight";
    EXPECT_NEAR(diff34, lineHeight, 1e-5f) << "Line 3->4 y gap != lineHeight";
    EXPECT_NEAR(diff45, lineHeight, 1e-5f) << "Line 4->5 y gap != lineHeight";
    EXPECT_NEAR(diff56, lineHeight, 1e-5f) << "Line 5->6 y gap != lineHeight";
    EXPECT_NEAR(diff67, lineHeight, 1e-5f) << "Line 6->7 y gap != lineHeight";
}

// ---------------------------------------------------------------------------
// MetricsTest — 5th line y-position when inputModeStr is supplied
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_LineHeightSpacing_WithInputModeStr_FifthLineSeparation)
{
    UISystem sys;
    sys.buildGlyphTable();

    const char* inputStr = "Input: CAMERA";
    Metrics metrics;
    std::vector<UIVertex> verts;
    metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts, inputStr);

    // Line 5 starts after lines 0-4: (12+28+27+27+21)*6 = 690 vertices.
    constexpr size_t line5Start = (12u + 28u + 27u + 27u + 21u) * 6u; // 702

    ASSERT_GE(verts.size(), line5Start + 6u)
        << "tessellateHUD produced too few vertices to check line 5 start";

    constexpr float leftMargin = 8.0f;
    constexpr float lineHeight = 40.0f;
    const float expectedY5 = leftMargin + 5.0f * lineHeight; // 208.0f

    EXPECT_NEAR(verts[line5Start].pos.y, expectedY5, 1e-5f)
        << "Line 5 TL y != " << expectedY5;
}

// ---------------------------------------------------------------------------
// MetricsTest — all lines follow the arithmetic y-sequence when inputModeStr
// is supplied
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_WithInputModeStr_AllLinesYSpacing)
{
    UISystem sys;
    sys.buildGlyphTable();

    const char* inputStr = "Input: CAMERA";
    Metrics metrics;
    std::vector<UIVertex> verts;
    metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts, inputStr);

    constexpr size_t line0Start = 0u;
    constexpr size_t line1Start = 12u * 6u;                        // 72
    constexpr size_t line2Start = (12u + 28u) * 6u;               // 240
    constexpr size_t line3Start = (12u + 28u + 27u) * 6u;         // 402
    constexpr size_t line4Start = (12u + 28u + 27u + 27u) * 6u;   // 564
    constexpr size_t line5Start = (12u + 28u + 27u + 27u + 21u) * 6u; // 702

    ASSERT_GE(verts.size(), line5Start + 6u)
        << "tessellateHUD produced too few vertices to check all lines";

    constexpr float leftMargin = 8.0f;
    constexpr float lineHeight = 40.0f;

    const size_t lineStarts[6] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start };
    for (int i = 0; i < 6; ++i) {
        const float expectedY = leftMargin + static_cast<float>(i) * lineHeight;
        EXPECT_NEAR(verts[lineStarts[i]].pos.y, expectedY, 1e-5f)
            << "Line " << i << " TL y != " << expectedY;
    }

    for (int i = 0; i < 5; ++i) {
        const float diff = verts[lineStarts[i + 1]].pos.y - verts[lineStarts[i]].pos.y;
        EXPECT_NEAR(diff, lineHeight, 1e-5f)
            << "Line " << i << " -> " << (i + 1) << " y gap != lineHeight";
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — all lines share leftMargin x-position when inputModeStr
// is supplied
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_WithInputModeStr_AllLinesXPositions)
{
    UISystem sys;
    sys.buildGlyphTable();

    const char* inputStr = "Input: CAMERA";
    Metrics metrics;
    std::vector<UIVertex> verts;
    metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts, inputStr);

    constexpr size_t line0Start = 0u;
    constexpr size_t line1Start = 12u * 6u;                        // 72
    constexpr size_t line2Start = (12u + 28u) * 6u;               // 240
    constexpr size_t line3Start = (12u + 28u + 27u) * 6u;         // 402
    constexpr size_t line4Start = (12u + 28u + 27u + 27u) * 6u;   // 564
    constexpr size_t line5Start = (12u + 28u + 27u + 27u + 21u) * 6u; // 702

    ASSERT_GE(verts.size(), line5Start + 6u)
        << "tessellateHUD produced too few vertices to check all lines";

    constexpr float leftMargin = 8.0f;

    const size_t lineStarts[6] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start };
    for (int i = 0; i < 6; ++i) {
        EXPECT_NEAR(verts[lineStarts[i]].pos.x, leftMargin, 1e-5f)
            << "Line " << i << " TL x != " << leftMargin;
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — all eight lines follow the arithmetic y-sequence when no
// inputModeStr is supplied
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_EightLines_AllLinesYSpacing)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;
    metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts, nullptr);

    constexpr size_t line0Start = 0u;
    constexpr size_t line1Start = 12u * 6u;                        // 72
    constexpr size_t line2Start = (12u + 28u) * 6u;               // 240
    constexpr size_t line3Start = (12u + 28u + 27u) * 6u;         // 402
    constexpr size_t line4Start = (12u + 28u + 27u + 27u) * 6u;   // 564
    constexpr size_t line5Start = (12u + 28u + 27u + 27u + 21u) * 6u; // 702
    constexpr size_t line6Start = (12u + 28u + 27u + 27u + 21u + 13u) * 6u; // 768
    constexpr size_t line7Start = (12u + 28u + 27u + 27u + 21u + 13u + 15u) * 6u; // 858

    ASSERT_GE(verts.size(), line7Start + 6u)
        << "tessellateHUD produced too few vertices to check all eight lines";

    constexpr float leftMargin = 8.0f;
    constexpr float lineHeight = 40.0f;

    const size_t lineStarts[8] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start, line7Start };
    for (int i = 0; i < 8; ++i) {
        const float expectedY = leftMargin + static_cast<float>(i) * lineHeight;
        EXPECT_NEAR(verts[lineStarts[i]].pos.y, expectedY, 1e-5f)
            << "Line " << i << " TL y != " << expectedY;
    }

    for (int i = 0; i < 7; ++i) {
        const float diff = verts[lineStarts[i + 1]].pos.y - verts[lineStarts[i]].pos.y;
        EXPECT_NEAR(diff, lineHeight, 1e-5f)
            << "Line " << i << " -> " << (i + 1) << " y gap != lineHeight";
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — all eight lines share leftMargin x-position when no
// inputModeStr is supplied
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_EightLines_AllLinesXPositions)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;
    metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts, nullptr);

    constexpr size_t line0Start = 0u;
    constexpr size_t line1Start = 12u * 6u;                        // 72
    constexpr size_t line2Start = (12u + 28u) * 6u;               // 240
    constexpr size_t line3Start = (12u + 28u + 27u) * 6u;         // 402
    constexpr size_t line4Start = (12u + 28u + 27u + 27u) * 6u;   // 564
    constexpr size_t line5Start = (12u + 28u + 27u + 27u + 21u) * 6u; // 702
    constexpr size_t line6Start = (12u + 28u + 27u + 27u + 21u + 13u) * 6u; // 768
    constexpr size_t line7Start = (12u + 28u + 27u + 27u + 21u + 13u + 15u) * 6u; // 858

    ASSERT_GE(verts.size(), line7Start + 6u)
        << "tessellateHUD produced too few vertices to check all eight lines";

    constexpr float leftMargin = 8.0f;

    const size_t lineStarts[8] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start, line7Start };
    for (int i = 0; i < 8; ++i) {
        EXPECT_NEAR(verts[lineStarts[i]].pos.x, leftMargin, 1e-5f)
            << "Line " << i << " TL x != " << leftMargin;
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — Traditional mode: all eight lines follow the arithmetic
// y-sequence
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_TraditionalMode_AllLinesYSpacing)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;
    metrics.tessellateHUD(sys, RenderMode::Traditional, 4u, verts, nullptr);

    constexpr size_t line0Start = 0u;
    constexpr size_t line1Start = 17u * 6u;                             // 102
    constexpr size_t line2Start = (17u + 28u) * 6u;                    // 270
    constexpr size_t line3Start = (17u + 28u + 27u) * 6u;              // 432
    constexpr size_t line4Start = (17u + 28u + 27u + 27u) * 6u;        // 594
    constexpr size_t line5Start = (17u + 28u + 27u + 27u + 21u) * 6u;  // 720
    constexpr size_t line6Start = (17u + 28u + 27u + 27u + 21u + 13u) * 6u; // 798
    constexpr size_t line7Start = (17u + 28u + 27u + 27u + 21u + 13u + 15u) * 6u; // 888

    ASSERT_GE(verts.size(), line7Start + 6u)
        << "tessellateHUD (Traditional) produced too few vertices to check all eight lines";

    constexpr float leftMargin = 8.0f;
    constexpr float lineHeight = 40.0f;

    const size_t lineStarts[8] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start, line7Start };
    for (int i = 0; i < 8; ++i) {
        const float expectedY = leftMargin + static_cast<float>(i) * lineHeight;
        EXPECT_NEAR(verts[lineStarts[i]].pos.y, expectedY, 1e-5f)
            << "Traditional mode Line " << i << " TL y != " << expectedY;
    }

    for (int i = 0; i < 7; ++i) {
        const float diff = verts[lineStarts[i + 1]].pos.y - verts[lineStarts[i]].pos.y;
        EXPECT_NEAR(diff, lineHeight, 1e-5f)
            << "Traditional mode Line " << i << " -> " << (i + 1) << " y gap != lineHeight";
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — Traditional mode: all eight lines start at leftMargin x
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_TraditionalMode_AllLinesXPositions)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;
    metrics.tessellateHUD(sys, RenderMode::Traditional, 4u, verts, nullptr);

    constexpr size_t line0Start = 0u;
    constexpr size_t line1Start = 17u * 6u;                             // 102
    constexpr size_t line2Start = (17u + 28u) * 6u;                    // 270
    constexpr size_t line3Start = (17u + 28u + 27u) * 6u;              // 432
    constexpr size_t line4Start = (17u + 28u + 27u + 27u) * 6u;        // 594
    constexpr size_t line5Start = (17u + 28u + 27u + 27u + 21u) * 6u;  // 720
    constexpr size_t line6Start = (17u + 28u + 27u + 27u + 21u + 13u) * 6u; // 798
    constexpr size_t line7Start = (17u + 28u + 27u + 27u + 21u + 13u + 15u) * 6u; // 888

    ASSERT_GE(verts.size(), line7Start + 6u)
        << "tessellateHUD (Traditional) produced too few vertices to check all eight lines";

    constexpr float leftMargin = 8.0f;

    const size_t lineStarts[8] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start, line7Start };
    for (int i = 0; i < 8; ++i) {
        EXPECT_NEAR(verts[lineStarts[i]].pos.x, leftMargin, 1e-5f)
            << "Traditional mode Line " << i << " TL x != " << leftMargin;
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — Traditional mode with inputModeStr: all lines start at
// leftMargin x-position
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_TraditionalMode_WithInputModeStr_AllLinesXPositions)
{
    UISystem sys;
    sys.buildGlyphTable();

    const char* inputStr = "Input: CAMERA";
    Metrics metrics;
    std::vector<UIVertex> verts;
    metrics.tessellateHUD(sys, RenderMode::Traditional, 4u, verts, inputStr);

    constexpr size_t line0Start = 0u;
    constexpr size_t line1Start = 17u * 6u;                             // 102
    constexpr size_t line2Start = (17u + 28u) * 6u;                    // 270
    constexpr size_t line3Start = (17u + 28u + 27u) * 6u;              // 432
    constexpr size_t line4Start = (17u + 28u + 27u + 27u) * 6u;        // 594
    constexpr size_t line5Start = (17u + 28u + 27u + 27u + 21u) * 6u;  // 720

    ASSERT_GE(verts.size(), line5Start + 6u)
        << "tessellateHUD (Traditional+inputModeStr) produced too few vertices to check all lines";

    constexpr float leftMargin = 8.0f;

    const size_t lineStarts[6] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start };
    for (int i = 0; i < 6; ++i) {
        EXPECT_NEAR(verts[lineStarts[i]].pos.x, leftMargin, 1e-5f)
            << "Traditional mode Line " << i << " TL x != " << leftMargin;
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — Traditional mode with inputModeStr: all lines follow the
// arithmetic y-sequence
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_TraditionalMode_WithInputModeStr_AllLinesYSpacing)
{
    UISystem sys;
    sys.buildGlyphTable();

    const char* inputStr = "Input: CAMERA";
    Metrics metrics;
    std::vector<UIVertex> verts;
    metrics.tessellateHUD(sys, RenderMode::Traditional, 4u, verts, inputStr);

    constexpr size_t line0Start = 0u;
    constexpr size_t line1Start = 17u * 6u;                             // 102
    constexpr size_t line2Start = (17u + 28u) * 6u;                    // 270
    constexpr size_t line3Start = (17u + 28u + 27u) * 6u;              // 432
    constexpr size_t line4Start = (17u + 28u + 27u + 27u) * 6u;        // 594
    constexpr size_t line5Start = (17u + 28u + 27u + 27u + 21u) * 6u;  // 720

    ASSERT_GE(verts.size(), line5Start + 6u)
        << "tessellateHUD (Traditional+inputModeStr) produced too few vertices to check all lines";

    constexpr float leftMargin = 8.0f;
    constexpr float lineHeight = 40.0f;

    const size_t lineStarts[6] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start };
    for (int i = 0; i < 6; ++i) {
        const float expectedY = leftMargin + static_cast<float>(i) * lineHeight;
        EXPECT_NEAR(verts[lineStarts[i]].pos.y, expectedY, 1e-5f)
            << "Traditional mode Line " << i << " TL y != " << expectedY;
    }

    for (int i = 0; i < 5; ++i) {
        const float diff = verts[lineStarts[i + 1]].pos.y - verts[lineStarts[i]].pos.y;
        EXPECT_NEAR(diff, lineHeight, 1e-5f)
            << "Traditional mode Line " << i << " -> " << (i + 1) << " y gap != lineHeight";
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — empty (non-null) inputModeStr with Direct mode: lines 0–7
// each start at x = leftMargin
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_EmptyInputModeStr_LinesXPositions)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;
    metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts, "");

    constexpr size_t line0Start = 0u;
    constexpr size_t line1Start = 12u * 6u;                        // 72
    constexpr size_t line2Start = (12u + 28u) * 6u;               // 240
    constexpr size_t line3Start = (12u + 28u + 27u) * 6u;         // 402
    constexpr size_t line4Start = (12u + 28u + 27u + 27u) * 6u;   // 564
    constexpr size_t line5Start = (12u + 28u + 27u + 27u + 21u) * 6u; // 702
    constexpr size_t line6Start = (12u + 28u + 27u + 27u + 21u + 13u) * 6u; // 768
    constexpr size_t line7Start = (12u + 28u + 27u + 27u + 21u + 13u + 15u) * 6u; // 858

    ASSERT_GE(verts.size(), line7Start + 6u)
        << "tessellateHUD (empty inputModeStr) produced too few vertices to check all eight base lines";

    constexpr float leftMargin = 8.0f;

    const size_t lineStarts[8] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start, line7Start };
    for (int i = 0; i < 8; ++i) {
        EXPECT_NEAR(verts[lineStarts[i]].pos.x, leftMargin, 1e-5f)
            << "Line " << i << " TL x != " << leftMargin;
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — Traditional mode with empty inputModeStr: vertex count and
// lines 0–7 y-positions unaffected
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_TraditionalMode_EmptyInputModeStr_FiveLinesYSpacing)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;
    uint32_t count = metrics.tessellateHUD(sys, RenderMode::Traditional, 4u, verts, "");

    // The 5th line contributes 0 vertices; total must match the 8-line Traditional base.
    constexpr uint32_t expectedVerts = (17u + 28u + 27u + 27u + 21u + 13u + 15u + 8u) * 6u;  // 936
    EXPECT_EQ(count, expectedVerts)
        << "tessellateHUD (Traditional, empty inputModeStr) returned " << count
        << " vertices; expected " << expectedVerts;
    EXPECT_EQ(static_cast<uint32_t>(verts.size()), expectedVerts)
        << "outVerts.size() does not match the returned count";

    constexpr size_t line0Start = 0u;
    constexpr size_t line1Start = 17u * 6u;                             // 102
    constexpr size_t line2Start = (17u + 28u) * 6u;                    // 270
    constexpr size_t line3Start = (17u + 28u + 27u) * 6u;              // 432
    constexpr size_t line4Start = (17u + 28u + 27u + 27u) * 6u;        // 594
    constexpr size_t line5Start = (17u + 28u + 27u + 27u + 21u) * 6u;  // 720
    constexpr size_t line6Start = (17u + 28u + 27u + 27u + 21u + 13u) * 6u; // 798
    constexpr size_t line7Start = (17u + 28u + 27u + 27u + 21u + 13u + 15u) * 6u; // 888

    ASSERT_GE(verts.size(), line7Start + 6u)
        << "tessellateHUD (Traditional, empty inputModeStr) produced too few vertices";

    constexpr float leftMargin = 8.0f;
    constexpr float lineHeight = 40.0f;

    const size_t lineStarts[8] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start, line7Start };
    for (int i = 0; i < 8; ++i) {
        const float expectedY = leftMargin + static_cast<float>(i) * lineHeight;
        EXPECT_NEAR(verts[lineStarts[i]].pos.y, expectedY, 1e-5f)
            << "Traditional mode Line " << i << " TL y != " << expectedY;
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — Traditional mode with empty inputModeStr: lines 0–7 each start
// at x = leftMargin
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_TraditionalMode_EmptyInputModeStr_LinesXPositions)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;
    metrics.tessellateHUD(sys, RenderMode::Traditional, 4u, verts, "");

    constexpr size_t line0Start = 0u;
    constexpr size_t line1Start = 17u * 6u;                             // 102
    constexpr size_t line2Start = (17u + 28u) * 6u;                    // 270
    constexpr size_t line3Start = (17u + 28u + 27u) * 6u;              // 432
    constexpr size_t line4Start = (17u + 28u + 27u + 27u) * 6u;        // 594
    constexpr size_t line5Start = (17u + 28u + 27u + 27u + 21u) * 6u;  // 720
    constexpr size_t line6Start = (17u + 28u + 27u + 27u + 21u + 13u) * 6u; // 798
    constexpr size_t line7Start = (17u + 28u + 27u + 27u + 21u + 13u + 15u) * 6u; // 888

    ASSERT_GE(verts.size(), line7Start + 6u)
        << "tessellateHUD (Traditional, empty inputModeStr) produced too few vertices";

    constexpr float leftMargin = 8.0f;

    const size_t lineStarts[8] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start, line7Start };
    for (int i = 0; i < 8; ++i) {
        EXPECT_NEAR(verts[lineStarts[i]].pos.x, leftMargin, 1e-5f)
            << "Traditional mode Line " << i << " TL x != " << leftMargin;
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — non-standard MSAA sample count produces a longer MSAA line
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_NonStandardMSAA_VertexCountReflectsLongerString)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics4;
    std::vector<UIVertex> verts4;
    uint32_t count4 = metrics4.tessellateHUD(sys, RenderMode::Direct, 4u, verts4);

    Metrics metrics16;
    std::vector<UIVertex> verts16;
    uint32_t count16 = metrics16.tessellateHUD(sys, RenderMode::Direct, 16u, verts16);

    ASSERT_GT(count4, 0u)
        << "msaaSamples=4 tessellateHUD returned 0 — baseline call failed";
    EXPECT_EQ(count16, count4 + 6u)
        << "msaaSamples=16 vertex count should be exactly 6 more than msaaSamples=4 count";
    EXPECT_EQ(static_cast<uint32_t>(verts16.size()), count16)
        << "outVerts.size() does not match the returned count for msaaSamples=16";
}

// ---------------------------------------------------------------------------
// MetricsTest — single-digit MSAA sample counts produce the same vertex count
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_SingleDigitMSAA_SameVertexCount)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics4;
    std::vector<UIVertex> verts4;
    uint32_t count4 = metrics4.tessellateHUD(sys, RenderMode::Direct, 4u, verts4);

    Metrics metrics8;
    std::vector<UIVertex> verts8;
    uint32_t count8 = metrics8.tessellateHUD(sys, RenderMode::Direct, 8u, verts8);

    ASSERT_GT(count4, 0u)
        << "msaaSamples=4 tessellateHUD returned 0 — baseline call failed";
    ASSERT_GT(count8, 0u)
        << "msaaSamples=8 tessellateHUD returned 0";
    EXPECT_EQ(count8, count4)
        << "msaaSamples=8 vertex count differs from msaaSamples=4 count";
    EXPECT_EQ(static_cast<uint32_t>(verts8.size()), count8)
        << "outVerts.size() does not match the returned count for msaaSamples=8";
}

// ---------------------------------------------------------------------------
// MetricsTest — 3-digit MSAA sample count produces correct vertex count
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_LargeMSAASampleCount_NoBufferOverflow)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;
    uint32_t count = metrics.tessellateHUD(sys, RenderMode::Direct, 999u, verts);

    constexpr uint32_t expectedVerts = (12u + 28u + 27u + 27u + 21u + 13u + 15u + 10u) * 6u;  // 918

    ASSERT_GT(count, 0u)
        << "tessellateHUD returned 0 for msaaSamples=999";
    EXPECT_EQ(count, expectedVerts)
        << "msaaSamples=999 vertex count should be " << expectedVerts;
    EXPECT_EQ(static_cast<uint32_t>(verts.size()), count)
        << "outVerts.size() does not match the returned count for msaaSamples=999";
}

// ---------------------------------------------------------------------------
// MetricsTest — empty inputModeStr: 8 lines, 5th line 0 chars, lines 0-7
// y-positions unaffected
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_EmptyInputModeStr_FiveLinesYSpacing)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;
    uint32_t count = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts, "");

    // The 5th line contributes 0 vertices; total must match the 8-line Direct base.
    constexpr uint32_t expectedVerts = (12u + 28u + 27u + 27u + 21u + 13u + 15u + 8u) * 6u;  // 906
    EXPECT_EQ(count, expectedVerts)
        << "tessellateHUD (Direct, empty inputModeStr) returned " << count
        << " vertices; expected " << expectedVerts;
    EXPECT_EQ(static_cast<uint32_t>(verts.size()), expectedVerts)
        << "outVerts.size() does not match the returned count";

    constexpr size_t line0Start = 0u;
    constexpr size_t line1Start = 12u * 6u;                        // 72
    constexpr size_t line2Start = (12u + 28u) * 6u;               // 240
    constexpr size_t line3Start = (12u + 28u + 27u) * 6u;         // 402
    constexpr size_t line4Start = (12u + 28u + 27u + 27u) * 6u;   // 564
    constexpr size_t line5Start = (12u + 28u + 27u + 27u + 21u) * 6u; // 702
    constexpr size_t line6Start = (12u + 28u + 27u + 27u + 21u + 13u) * 6u; // 768
    constexpr size_t line7Start = (12u + 28u + 27u + 27u + 21u + 13u + 15u) * 6u; // 858

    ASSERT_GE(verts.size(), line7Start + 6u)
        << "tessellateHUD (Direct, empty inputModeStr) produced too few vertices";

    constexpr float leftMargin = 8.0f;
    constexpr float lineHeight = 40.0f;

    const size_t lineStarts[8] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start, line7Start };
    for (int i = 0; i < 8; ++i) {
        const float expectedY = leftMargin + static_cast<float>(i) * lineHeight;
        EXPECT_NEAR(verts[lineStarts[i]].pos.y, expectedY, 1e-5f)
            << "Direct mode Line " << i << " TL y != " << expectedY;
    }
}
