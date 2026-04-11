#include <gtest/gtest.h>
#include "metrics.h"
#include "ui_system.h"
#include <cstring>
#include <thread>
#include <chrono>

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
    //   Line 2: "  [Tab] toggle input mode"     = 25 chars
    //   Line 3: "  [+] [-] adjust depth bias"  = 27 chars
    //   Line 4: "  [[] []] quad width"         = 20 chars
    //   Line 5: "  [O] [P] quad height"        = 21 chars
    //   Line 6: "  [RClick] mouse look"         = 21 chars
    //   Line 7: "Frame: 0.0 ms"                = 13 chars
    //   Line 8: "GPU Mem: 0.0 MB"              = 15 chars
    //   Line 9: "MSAA: 4x"                     =  8 chars
    //   Line 10: "  [F] pause/resume"           = 18 chars
    //   Total                                   = 208 chars  -> 1248 vertices
    Metrics metrics;
    std::vector<UIVertex> verts;
    uint32_t count = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts);

    constexpr int expectedChars = 12 + 28 + 25 + 27 + 20 + 21 + 21 + 13 + 15 + 8 + 18;
    constexpr uint32_t expectedVerts = 6u * static_cast<uint32_t>(expectedChars);

    EXPECT_EQ(count, expectedVerts)
        << "tessellateHUD returned wrong vertex count for HUD lines";
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
    //   Line 2: "  [Tab] toggle input mode"     = 25 chars
    //   Line 3: "  [+] [-] adjust depth bias"  = 27 chars
    //   Line 4: "  [[] []] quad width"         = 20 chars
    //   Line 5: "  [O] [P] quad height"        = 21 chars
    //   Line 6: "  [RClick] mouse look"         = 21 chars
    //   Line 7: "Frame: 0.0 ms"                = 13 chars
    //   Line 8: "GPU Mem: 0.0 MB"              = 15 chars
    //   Line 9: "MSAA: 4x"                     =  8 chars
    //   Line 10: "  [F] pause/resume"           = 18 chars
    //   Total                                   = 213 chars  -> 1278 vertices
    Metrics metrics;
    std::vector<UIVertex> verts;
    uint32_t count = metrics.tessellateHUD(sys, RenderMode::Traditional, 4u, verts);

    constexpr int expectedChars = 17 + 28 + 25 + 27 + 20 + 21 + 21 + 13 + 15 + 8 + 18;
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
    //   11-line base (same as HUDTessellation_VertexCountMatchesLineCount):
    //     Mode: DIRECT              = 12 chars
    //     [Space] toggle...         = 28 chars
    //     [Tab] toggle...           = 25 chars
    //     [+] [-] adjust...         = 27 chars
    //     [[] []] quad width        = 20 chars
    //     [O] [P] quad height       = 21 chars
    //     [RClick] mouse look       = 21 chars
    //     Frame: 0.0 ms             = 13 chars
    //     GPU Mem: 0.0 MB           = 15 chars
    //     MSAA: 4x                  =  8 chars
    //     [F] pause/resume          = 18 chars
    //   inputModeStr: "Input: CAMERA" = 13 chars (inserted before Frame:)
    //   Total                              = 221 chars  -> 1326 vertices
    const char* inputStr = "Input: CAMERA";
    Metrics metrics;
    std::vector<UIVertex> verts;
    uint32_t count = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts, inputStr);

    constexpr int baseLine10Chars = 12 + 28 + 25 + 27 + 20 + 21 + 21 + 13 + 15 + 8 + 18;
    const int extraChars = static_cast<int>(std::strlen(inputStr));
    const uint32_t expectedVerts = 6u * static_cast<uint32_t>(baseLine10Chars + extraChars);

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
    // produces 1248 vertices (same as HUDTessellation_VertexCountMatchesLineCount).
    uint32_t appended = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts);

    constexpr uint32_t expectedAppended = 6u * (12u + 28u + 25u + 27u + 20u + 21u + 21u + 13u + 15u + 8u + 18u); // 1248
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
    //   Line 2: "  [Tab] toggle input mode"     = 25 chars  -> 150 vertices (offset 240)
    //   Line 3: "  [+] [-] adjust depth bias"  = 27 chars  -> 162 vertices (offset 390)
    //   Line 4: "  [[] []] quad width"         = 20 chars  -> 120 vertices (offset 552)
    //   Line 5: "  [O] [P] quad height"        = 21 chars  -> 126 vertices (offset 672)
    //   Line 6: "  [RClick] mouse look"         = 21 chars  -> 126 vertices (offset 798)
    //   Line 7: "Frame: 0.0 ms"                = 13 chars  ->  78 vertices (offset 924)
    //   Line 8: "GPU Mem: 0.0 MB"              = 15 chars  ->  90 vertices (offset 1002)
    //   Line 9: "MSAA: 4x"                     =  8 chars  ->  48 vertices (offset 1092)
    constexpr size_t line0Start = 0;
    constexpr size_t line1Start = 72;        // 12 * 6
    constexpr size_t line2Start = 240;       // (12 + 28) * 6
    constexpr size_t line3Start = 390;       // (12 + 28 + 25) * 6
    constexpr size_t line4Start = 552;       // (12 + 28 + 25 + 27) * 6
    constexpr size_t line5Start = 672;       // (12 + 28 + 25 + 27 + 20) * 6
    constexpr size_t line6Start = 798;       // (12 + 28 + 25 + 27 + 20 + 21) * 6
    constexpr size_t line7Start = 924;       // (12 + 28 + 25 + 27 + 20 + 21 + 21) * 6
    constexpr size_t line8Start = 1002;      // (12 + 28 + 25 + 27 + 20 + 21 + 21 + 13) * 6
    constexpr size_t line9Start = 1092;      // (12 + 28 + 25 + 27 + 20 + 21 + 21 + 13 + 15) * 6

    ASSERT_GE(verts.size(), line9Start + 6u)
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
    const float expectedY8 = leftMargin + 8 * lineHeight;   // 328.0f
    const float expectedY9 = leftMargin + 9 * lineHeight;   // 368.0f

    EXPECT_NEAR(verts[line0Start].pos.y, expectedY0, 1e-5f) << "Line 0 TL y != " << expectedY0;
    EXPECT_NEAR(verts[line1Start].pos.y, expectedY1, 1e-5f) << "Line 1 TL y != " << expectedY1;
    EXPECT_NEAR(verts[line2Start].pos.y, expectedY2, 1e-5f) << "Line 2 TL y != " << expectedY2;
    EXPECT_NEAR(verts[line3Start].pos.y, expectedY3, 1e-5f) << "Line 3 TL y != " << expectedY3;
    EXPECT_NEAR(verts[line4Start].pos.y, expectedY4, 1e-5f) << "Line 4 TL y != " << expectedY4;
    EXPECT_NEAR(verts[line5Start].pos.y, expectedY5, 1e-5f) << "Line 5 TL y != " << expectedY5;
    EXPECT_NEAR(verts[line6Start].pos.y, expectedY6, 1e-5f) << "Line 6 TL y != " << expectedY6;
    EXPECT_NEAR(verts[line7Start].pos.y, expectedY7, 1e-5f) << "Line 7 TL y != " << expectedY7;
    EXPECT_NEAR(verts[line8Start].pos.y, expectedY8, 1e-5f) << "Line 8 TL y != " << expectedY8;
    EXPECT_NEAR(verts[line9Start].pos.y, expectedY9, 1e-5f) << "Line 9 TL y != " << expectedY9;

    // Cross-check: successive line y-values must differ by exactly lineHeight.
    float diff01 = verts[line1Start].pos.y - verts[line0Start].pos.y;
    float diff12 = verts[line2Start].pos.y - verts[line1Start].pos.y;
    float diff23 = verts[line3Start].pos.y - verts[line2Start].pos.y;
    float diff34 = verts[line4Start].pos.y - verts[line3Start].pos.y;
    float diff45 = verts[line5Start].pos.y - verts[line4Start].pos.y;
    float diff56 = verts[line6Start].pos.y - verts[line5Start].pos.y;
    float diff67 = verts[line7Start].pos.y - verts[line6Start].pos.y;
    float diff78 = verts[line8Start].pos.y - verts[line7Start].pos.y;
    float diff89 = verts[line9Start].pos.y - verts[line8Start].pos.y;

    EXPECT_NEAR(diff01, lineHeight, 1e-5f) << "Line 0->1 y gap != lineHeight";
    EXPECT_NEAR(diff12, lineHeight, 1e-5f) << "Line 1->2 y gap != lineHeight";
    EXPECT_NEAR(diff23, lineHeight, 1e-5f) << "Line 2->3 y gap != lineHeight";
    EXPECT_NEAR(diff34, lineHeight, 1e-5f) << "Line 3->4 y gap != lineHeight";
    EXPECT_NEAR(diff45, lineHeight, 1e-5f) << "Line 4->5 y gap != lineHeight";
    EXPECT_NEAR(diff56, lineHeight, 1e-5f) << "Line 5->6 y gap != lineHeight";
    EXPECT_NEAR(diff67, lineHeight, 1e-5f) << "Line 6->7 y gap != lineHeight";
    EXPECT_NEAR(diff78, lineHeight, 1e-5f) << "Line 7->8 y gap != lineHeight";
    EXPECT_NEAR(diff89, lineHeight, 1e-5f) << "Line 8->9 y gap != lineHeight";
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

   // inputModeStr starts after lines 0-6: (12+28+25+27+20+21+21)*6 = 924 vertices.
    constexpr size_t line7Start = (12u + 28u + 25u + 27u + 20u + 21u + 21u) * 6u; // 924

    ASSERT_GE(verts.size(), line7Start + 6u)
        << "tessellateHUD produced too few vertices to check inputModeStr line start";

    constexpr float leftMargin = 8.0f;
    constexpr float lineHeight = 40.0f;
    const float expectedY7 = leftMargin + 7.0f * lineHeight; // 288.0f

    EXPECT_NEAR(verts[line7Start].pos.y, expectedY7, 1e-5f)
        << "inputModeStr line TL y != " << expectedY7;
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
    constexpr size_t line1Start = 12u * 6u;                              // 72
    constexpr size_t line2Start = (12u + 28u) * 6u;                     // 240
    constexpr size_t line3Start = (12u + 28u + 25u) * 6u;               // 390
    constexpr size_t line4Start = (12u + 28u + 25u + 27u) * 6u;         // 564
    constexpr size_t line5Start = (12u + 28u + 25u + 27u + 20u) * 6u;   // 684
    constexpr size_t line6Start = (12u + 28u + 25u + 27u + 20u + 21u) * 6u;  // 798

    ASSERT_GE(verts.size(), line6Start + 6u)
        << "tessellateHUD produced too few vertices to check all lines";

    constexpr float leftMargin = 8.0f;
    constexpr float lineHeight = 40.0f;

    const size_t lineStarts[7] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start };
    for (int i = 0; i < 7; ++i) {
        const float expectedY = leftMargin + static_cast<float>(i) * lineHeight;
        EXPECT_NEAR(verts[lineStarts[i]].pos.y, expectedY, 1e-5f)
            << "Line " << i << " TL y != " << expectedY;
    }

    for (int i = 0; i < 6; ++i) {
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
    constexpr size_t line1Start = 12u * 6u;                              // 72
    constexpr size_t line2Start = (12u + 28u) * 6u;                     // 240
    constexpr size_t line3Start = (12u + 28u + 25u) * 6u;               // 390
    constexpr size_t line4Start = (12u + 28u + 25u + 27u) * 6u;         // 564
    constexpr size_t line5Start = (12u + 28u + 25u + 27u + 20u) * 6u;   // 684
    constexpr size_t line6Start = (12u + 28u + 25u + 27u + 20u + 21u) * 6u;  // 798

    ASSERT_GE(verts.size(), line6Start + 6u)
        << "tessellateHUD produced too few vertices to check all lines";

    constexpr float leftMargin = 8.0f;

    const size_t lineStarts[7] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start };
    for (int i = 0; i < 7; ++i) {
        EXPECT_NEAR(verts[lineStarts[i]].pos.x, leftMargin, 1e-5f)
            << "Line " << i << " TL x != " << leftMargin;
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — all nine lines follow the arithmetic y-sequence when no
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
    constexpr size_t line1Start = 12u * 6u;                                        // 72
    constexpr size_t line2Start = (12u + 28u) * 6u;                               // 240
    constexpr size_t line3Start = (12u + 28u + 25u) * 6u;                         // 390
    constexpr size_t line4Start = (12u + 28u + 25u + 27u) * 6u;                   // 552
    constexpr size_t line5Start = (12u + 28u + 25u + 27u + 20u) * 6u;             // 672
    constexpr size_t line6Start = (12u + 28u + 25u + 27u + 20u + 21u) * 6u;       // 798
    constexpr size_t line7Start = (12u + 28u + 25u + 27u + 20u + 21u + 21u) * 6u; // 924
    constexpr size_t line8Start = (12u + 28u + 25u + 27u + 20u + 21u + 21u + 13u) * 6u; // 1002
    constexpr size_t line9Start = (12u + 28u + 25u + 27u + 20u + 21u + 21u + 13u + 15u) * 6u; // 1092

    ASSERT_GE(verts.size(), line9Start + 6u)
        << "tessellateHUD produced too few vertices to check all ten lines";

    constexpr float leftMargin = 8.0f;
    constexpr float lineHeight = 40.0f;

    const size_t lineStarts[10] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start, line7Start, line8Start, line9Start };
    for (int i = 0; i < 10; ++i) {
        const float expectedY = leftMargin + static_cast<float>(i) * lineHeight;
        EXPECT_NEAR(verts[lineStarts[i]].pos.y, expectedY, 1e-5f)
            << "Line " << i << " TL y != " << expectedY;
    }

    for (int i = 0; i < 9; ++i) {
        const float diff = verts[lineStarts[i + 1]].pos.y - verts[lineStarts[i]].pos.y;
        EXPECT_NEAR(diff, lineHeight, 1e-5f)
            << "Line " << i << " -> " << (i + 1) << " y gap != lineHeight";
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — all nine lines share leftMargin x-position when no
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
    constexpr size_t line1Start = 12u * 6u;                                        // 72
    constexpr size_t line2Start = (12u + 28u) * 6u;                               // 240
    constexpr size_t line3Start = (12u + 28u + 25u) * 6u;                         // 390
    constexpr size_t line4Start = (12u + 28u + 25u + 27u) * 6u;                   // 552
    constexpr size_t line5Start = (12u + 28u + 25u + 27u + 20u) * 6u;             // 672
    constexpr size_t line6Start = (12u + 28u + 25u + 27u + 20u + 21u) * 6u;       // 798
    constexpr size_t line7Start = (12u + 28u + 25u + 27u + 20u + 21u + 21u) * 6u; // 924
    constexpr size_t line8Start = (12u + 28u + 25u + 27u + 20u + 21u + 21u + 13u) * 6u; // 1002
    constexpr size_t line9Start = (12u + 28u + 25u + 27u + 20u + 21u + 21u + 13u + 15u) * 6u; // 1092

    ASSERT_GE(verts.size(), line9Start + 6u)
        << "tessellateHUD produced too few vertices to check all ten lines";

    constexpr float leftMargin = 8.0f;

    const size_t lineStarts[10] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start, line7Start, line8Start, line9Start };
    for (int i = 0; i < 10; ++i) {
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
    constexpr size_t line1Start = 17u * 6u;                                         // 102
    constexpr size_t line2Start = (17u + 28u) * 6u;                                // 270
    constexpr size_t line3Start = (17u + 28u + 25u) * 6u;                          // 420
    constexpr size_t line4Start = (17u + 28u + 25u + 27u) * 6u;                    // 582
    constexpr size_t line5Start = (17u + 28u + 25u + 27u + 20u) * 6u;              // 702
    constexpr size_t line6Start = (17u + 28u + 25u + 27u + 20u + 21u) * 6u;        // 828
    constexpr size_t line7Start = (17u + 28u + 25u + 27u + 20u + 21u + 21u) * 6u;  // 954
    constexpr size_t line8Start = (17u + 28u + 25u + 27u + 20u + 21u + 21u + 13u) * 6u; // 1032
    constexpr size_t line9Start = (17u + 28u + 25u + 27u + 20u + 21u + 21u + 13u + 15u) * 6u; // 1122

    ASSERT_GE(verts.size(), line9Start + 6u)
        << "tessellateHUD (Traditional) produced too few vertices to check all ten lines";

    constexpr float leftMargin = 8.0f;
    constexpr float lineHeight = 40.0f;

    const size_t lineStarts[10] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start, line7Start, line8Start, line9Start };
    for (int i = 0; i < 10; ++i) {
        const float expectedY = leftMargin + static_cast<float>(i) * lineHeight;
        EXPECT_NEAR(verts[lineStarts[i]].pos.y, expectedY, 1e-5f)
            << "Traditional mode Line " << i << " TL y != " << expectedY;
    }

    for (int i = 0; i < 9; ++i) {
        const float diff = verts[lineStarts[i + 1]].pos.y - verts[lineStarts[i]].pos.y;
        EXPECT_NEAR(diff, lineHeight, 1e-5f)
            << "Traditional mode Line " << i << " -> " << (i + 1) << " y gap != lineHeight";
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — Traditional mode: all nine lines start at leftMargin x
// ---------------------------------------------------------------------------

TEST(MetricsTest, HUDTessellation_TraditionalMode_AllLinesXPositions)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;
    metrics.tessellateHUD(sys, RenderMode::Traditional, 4u, verts, nullptr);

    constexpr size_t line0Start = 0u;
    constexpr size_t line1Start = 17u * 6u;                                         // 102
    constexpr size_t line2Start = (17u + 28u) * 6u;                                // 270
    constexpr size_t line3Start = (17u + 28u + 25u) * 6u;                          // 420
    constexpr size_t line4Start = (17u + 28u + 25u + 27u) * 6u;                    // 582
    constexpr size_t line5Start = (17u + 28u + 25u + 27u + 20u) * 6u;              // 702
    constexpr size_t line6Start = (17u + 28u + 25u + 27u + 20u + 21u) * 6u;        // 828
    constexpr size_t line7Start = (17u + 28u + 25u + 27u + 20u + 21u + 21u) * 6u;  // 954
    constexpr size_t line8Start = (17u + 28u + 25u + 27u + 20u + 21u + 21u + 13u) * 6u; // 1032
    constexpr size_t line9Start = (17u + 28u + 25u + 27u + 20u + 21u + 21u + 13u + 15u) * 6u; // 1122

    ASSERT_GE(verts.size(), line9Start + 6u)
        << "tessellateHUD (Traditional) produced too few vertices to check all ten lines";

    constexpr float leftMargin = 8.0f;

    const size_t lineStarts[10] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start, line7Start, line8Start, line9Start };
    for (int i = 0; i < 10; ++i) {
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
    constexpr size_t line1Start = 17u * 6u;                                        // 102
    constexpr size_t line2Start = (17u + 28u) * 6u;                               // 270
    constexpr size_t line3Start = (17u + 28u + 25u) * 6u;                         // 420
    constexpr size_t line4Start = (17u + 28u + 25u + 27u) * 6u;                   // 582
    constexpr size_t line5Start = (17u + 28u + 25u + 27u + 20u) * 6u;             // 702
    constexpr size_t line6Start = (17u + 28u + 25u + 27u + 20u + 21u) * 6u;       // 828

    ASSERT_GE(verts.size(), line6Start + 6u)
        << "tessellateHUD (Traditional+inputModeStr) produced too few vertices to check all lines";

    constexpr float leftMargin = 8.0f;

    const size_t lineStarts[7] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start };
    for (int i = 0; i < 7; ++i) {
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
    constexpr size_t line1Start = 17u * 6u;                                        // 102
    constexpr size_t line2Start = (17u + 28u) * 6u;                               // 270
    constexpr size_t line3Start = (17u + 28u + 27u) * 6u;                         // 432
    constexpr size_t line4Start = (17u + 28u + 25u + 27u) * 6u;                   // 594
    constexpr size_t line5Start = (17u + 28u + 25u + 27u + 20u) * 6u;             // 714
    constexpr size_t line6Start = (17u + 28u + 27u + 27u + 20u + 21u) * 6u;       // 840

    ASSERT_GE(verts.size(), line6Start + 6u)
        << "tessellateHUD (Traditional+inputModeStr) produced too few vertices to check all lines";

    constexpr float leftMargin = 8.0f;
    constexpr float lineHeight = 40.0f;

    const size_t lineStarts[7] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start };
    for (int i = 0; i < 7; ++i) {
        const float expectedY = leftMargin + static_cast<float>(i) * lineHeight;
        EXPECT_NEAR(verts[lineStarts[i]].pos.y, expectedY, 1e-5f)
            << "Traditional mode Line " << i << " TL y != " << expectedY;
    }

    for (int i = 0; i < 6; ++i) {
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
    constexpr size_t line1Start = 12u * 6u;                                        // 72
    constexpr size_t line2Start = (12u + 28u) * 6u;                               // 240
    constexpr size_t line3Start = (12u + 28u + 25u) * 6u;                         // 390
    constexpr size_t line4Start = (12u + 28u + 25u + 27u) * 6u;                   // 552
    constexpr size_t line5Start = (12u + 28u + 25u + 27u + 20u) * 6u;             // 672
    constexpr size_t line6Start = (12u + 28u + 25u + 27u + 20u + 21u) * 6u;       // 798
    constexpr size_t line7Start = (12u + 28u + 25u + 27u + 20u + 21u + 21u) * 6u; // 924
    constexpr size_t line8Start = (12u + 28u + 25u + 27u + 20u + 21u + 21u + 13u) * 6u; // 1002
    constexpr size_t line9Start = (12u + 28u + 25u + 27u + 20u + 21u + 21u + 13u + 15u) * 6u; // 1092

    ASSERT_GE(verts.size(), line9Start + 6u)
        << "tessellateHUD (empty inputModeStr) produced too few vertices to check all ten base lines";

    constexpr float leftMargin = 8.0f;

    const size_t lineStarts[10] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start, line7Start, line8Start, line9Start };
    for (int i = 0; i < 10; ++i) {
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

    // The inputModeStr contributes 0 vertices; total must match the 11-line Traditional base.
    constexpr uint32_t expectedVerts = (17u + 28u + 25u + 27u + 20u + 21u + 21u + 13u + 15u + 8u + 18u) * 6u;  // 1278
    EXPECT_EQ(count, expectedVerts)
        << "tessellateHUD (Traditional, empty inputModeStr) returned " << count
        << " vertices; expected " << expectedVerts;
    EXPECT_EQ(static_cast<uint32_t>(verts.size()), expectedVerts)
        << "outVerts.size() does not match the returned count";

    constexpr size_t line0Start = 0u;
    constexpr size_t line1Start = 17u * 6u;                                         // 102
    constexpr size_t line2Start = (17u + 28u) * 6u;                                // 270
    constexpr size_t line3Start = (17u + 28u + 25u) * 6u;                          // 420
    constexpr size_t line4Start = (17u + 28u + 25u + 27u) * 6u;                    // 582
    constexpr size_t line5Start = (17u + 28u + 25u + 27u + 20u) * 6u;              // 702
    constexpr size_t line6Start = (17u + 28u + 25u + 27u + 20u + 21u) * 6u;        // 828
    constexpr size_t line7Start = (17u + 28u + 25u + 27u + 20u + 21u + 21u) * 6u;  // 954
    constexpr size_t line8Start = (17u + 28u + 25u + 27u + 20u + 21u + 21u + 13u) * 6u; // 1032
    constexpr size_t line9Start = (17u + 28u + 25u + 27u + 20u + 21u + 21u + 13u + 15u) * 6u; // 1122

    ASSERT_GE(verts.size(), line9Start + 6u)
        << "tessellateHUD (Traditional, empty inputModeStr) produced too few vertices";

    constexpr float leftMargin = 8.0f;
    constexpr float lineHeight = 40.0f;

    const size_t lineStarts[10] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start, line7Start, line8Start, line9Start };
    for (int i = 0; i < 10; ++i) {
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
    constexpr size_t line1Start = 17u * 6u;                                         // 102
    constexpr size_t line2Start = (17u + 28u) * 6u;                                // 270
    constexpr size_t line3Start = (17u + 28u + 25u) * 6u;                          // 420
    constexpr size_t line4Start = (17u + 28u + 25u + 27u) * 6u;                    // 582
    constexpr size_t line5Start = (17u + 28u + 25u + 27u + 20u) * 6u;              // 702
    constexpr size_t line6Start = (17u + 28u + 25u + 27u + 20u + 21u) * 6u;        // 828
    constexpr size_t line7Start = (17u + 28u + 25u + 27u + 20u + 21u + 21u) * 6u;  // 954
    constexpr size_t line8Start = (17u + 28u + 25u + 27u + 20u + 21u + 21u + 13u) * 6u; // 1032
    constexpr size_t line9Start = (17u + 28u + 25u + 27u + 20u + 21u + 21u + 13u + 15u) * 6u; // 1122

    ASSERT_GE(verts.size(), line9Start + 6u)
        << "tessellateHUD (Traditional, empty inputModeStr) produced too few vertices";

    constexpr float leftMargin = 8.0f;

    const size_t lineStarts[10] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start, line7Start, line8Start, line9Start };
    for (int i = 0; i < 10; ++i) {
        EXPECT_NEAR(verts[lineStarts[i]].pos.x, leftMargin, 1e-5f)
            << "Traditional mode Line " << i << " TL x != " << leftMargin;
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — empty (non-null) inputModeStr matches the nullptr baseline
// ---------------------------------------------------------------------------

// The guard `inputModeStr && inputModeStr[0] != '\0'` must skip the extra line
// when inputModeStr is a non-null pointer to an empty string.  Verify by
// comparing the vertex count directly against the nullptr baseline call rather
// than against a hardcoded constant, so the test is self-contained regardless
// of the actual line content.
TEST(MetricsTest, HUDTessellation_EmptyInputModeStr_MatchesNullptrBaseline)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> vertsNull, vertsEmpty;

    uint32_t countNull  = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, vertsNull,  nullptr);
    uint32_t countEmpty = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, vertsEmpty, "");

    ASSERT_GT(countNull, 0u)
        << "nullptr baseline produced 0 vertices — baseline call failed";
    EXPECT_EQ(countEmpty, countNull)
        << "inputModeStr=\"\" produced " << countEmpty
        << " vertices but nullptr baseline produced " << countNull
        << "; the inputModeStr[0] != '\\0' guard must skip the extra line for an empty string";
    EXPECT_EQ(vertsEmpty.size(), vertsNull.size())
        << "outVerts.size() mismatch between empty-string and nullptr calls";
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

    constexpr uint32_t expectedVerts = (12u + 28u + 25u + 27u + 20u + 21u + 21u + 13u + 15u + 10u + 18u) * 6u;  // 1260

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

    // The inputModeStr contributes 0 vertices; total must match the 11-line Direct base.
    constexpr uint32_t expectedVerts = (12u + 28u + 25u + 27u + 20u + 21u + 21u + 13u + 15u + 8u + 18u) * 6u;  // 1248
    EXPECT_EQ(count, expectedVerts)
        << "tessellateHUD (Direct, empty inputModeStr) returned " << count
        << " vertices; expected " << expectedVerts;
    EXPECT_EQ(static_cast<uint32_t>(verts.size()), expectedVerts)
        << "outVerts.size() does not match the returned count";

    constexpr size_t line0Start = 0u;
    constexpr size_t line1Start = 12u * 6u;                                        // 72
    constexpr size_t line2Start = (12u + 28u) * 6u;                               // 240
    constexpr size_t line3Start = (12u + 28u + 25u) * 6u;                         // 390
    constexpr size_t line4Start = (12u + 28u + 25u + 27u) * 6u;                   // 552
    constexpr size_t line5Start = (12u + 28u + 25u + 27u + 20u) * 6u;             // 672
    constexpr size_t line6Start = (12u + 28u + 25u + 27u + 20u + 21u) * 6u;       // 798
    constexpr size_t line7Start = (12u + 28u + 25u + 27u + 20u + 21u + 21u) * 6u; // 924
    constexpr size_t line8Start = (12u + 28u + 25u + 27u + 20u + 21u + 21u + 13u) * 6u; // 1002
    constexpr size_t line9Start = (12u + 28u + 25u + 27u + 20u + 21u + 21u + 13u + 15u) * 6u; // 1092

    ASSERT_GE(verts.size(), line9Start + 6u)
        << "tessellateHUD (Direct, empty inputModeStr) produced too few vertices";

    constexpr float leftMargin = 8.0f;
    constexpr float lineHeight = 40.0f;

    const size_t lineStarts[10] = { line0Start, line1Start, line2Start, line3Start, line4Start, line5Start, line6Start, line7Start, line8Start, line9Start };
    for (int i = 0; i < 10; ++i) {
        const float expectedY = leftMargin + static_cast<float>(i) * lineHeight;
        EXPECT_NEAR(verts[lineStarts[i]].pos.y, expectedY, 1e-5f)
            << "Direct mode Line " << i << " TL y != " << expectedY;
    }
}

// ---------------------------------------------------------------------------
// MetricsTest — tessellateHUD frame time line reflects a non-zero average
// ---------------------------------------------------------------------------
//
// Guards against a regression where tessellateHUD always formats "Frame: 0.0 ms"
// (or any hardcoded zero) even after real frames have been recorded.
//
// After a few beginFrame/endFrame cycles that include a 1 ms sleep,
// averageFrameMs() must return a strictly positive value.  tessellateHUD must
// then produce a total vertex count consistent with the formatted string
// derived from that non-zero average — not from a hardcoded 0.0.
//
// Note: for averages in [0.1, 9.9] ms the formatted string "Frame: X.X ms"
// has the same character length (13) as "Frame: 0.0 ms".  In that range the
// vertex count check is vacuous but the ASSERT_GT below is the primary guard.
// On slower hosts where the OS sleep quantisation yields >= 10 ms the vertex
// count becomes a strict distinguisher (14 chars vs 13).
TEST(MetricsTest, HUDTessellation_FrameTimeLine_ReflectsNonZeroAverage)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;

    // Record 3 frames, each with a 1 ms sleep, to produce a non-zero average.
    for (int i = 0; i < 3; ++i) {
        metrics.beginFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        metrics.endFrame();
    }

    float avg = metrics.averageFrameMs();
    ASSERT_GT(avg, 0.0f)
        << "averageFrameMs() must be > 0 after 3 frames with a 1 ms sleep each; "
           "beginFrame/endFrame may not be recording elapsed time";

    // Reproduce the snprintf format used by tessellateHUD to determine the
    // expected character count for the frame time line.
    char frameLine[64];
    snprintf(frameLine, sizeof(frameLine), "Frame: %.1f ms", avg);
    const int frameLineChars = static_cast<int>(std::strlen(frameLine));

    // Fixed char counts for all other lines (Direct mode, no inputModeStr,
    // gpuAllocatedBytes()==0):
    //   Line 0: "Mode: DIRECT"               = 12 chars
    //   Line 1: "  [Space] toggle render mode" = 28 chars
    //   Line 2: "  [Tab] toggle input mode"    = 25 chars
    //   Line 3: "  [+] [-] adjust depth bias"  = 27 chars
    //   Line 4: "  [[] []] quad width"         = 20 chars
    //   Line 5: "  [O] [P] quad height"        = 21 chars
    //   Line 6: "  [RClick] mouse look"         = 21 chars
    //   Line 7: frame time line               (variable — frameLineChars)
    //   Line 8: "GPU Mem: 0.0 MB"             = 15 chars
    //   Line 9: "MSAA: 4x"                    =  8 chars
    //   Line 10: "  [F] pause/resume"          = 18 chars
    constexpr int fixedChars = 12 + 28 + 25 + 27 + 20 + 21 + 21 + 15 + 8 + 18; // = 195
    const uint32_t expectedVerts =
        6u * static_cast<uint32_t>(fixedChars + frameLineChars);

    std::vector<UIVertex> verts;
    uint32_t count = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts);

    EXPECT_EQ(count, expectedVerts)
        << "tessellateHUD vertex count does not match the expected count "
           "computed from averageFrameMs()=" << avg << " ms "
           "(frameLineChars=" << frameLineChars << "); "
           "tessellateHUD may be hardcoding 0.0 for the frame time";
    EXPECT_EQ(static_cast<uint32_t>(verts.size()), expectedVerts)
        << "outVerts.size() does not match the returned vertex count";
}

// ---------------------------------------------------------------------------
// MetricsTest — pause button line is always rendered (fails until implemented)
// ---------------------------------------------------------------------------

// tessellateHUD must include an "  [F] pause/resume" line (18 chars) after MSAA.
// Without the pause button implementation, vertex count is 190*6=1140 (Direct,
// no inputModeStr). With the [F] line it must be (190+18)*6=1248.
TEST(MetricsTest, HUDTessellation_PauseHintLine_AlwaysPresent)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> verts;
    uint32_t count = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, verts);

    // "  [F] pause/resume" = 18 chars
    constexpr int pauseHintChars = 18;
    constexpr int baseChars = 12 + 28 + 25 + 27 + 20 + 21 + 21 + 13 + 15 + 8; // 190
    constexpr uint32_t expectedVerts = 6u * static_cast<uint32_t>(baseChars + pauseHintChars);

    EXPECT_EQ(count, expectedVerts)
        << "tessellateHUD must include [F] pause/resume hint line (18 chars); "
           "got " << count << " verts, expected " << expectedVerts;
}

// ---------------------------------------------------------------------------
// MetricsTest — paused=true adds a status line (fails until implemented)
// ---------------------------------------------------------------------------

// When paused=true, tessellateHUD must append "Status: PAUSED" (14 chars)
// after the [F] pause hint line. Without implementation, the paused parameter
// has no effect and the count is the same as the non-paused case.
TEST(MetricsTest, HUDTessellation_PausedState_ShowsStatusLine)
{
    UISystem sys;
    sys.buildGlyphTable();

    Metrics metrics;
    std::vector<UIVertex> vertsUnpaused, vertsPaused;
    uint32_t countUnpaused = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, vertsUnpaused,
                                                    nullptr, /*paused=*/false);
    uint32_t countPaused   = metrics.tessellateHUD(sys, RenderMode::Direct, 4u, vertsPaused,
                                                    nullptr, /*paused=*/true);

    // "Status: PAUSED" = 14 chars = 84 extra vertices
    constexpr uint32_t expectedExtra = 6u * 14u;
    EXPECT_EQ(countPaused, countUnpaused + expectedExtra)
        << "paused=true must add 'Status: PAUSED' (14 chars = 84 verts); "
           "unpaused=" << countUnpaused << " paused=" << countPaused;
}
