#include <gtest/gtest.h>
#include "ui_system.h"
#include "ui_surface.h"
#include "scene.h"

#include <glm/glm.hpp>
#include <cmath>
#include <string>

// ---------------------------------------------------------------------------
// SDF constant and UISystem accessor tests
// Note: SDF_GLYPH_PADDING, SDF_PIXEL_DIST_SCALE, and SDF_THRESHOLD_DEFAULT
// constant-property invariants are enforced by static_assert in ui_system.h.
// ---------------------------------------------------------------------------

TEST(SDFConstants, SdfThresholdReturnsZeroWhenNotSDF)
{
    // Default-constructed UISystem has isSDF()==false; sdfThreshold() must return 0.
    UISystem sys;
    EXPECT_FALSE(sys.isSDF());
    EXPECT_FLOAT_EQ(sys.sdfThreshold(), 0.0f);
}

// ---------------------------------------------------------------------------
// UISystem — uvForChar covers all printable ASCII (no Vulkan required)
// ---------------------------------------------------------------------------

TEST(UISystemUVTable, PrintableASCII_UVsInUnitSquare)
{
    // buildGlyphTable() is pure CPU math (no Vulkan).  For every printable
    // ASCII codepoint (32–126) the returned UV rect must:
    //   • lie fully within [0, 1] × [0, 1]
    //   • have strictly positive width  (u1 > u0)
    //   • have strictly positive height (v1 > v0)
    //
    // An out-of-range UV silently samples outside the atlas, producing garbled
    // or invisible glyphs with no compile-time or runtime error.
    UISystem sys;
    sys.buildGlyphTable();

    for (int cp = 32; cp <= 126; ++cp) {
        SCOPED_TRACE("codepoint=" + std::to_string(cp) +
                     " char='" + static_cast<char>(cp) + "'");
        GlyphRect r = sys.uvForChar(static_cast<char>(cp));

        EXPECT_GE(r.u0, 0.0f) << "u0 < 0";
        EXPECT_LE(r.u0, 1.0f) << "u0 > 1";
        EXPECT_GE(r.v0, 0.0f) << "v0 < 0";
        EXPECT_LE(r.v0, 1.0f) << "v0 > 1";
        EXPECT_GE(r.u1, 0.0f) << "u1 < 0";
        EXPECT_LE(r.u1, 1.0f) << "u1 > 1";
        EXPECT_GE(r.v1, 0.0f) << "v1 < 0";
        EXPECT_LE(r.v1, 1.0f) << "v1 > 1";

        EXPECT_GT(r.u1, r.u0) << "zero or negative UV width (u1 <= u0)";
        EXPECT_GT(r.v1, r.v0) << "zero or negative UV height (v1 <= v0)";
    }
}

TEST(UISystemUVTable, FirstAndLastPrintable_CorrectCells)
{
    // Spot-check the first (space, index 0) and last ('~', index 94) glyphs
    // against the analytically expected cell boundaries.
    //   Atlas: 512×512, cell: 32×32  → cellSize/atlasSize = 1/16 = 0.0625
    //   index 0  → col=0, row=0  → u0=0,      v0=0,      u1=0.0625, v1=0.0625
    //   index 94 → col=14, row=5 → u0=14/16,  v0=5/16,   u1=15/16,  v1=6/16
    UISystem sys;
    sys.buildGlyphTable();

    constexpr float cell = static_cast<float>(GLYPH_CELL) / static_cast<float>(ATLAS_SIZE);

    // Space (ASCII 32, index 0)
    GlyphRect sp = sys.uvForChar(' ');
    EXPECT_NEAR(sp.u0, 0.0f,  1e-6f);
    EXPECT_NEAR(sp.v0, 0.0f,  1e-6f);
    EXPECT_NEAR(sp.u1, cell,  1e-6f);
    EXPECT_NEAR(sp.v1, cell,  1e-6f);

    // Tilde (ASCII 126, index 94)
    GlyphRect tilde = sys.uvForChar('~');
    EXPECT_NEAR(tilde.u0, 14.0f * cell, 1e-6f);
    EXPECT_NEAR(tilde.v0,  5.0f * cell, 1e-6f);
    EXPECT_NEAR(tilde.u1, 15.0f * cell, 1e-6f);
    EXPECT_NEAR(tilde.v1,  6.0f * cell, 1e-6f);
}

TEST(UISystemUVTable, GlyphTableBuilt_StateTransition)
{
    // A default-constructed UISystem must report isGlyphTableBuilt()==false so
    // that guards in UISystem and tessellateHUD can detect accidental use before
    // the table is initialised.  After buildGlyphTable() the flag must flip to
    // true — if it never sets the flag, every isGlyphTableBuilt() guard would
    // permanently short-circuit and the affected code paths would be silently
    // skipped at runtime.
    UISystem sys;
    EXPECT_FALSE(sys.isGlyphTableBuilt()) << "expected false before buildGlyphTable()";

    sys.buildGlyphTable();
    EXPECT_TRUE(sys.isGlyphTableBuilt()) << "expected true immediately after buildGlyphTable()";
}

TEST(UISystemUVTable, OutOfRangeChar_ClampedToSpaceGlyph)
{
    // Characters outside [32, 126] must not sample outside the atlas.
    // The implementation clamps them to the space glyph (index 0).
    UISystem sys;
    sys.buildGlyphTable();

    GlyphRect space = sys.uvForChar(' ');

    // Control character (below 32)
    GlyphRect ctrl = sys.uvForChar('\t');
    EXPECT_NEAR(ctrl.u0, space.u0, 1e-6f);
    EXPECT_NEAR(ctrl.v0, space.v0, 1e-6f);

    // Extended ASCII (above 126)
    GlyphRect ext = sys.uvForChar('\x80');
    EXPECT_NEAR(ext.u0, space.u0, 1e-6f);
    EXPECT_NEAR(ext.v0, space.v0, 1e-6f);
}

TEST(UISystemUVTable, RebuildGlyphTable_ConsistentAfterMultipleCalls)
{
    // Calling buildGlyphTable() multiple times on the same UISystem must yield
    // identical UV table results. This guards against non-idempotent
    // initialization or state pollution where subsequent calls modify the
    // table differently than the first call.
    UISystem sys;

    // First call to buildGlyphTable()
    sys.buildGlyphTable();
    bool firstBuilt = sys.isGlyphTableBuilt();
    ASSERT_TRUE(firstBuilt) << "first buildGlyphTable() must set m_glyphTableBuilt to true";

    // Second call to buildGlyphTable()
    sys.buildGlyphTable();
    bool secondBuilt = sys.isGlyphTableBuilt();
    ASSERT_TRUE(secondBuilt) << "second buildGlyphTable() must set m_glyphTableBuilt to true";

    // All 95 glyph entries must be identical between the two calls
    // We verify by comparing uvForChar() results for all printable ASCII chars
    for (int cp = 32; cp <= 126; ++cp) {
        SCOPED_TRACE("codepoint=" + std::to_string(cp) + " char='" + static_cast<char>(cp) + "'");
        // Re-call buildGlyphTable() to simulate the scenario
        sys.buildGlyphTable();
        GlyphRect r = sys.uvForChar(static_cast<char>(cp));

        // Verify the UV rect is valid (in unit square, positive area)
        EXPECT_GE(r.u0, 0.0f) << "u0 < 0 for codepoint " << cp;
        EXPECT_LE(r.u0, 1.0f) << "u0 > 1 for codepoint " << cp;
        EXPECT_GE(r.v0, 0.0f) << "v0 < 0 for codepoint " << cp;
        EXPECT_LE(r.v0, 1.0f) << "v0 > 1 for codepoint " << cp;
        EXPECT_GE(r.u1, 0.0f) << "u1 < 0 for codepoint " << cp;
        EXPECT_LE(r.u1, 1.0f) << "u1 > 1 for codepoint " << cp;
        EXPECT_GE(r.v1, 0.0f) << "v1 < 0 for codepoint " << cp;
        EXPECT_LE(r.v1, 1.0f) << "v1 > 1 for codepoint " << cp;
        EXPECT_GT(r.u1, r.u0) << "zero or negative UV width for codepoint " << cp;
        EXPECT_GT(r.v1, r.v0) << "zero or negative UV height for codepoint " << cp;
    }

    // Third call to verify consistency continues
    sys.buildGlyphTable();
    for (int cp = 32; cp <= 126; ++cp) {
        GlyphRect r = sys.uvForChar(static_cast<char>(cp));
        EXPECT_GE(r.u0, 0.0f) << "u0 < 0 for codepoint " << cp << " on third call";
        EXPECT_GT(r.u1, r.u0) << "width must be positive for codepoint " << cp << " on third call";
    }
}

// ---------------------------------------------------------------------------
// UISystem::tessellateString — vertex count, positions, and UV correctness
// ---------------------------------------------------------------------------

class TessellateStringTest : public ::testing::Test {
protected:
    UISystem sys;

    void SetUp() override {
        sys.buildGlyphTable();
    }
};

TEST_F(TessellateStringTest, EmptyString_ZeroVertices)
{
    std::vector<UIVertex> verts;
    uint32_t count = sys.tessellateString("", 0.0f, 0.0f, verts);
    EXPECT_EQ(count, 0u);
    EXPECT_TRUE(verts.empty());
}

TEST_F(TessellateStringTest, NChars_Produces6NVertices)
{
    for (int n : {1, 3, 11}) {
        std::vector<UIVertex> verts;
        std::string text(n, 'A');
        uint32_t count = sys.tessellateString(text, 0.0f, 0.0f, verts);
        EXPECT_EQ(count, static_cast<uint32_t>(6 * n)) << "n=" << n;
        EXPECT_EQ(verts.size(), static_cast<size_t>(6 * n)) << "n=" << n;
    }
}

TEST_F(TessellateStringTest, QuadCornerPositions_AdvanceByGlyphCell)
{
    // "AB" — two characters starting at (10, 20).
    // Char 0: x0=10, y0=20, x1=10+GLYPH_CELL, y1=20+GLYPH_CELL
    // Char 1: x0=10+GLYPH_CELL, y0=20, x1=10+2*GLYPH_CELL, y1=20+GLYPH_CELL
    const float startX = 10.0f, startY = 20.0f;
    const float cell = static_cast<float>(GLYPH_CELL);

    std::vector<UIVertex> verts;
    sys.tessellateString("AB", startX, startY, verts);
    ASSERT_EQ(verts.size(), 12u);

    // Char 0 vertices (indices 0–5): two triangles covering [10, 10+cell] x [20, 20+cell]
    // Layout: TL, TR, BR, TL, BR, BL
    const float x0_0 = startX,        x1_0 = startX + cell;
    const float x0_1 = startX + cell, x1_1 = startX + 2.0f * cell;
    const float y0 = startY, y1 = startY + cell;

    EXPECT_NEAR(verts[0].pos.x, x0_0, 1e-5f) << "char0 v0 x";
    EXPECT_NEAR(verts[0].pos.y, y0,   1e-5f) << "char0 v0 y";
    EXPECT_NEAR(verts[1].pos.x, x1_0, 1e-5f) << "char0 v1 x";
    EXPECT_NEAR(verts[1].pos.y, y0,   1e-5f) << "char0 v1 y";
    EXPECT_NEAR(verts[2].pos.x, x1_0, 1e-5f) << "char0 v2 x";
    EXPECT_NEAR(verts[2].pos.y, y1,   1e-5f) << "char0 v2 y";
    EXPECT_NEAR(verts[5].pos.x, x0_0, 1e-5f) << "char0 v5 x";
    EXPECT_NEAR(verts[5].pos.y, y1,   1e-5f) << "char0 v5 y";

    EXPECT_NEAR(verts[6].pos.x,  x0_1, 1e-5f) << "char1 v0 x";
    EXPECT_NEAR(verts[6].pos.y,  y0,   1e-5f) << "char1 v0 y";
    EXPECT_NEAR(verts[7].pos.x,  x1_1, 1e-5f) << "char1 v1 x";
    EXPECT_NEAR(verts[11].pos.x, x0_1, 1e-5f) << "char1 v5 x";
    EXPECT_NEAR(verts[11].pos.y, y1,   1e-5f) << "char1 v5 y";
}

TEST_F(TessellateStringTest, AppendsToExistingVector)
{
    // Pre-populate the vector with two sentinel vertices so we can verify they
    // survive the call (guards against an accidental outVerts.clear() inside
    // tessellateString).
    std::vector<UIVertex> verts;
    verts.push_back({{-1.0f, -2.0f}, {0.1f, 0.2f}});
    verts.push_back({{-3.0f, -4.0f}, {0.3f, 0.4f}});
    const size_t sentinelCount = verts.size();

    uint32_t count = sys.tessellateString("Hi", 0.0f, 0.0f, verts);

    // Two characters → 12 new vertices.
    EXPECT_EQ(count, 12u);
    ASSERT_EQ(verts.size(), sentinelCount + 12u);

    // Sentinel entries must be unmodified.
    EXPECT_NEAR(verts[0].pos.x, -1.0f, 1e-6f) << "sentinel[0] pos.x altered";
    EXPECT_NEAR(verts[0].pos.y, -2.0f, 1e-6f) << "sentinel[0] pos.y altered";
    EXPECT_NEAR(verts[0].uv.x,   0.1f, 1e-6f) << "sentinel[0] uv.x altered";
    EXPECT_NEAR(verts[0].uv.y,   0.2f, 1e-6f) << "sentinel[0] uv.y altered";
    EXPECT_NEAR(verts[1].pos.x, -3.0f, 1e-6f) << "sentinel[1] pos.x altered";
    EXPECT_NEAR(verts[1].pos.y, -4.0f, 1e-6f) << "sentinel[1] pos.y altered";
    EXPECT_NEAR(verts[1].uv.x,   0.3f, 1e-6f) << "sentinel[1] uv.x altered";
    EXPECT_NEAR(verts[1].uv.y,   0.4f, 1e-6f) << "sentinel[1] uv.y altered";

    // Spot-check: first new vertex starts at position (0, 0) for 'H'.
    EXPECT_NEAR(verts[sentinelCount].pos.x, 0.0f, 1e-5f) << "first new vert x";
    EXPECT_NEAR(verts[sentinelCount].pos.y, 0.0f, 1e-5f) << "first new vert y";
}

TEST_F(TessellateStringTest, UVsMatchUvForChar)
{
    // For each character in "Hello", the six tessellated vertices must carry
    // UVs consistent with uvForChar for that character.
    std::string_view text = "Hello";
    std::vector<UIVertex> verts;
    sys.tessellateString(text, 0.0f, 0.0f, verts);
    ASSERT_EQ(verts.size(), text.size() * 6);

    for (size_t i = 0; i < text.size(); ++i) {
        GlyphRect uv = sys.uvForChar(text[i]);
        size_t base = i * 6;
        // Top-left corner (v0 and v3): u0, v0
        EXPECT_NEAR(verts[base + 0].uv.x, uv.u0, 1e-6f) << "char " << i << " v0 u";
        EXPECT_NEAR(verts[base + 0].uv.y, uv.v0, 1e-6f) << "char " << i << " v0 v";
        EXPECT_NEAR(verts[base + 3].uv.x, uv.u0, 1e-6f) << "char " << i << " v3 u";
        EXPECT_NEAR(verts[base + 3].uv.y, uv.v0, 1e-6f) << "char " << i << " v3 v";
        // Top-right corner (v1): u1, v0
        EXPECT_NEAR(verts[base + 1].uv.x, uv.u1, 1e-6f) << "char " << i << " v1 u";
        EXPECT_NEAR(verts[base + 1].uv.y, uv.v0, 1e-6f) << "char " << i << " v1 v";
        // Bottom-right corner (v2 and v4): u1, v1
        EXPECT_NEAR(verts[base + 2].uv.x, uv.u1, 1e-6f) << "char " << i << " v2 u";
        EXPECT_NEAR(verts[base + 2].uv.y, uv.v1, 1e-6f) << "char " << i << " v2 v";
        EXPECT_NEAR(verts[base + 4].uv.x, uv.u1, 1e-6f) << "char " << i << " v4 u";
        EXPECT_NEAR(verts[base + 4].uv.y, uv.v1, 1e-6f) << "char " << i << " v4 v";
        // Bottom-left corner (v5): u0, v1
        EXPECT_NEAR(verts[base + 5].uv.x, uv.u0, 1e-6f) << "char " << i << " v5 u";
        EXPECT_NEAR(verts[base + 5].uv.y, uv.v1, 1e-6f) << "char " << i << " v5 v";
    }
}

TEST_F(TessellateStringTest, NonPrintableChars_FallBackToSpaceGlyphUVs)
{
    // Characters outside the printable ASCII range [32, 126] must produce the
    // same quad UVs as the space glyph (ASCII 32 / index 0) when passed through
    // tessellateString.  This guards the lookup-table bounds check: if the
    // index computation (char - 32) is not clamped, a char like '\0' yields
    // index -32 (out-of-bounds array access), and a char like '\x7f' (127)
    // yields index 95 (one past the 95-element table).
    //
    // Tested codepoints: '\0' (0), '\t' (9), '\n' (10), '\x7f' (127).
    const GlyphRect spaceUV = sys.uvForChar(' ');

    const char nonPrintable[] = {'\0', '\t', '\n', '\x7f'};
    const char* labels[]      = {"NUL(0)", "TAB(9)", "LF(10)", "DEL(127)"};

    for (int i = 0; i < 4; ++i) {
        SCOPED_TRACE(labels[i]);
        std::string text(1, nonPrintable[i]);
        std::vector<UIVertex> verts;
        uint32_t count = sys.tessellateString(text, 0.0f, 0.0f, verts);

        // Must still produce exactly one quad (6 vertices).
        ASSERT_EQ(count, 6u) << labels[i] << " did not produce 6 vertices";
        ASSERT_EQ(verts.size(), 6u);

        // Every UV in the quad must match the space glyph's UV rect.
        // Expected layout (same as TessellateStringTest.UVsMatchUvForChar):
        //   v0 (TL): u0, v0    v1 (TR): u1, v0    v2 (BR): u1, v1
        //   v3 (TL): u0, v0    v4 (BR): u1, v1    v5 (BL): u0, v1
        EXPECT_NEAR(verts[0].uv.x, spaceUV.u0, 1e-6f) << "v0 u";
        EXPECT_NEAR(verts[0].uv.y, spaceUV.v0, 1e-6f) << "v0 v";
        EXPECT_NEAR(verts[1].uv.x, spaceUV.u1, 1e-6f) << "v1 u";
        EXPECT_NEAR(verts[1].uv.y, spaceUV.v0, 1e-6f) << "v1 v";
        EXPECT_NEAR(verts[2].uv.x, spaceUV.u1, 1e-6f) << "v2 u";
        EXPECT_NEAR(verts[2].uv.y, spaceUV.v1, 1e-6f) << "v2 v";
        EXPECT_NEAR(verts[3].uv.x, spaceUV.u0, 1e-6f) << "v3 u";
        EXPECT_NEAR(verts[3].uv.y, spaceUV.v0, 1e-6f) << "v3 v";
        EXPECT_NEAR(verts[4].uv.x, spaceUV.u1, 1e-6f) << "v4 u";
        EXPECT_NEAR(verts[4].uv.y, spaceUV.v1, 1e-6f) << "v4 v";
        EXPECT_NEAR(verts[5].uv.x, spaceUV.u0, 1e-6f) << "v5 u";
        EXPECT_NEAR(verts[5].uv.y, spaceUV.v1, 1e-6f) << "v5 v";
    }
}

TEST_F(TessellateStringTest, NonPrintable_In_MixedString_PositionsAdvanceByGlyphCell)
{
    // "A\tB" — two printable characters bracketing one non-printable tab.
    // The non-printable '\t' is clamped to the space glyph UV, but the cursor
    // must still advance by exactly one GLYPH_CELL so that 'B' lands at
    // x = startX + 2*GLYPH_CELL, not at startX + GLYPH_CELL (skipped advance)
    // or startX + 3*GLYPH_CELL (double-advance).
    //
    // Guard against: a future refactor that conditions cx += cellF on the
    // character being printable, or that advances cx twice for non-printables.
    const float startX = 0.0f, startY = 0.0f;
    const float cell = static_cast<float>(GLYPH_CELL);

    std::vector<UIVertex> verts;
    uint32_t count = sys.tessellateString("A\tB", startX, startY, verts);

    // 3 characters × 6 vertices each = 18 vertices total.
    ASSERT_EQ(count, 18u);
    ASSERT_EQ(verts.size(), 18u);

    // 'A' (char 0, vertices 0–5): x in [startX, startX + cell]
    EXPECT_NEAR(verts[0].pos.x, startX,        1e-5f) << "A TL x";
    EXPECT_NEAR(verts[1].pos.x, startX + cell, 1e-5f) << "A TR x";
    EXPECT_NEAR(verts[0].pos.y, startY,        1e-5f) << "A TL y";
    EXPECT_NEAR(verts[2].pos.y, startY + cell, 1e-5f) << "A BR y";

    // '\t' (char 1, vertices 6–11): cursor must have advanced by exactly one
    // GLYPH_CELL — the quad starts at startX + cell, not at startX (no-advance
    // bug) and not at startX + 2*cell (double-advance bug).
    EXPECT_NEAR(verts[6].pos.x,  startX + cell,        1e-5f) << "\\t TL x";
    EXPECT_NEAR(verts[7].pos.x,  startX + 2.0f * cell, 1e-5f) << "\\t TR x";
    EXPECT_NEAR(verts[6].pos.y,  startY,               1e-5f) << "\\t TL y";
    EXPECT_NEAR(verts[8].pos.y,  startY + cell,        1e-5f) << "\\t BR y";

    // 'B' (char 2, vertices 12–17): must be at startX + 2*cell.
    EXPECT_NEAR(verts[12].pos.x, startX + 2.0f * cell, 1e-5f) << "B TL x";
    EXPECT_NEAR(verts[13].pos.x, startX + 3.0f * cell, 1e-5f) << "B TR x";
    EXPECT_NEAR(verts[12].pos.y, startY,               1e-5f) << "B TL y";
    EXPECT_NEAR(verts[14].pos.y, startY + cell,        1e-5f) << "B BR y";
}

TEST(UISystemUVTable, AllCharacterIndexSpacing_NoDuplicatesOrGaps)
{
    // Verify all 95 glyph UV rects tile the atlas grid without overlap or gap.
    // For each glyph at index i (ASCII char - 32):
    //   - Its expected cell is col = i % COLS, row = i / COLS
    //     where COLS = ATLAS_SIZE / GLYPH_CELL = 16
    //   - Each rect must have exactly (GLYPH_CELL/ATLAS_SIZE) width and height
    //   - Each rect must match its expected cell position exactly (no skipped
    //     cells, no duplicate assignments)
    //   - Consecutive glyphs in the same row must share an edge (u1[i] == u0[i+1])
    //   - The first glyph of each new row must start at u0 == 0 and have
    //     v0 == row * cellSize, ensuring row transitions have no gap
    //
    // This guards against: off-by-one in col/row arithmetic, incorrect cell
    // size constants, and any permutation in buildGlyphTable() that assigns
    // a glyph to the wrong cell index.
    UISystem sys;
    sys.buildGlyphTable();

    constexpr int   COLS      = static_cast<int>(ATLAS_SIZE / GLYPH_CELL); // 16
    constexpr float cellSize  = static_cast<float>(GLYPH_CELL) /
                                static_cast<float>(ATLAS_SIZE);            // 0.0625
    constexpr float expectedArea = cellSize * cellSize;
    constexpr int   NUM_GLYPHS   = 95; // printable ASCII 32..126

    for (int i = 0; i < NUM_GLYPHS; ++i) {
        const char c = static_cast<char>(32 + i);
        SCOPED_TRACE("index=" + std::to_string(i) +
                     " char='" + c + "'");

        GlyphRect r = sys.uvForChar(c);

        // 1. Each rect must be exactly cellSize × cellSize.
        const float width  = r.u1 - r.u0;
        const float height = r.v1 - r.v0;
        EXPECT_NEAR(width,  cellSize, 1e-6f) << "wrong UV width at index " << i;
        EXPECT_NEAR(height, cellSize, 1e-6f) << "wrong UV height at index " << i;

        // Area check is redundant if width/height pass, but serves as an
        // explicit signal in the failure message.
        const float area = width * height;
        EXPECT_NEAR(area, expectedArea, 1e-10f) << "wrong UV area at index " << i;

        // 2. Rect must be at the expected grid cell (no gaps, no duplicates).
        const int   col       = i % COLS;
        const int   row       = i / COLS;
        const float expectedU0 = col * cellSize;
        const float expectedV0 = row * cellSize;
        const float expectedU1 = (col + 1) * cellSize;
        const float expectedV1 = (row + 1) * cellSize;

        EXPECT_NEAR(r.u0, expectedU0, 1e-6f) << "u0 mismatch at index " << i;
        EXPECT_NEAR(r.v0, expectedV0, 1e-6f) << "v0 mismatch at index " << i;
        EXPECT_NEAR(r.u1, expectedU1, 1e-6f) << "u1 mismatch at index " << i;
        EXPECT_NEAR(r.v1, expectedV1, 1e-6f) << "v1 mismatch at index " << i;
    }

    // 3. Consecutive glyphs within the same row must share an edge (u1[i] == u0[i+1]).
    for (int i = 0; i < NUM_GLYPHS - 1; ++i) {
        const int col = i % COLS;
        if (col == COLS - 1)
            continue; // row boundary — next glyph starts a new row, no shared edge
        GlyphRect cur  = sys.uvForChar(static_cast<char>(32 + i));
        GlyphRect next = sys.uvForChar(static_cast<char>(32 + i + 1));
        EXPECT_NEAR(cur.u1, next.u0, 1e-6f)
            << "shared edge missing between index " << i << " and " << (i + 1);
    }

    // 4. The first glyph of each row must start at u0 == 0.
    const int numRows = (NUM_GLYPHS + COLS - 1) / COLS;
    for (int row = 0; row < numRows; ++row) {
        const int firstIdx = row * COLS;
        if (firstIdx >= NUM_GLYPHS) break;
        GlyphRect r = sys.uvForChar(static_cast<char>(32 + firstIdx));
        EXPECT_NEAR(r.u0, 0.0f, 1e-6f)
            << "first glyph of row " << row << " does not start at u0=0";
        EXPECT_NEAR(r.v0, row * cellSize, 1e-6f)
            << "first glyph of row " << row << " has wrong v0";
    }
}

TEST_F(TessellateStringTest, RepeatedCharsSameGlyph_AllVerticesIdenticalUVs)
{
    // Tessellate "AAAA" (4 identical characters) and verify that every one of
    // the 24 resulting vertices carries the same UV rect as uvForChar('A').
    //
    // When all input characters are identical the quad-assembly loop is driven
    // by cursor position alone — a permutation or ordering bug (e.g. using the
    // loop index rather than the character to look up the glyph, or writing UV
    // corners in the wrong order for non-first characters) would surface here
    // while going undetected in a test like UVsMatchUvForChar that uses a
    // string with all distinct characters.
    //
    // Expected UV layout per quad (6 vertices, two triangles TL–TR–BR / TL–BR–BL):
    //   v0 (TL): u0, v0    v1 (TR): u1, v0    v2 (BR): u1, v1
    //   v3 (TL): u0, v0    v4 (BR): u1, v1    v5 (BL): u0, v1
    const GlyphRect uvA = sys.uvForChar('A');

    std::vector<UIVertex> verts;
    uint32_t count = sys.tessellateString("AAAA", 0.0f, 0.0f, verts);

    // 4 characters × 6 vertices = 24 total.
    ASSERT_EQ(count, 24u);
    ASSERT_EQ(verts.size(), 24u);

    for (size_t q = 0; q < 4; ++q) {
        SCOPED_TRACE("quad=" + std::to_string(q));
        const size_t b = q * 6;

        // TL corners (v0 and v3): (u0, v0)
        EXPECT_NEAR(verts[b + 0].uv.x, uvA.u0, 1e-6f) << "v0 u";
        EXPECT_NEAR(verts[b + 0].uv.y, uvA.v0, 1e-6f) << "v0 v";
        EXPECT_NEAR(verts[b + 3].uv.x, uvA.u0, 1e-6f) << "v3 u";
        EXPECT_NEAR(verts[b + 3].uv.y, uvA.v0, 1e-6f) << "v3 v";
        // TR corner (v1): (u1, v0)
        EXPECT_NEAR(verts[b + 1].uv.x, uvA.u1, 1e-6f) << "v1 u";
        EXPECT_NEAR(verts[b + 1].uv.y, uvA.v0, 1e-6f) << "v1 v";
        // BR corners (v2 and v4): (u1, v1)
        EXPECT_NEAR(verts[b + 2].uv.x, uvA.u1, 1e-6f) << "v2 u";
        EXPECT_NEAR(verts[b + 2].uv.y, uvA.v1, 1e-6f) << "v2 v";
        EXPECT_NEAR(verts[b + 4].uv.x, uvA.u1, 1e-6f) << "v4 u";
        EXPECT_NEAR(verts[b + 4].uv.y, uvA.v1, 1e-6f) << "v4 v";
        // BL corner (v5): (u0, v1)
        EXPECT_NEAR(verts[b + 5].uv.x, uvA.u0, 1e-6f) << "v5 u";
        EXPECT_NEAR(verts[b + 5].uv.y, uvA.v1, 1e-6f) << "v5 v";
    }
}

TEST_F(TessellateStringTest, AllNonPrintableRun_CursorAdvancesEvenly)
{
    // A string of all non-printable characters must produce 6 vertices per
    // character with the cursor advancing by exactly GLYPH_CELL for each.
    // This covers the degenerate case where there are no printable characters
    // to anchor the position sequence, complementing the mixed-string test.
    const float startX = 5.0f, startY = 10.0f;
    const float cell = static_cast<float>(GLYPH_CELL);
    const std::string text = "\t\n";  // two non-printable characters

    std::vector<UIVertex> verts;
    uint32_t count = sys.tessellateString(text, startX, startY, verts);

    // 2 characters × 6 vertices each = 12 vertices total.
    ASSERT_EQ(count, 12u);
    ASSERT_EQ(verts.size(), 12u);

    // Char 0 ('\t'): TL at (startX, startY), TR at (startX+cell, startY)
    EXPECT_NEAR(verts[0].pos.x, startX,        1e-5f) << "char0 TL x";
    EXPECT_NEAR(verts[0].pos.y, startY,        1e-5f) << "char0 TL y";
    EXPECT_NEAR(verts[1].pos.x, startX + cell, 1e-5f) << "char0 TR x";
    EXPECT_NEAR(verts[2].pos.y, startY + cell, 1e-5f) << "char0 BR y";

    // Char 1 ('\n'): TL at (startX+cell, startY) — one cell advance
    EXPECT_NEAR(verts[6].pos.x,  startX + cell,        1e-5f) << "char1 TL x";
    EXPECT_NEAR(verts[6].pos.y,  startY,               1e-5f) << "char1 TL y";
    EXPECT_NEAR(verts[7].pos.x,  startX + 2.0f * cell, 1e-5f) << "char1 TR x";
    EXPECT_NEAR(verts[8].pos.y,  startY + cell,        1e-5f) << "char1 BR y";
}

// ---------------------------------------------------------------------------
// TessellateStringDegenerate — overflow and boundary tests for tessellateString
// ---------------------------------------------------------------------------

class TessellateStringDegenerateTest : public ::testing::Test {
protected:
    UISystem sys;

    void SetUp() override {
        sys.buildGlyphTable();
    }
};

TEST_F(TessellateStringDegenerateTest, LongString1000Chars_NoOverflow)
{
    // Tessellate a very long string (1000 characters) to verify:
    // 1. No integer overflow in the returned uint32_t vertex count
    // 2. outVerts.size() equals the returned count
    // 3. Position cursor advances correctly for all characters
    //
    // With 1000 chars × 6 vertices each = 6000 vertices, we're well within
    // uint32_t range but large enough to catch overflow bugs if the count
    // variable wraps around.
    const int numChars = 1000;
    std::string text(numChars, 'X');
    std::vector<UIVertex> verts;
    // Pre-reserve to avoid reallocations
    verts.reserve(numChars * 6);

    uint32_t count = sys.tessellateString(text, 0.0f, 0.0f, verts);

    // Vertex count must match expected: 6 vertices per character
    const uint32_t expectedCount = static_cast<uint32_t>(numChars * 6);
    EXPECT_EQ(count, expectedCount) << "vertex count mismatch";
    EXPECT_EQ(verts.size(), static_cast<size_t>(expectedCount))
        << "vector size must match returned count";

    // Spot-check: first character at x=0, last character at x=(n-1)*cell
    const float cellF = static_cast<float>(GLYPH_CELL);
    const float lastCharX = (numChars - 1) * cellF;

    // First quad (vertices 0-5): x in [0, 32]
    EXPECT_NEAR(verts[0].pos.x, 0.0f, 1e-5f) << "first char TL x";
    EXPECT_NEAR(verts[1].pos.x, cellF, 1e-5f) << "first char TR x";

    // Last quad (vertices 5994-5999): x in [lastCharX, lastCharX+cellF]
    const size_t lastBase = static_cast<size_t>(numChars - 1) * 6;
    EXPECT_NEAR(verts[lastBase + 0].pos.x, lastCharX, 1e-5f)
        << "last char TL x";
    EXPECT_NEAR(verts[lastBase + 1].pos.x, lastCharX + cellF, 1e-5f)
        << "last char TR x";
}

TEST_F(TessellateStringDegenerateTest, LongString5000Chars_VerifyNoWraparound)
{
    // Test with 5000 characters (30000 vertices) to further stress-test
    // for potential overflow or wraparound issues. This is well within
    // uint32_t range but large enough that any overflow would be obvious.
    const int numChars = 5000;
    std::string text(numChars, 'Y');
    std::vector<UIVertex> verts;
    verts.reserve(numChars * 6);

    uint32_t count = sys.tessellateString(text, 0.0f, 0.0f, verts);

    const uint32_t expectedCount = static_cast<uint32_t>(numChars * 6);
    EXPECT_EQ(count, expectedCount) << "vertex count mismatch for 5000 chars";
    EXPECT_EQ(verts.size(), static_cast<size_t>(expectedCount))
        << "vector size must match returned count";

    // Verify cursor position for a middle character (char 2500)
    const float cellF = static_cast<float>(GLYPH_CELL);
    const float midCharX = 2500.0f * cellF;

    const size_t midBase = 2500 * 6;
    EXPECT_NEAR(verts[midBase + 0].pos.x, midCharX, 1e-5f)
        << "char 2500 TL x";
    EXPECT_NEAR(verts[midBase + 1].pos.x, midCharX + cellF, 1e-5f)
        << "char 2500 TR x";
}

// ---------------------------------------------------------------------------
// UISurface — local corner positions define a 4m×2m quad centered at origin
// ---------------------------------------------------------------------------

TEST(UISurfaceTest, LocalCorners_CorrectDimensions)
{
    // The default UISurface must define a 4 m wide × 2 m tall quad centered
    // at the origin in the XY plane.  Accidental edits to these constants
    // silently scale or shift the surface in world space, corrupting M_sw and
    // all clip planes derived from it.
    UISurface surf;

    EXPECT_NEAR(surf.P_00_local.x, -2.0f, 1e-6f) << "P_00 x";
    EXPECT_NEAR(surf.P_00_local.y,  1.0f, 1e-6f) << "P_00 y";
    EXPECT_NEAR(surf.P_00_local.z,  0.0f, 1e-6f) << "P_00 z";

    EXPECT_NEAR(surf.P_10_local.x,  2.0f, 1e-6f) << "P_10 x";
    EXPECT_NEAR(surf.P_10_local.y,  1.0f, 1e-6f) << "P_10 y";
    EXPECT_NEAR(surf.P_10_local.z,  0.0f, 1e-6f) << "P_10 z";

    EXPECT_NEAR(surf.P_01_local.x, -2.0f, 1e-6f) << "P_01 x";
    EXPECT_NEAR(surf.P_01_local.y, -1.0f, 1e-6f) << "P_01 y";
    EXPECT_NEAR(surf.P_01_local.z,  0.0f, 1e-6f) << "P_01 z";

    EXPECT_NEAR(surf.P_11_local.x,  2.0f, 1e-6f) << "P_11 x";
    EXPECT_NEAR(surf.P_11_local.y, -1.0f, 1e-6f) << "P_11 y";
    EXPECT_NEAR(surf.P_11_local.z,  0.0f, 1e-6f) << "P_11 z";
}
