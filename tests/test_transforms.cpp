#include <gtest/gtest.h>
#include "ui_surface.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool vec3Near(glm::vec3 a, glm::vec3 b, float eps = 1e-5f)
{
    return glm::length(a - b) < eps;
}

// ---------------------------------------------------------------------------
// M_us tests
// ---------------------------------------------------------------------------

TEST(TransformMath, M_us_MapsOriginToOrigin)
{
    glm::mat4 M = computeM_us(512.0f, 128.0f);
    glm::vec4 result = M * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(result.x, 0.0f, 1e-6f);
    EXPECT_NEAR(result.y, 0.0f, 1e-6f);
}

TEST(TransformMath, M_us_MapsCanvasCornerToOne)
{
    glm::mat4 M = computeM_us(512.0f, 128.0f);
    glm::vec4 result = M * glm::vec4(512.0f, 128.0f, 0.0f, 1.0f);
    EXPECT_NEAR(result.x, 1.0f, 1e-5f);
    EXPECT_NEAR(result.y, 1.0f, 1e-5f);
}

// ---------------------------------------------------------------------------
// M_sw tests
// ---------------------------------------------------------------------------

TEST(TransformMath, M_sw_MapsOriginToP00)
{
    glm::vec3 P00{0, 0, 0}, P10{1, 0, 0}, P01{0, 1, 0};
    glm::mat4 M = computeM_sw(P00, P10, P01);
    glm::vec4 result = M * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(vec3Near(glm::vec3(result), P00));
}

TEST(TransformMath, M_sw_MapsOneOneToP11)
{
    glm::vec3 P00{0, 0, 0}, P10{1, 0, 0}, P01{0, 1, 0};
    glm::mat4 M = computeM_sw(P00, P10, P01);
    // P_11 = P_00 + e_u + e_v = (1, 1, 0)
    glm::vec4 result = M * glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
    glm::vec3 P11 = P00 + (P10 - P00) + (P01 - P00);
    EXPECT_TRUE(vec3Near(glm::vec3(result), P11));
}

TEST(TransformMath, M_sw_SurfaceWithZComponent_AllFourCorners)
{
    // Surface at Z=-2.5 matching the actual scene geometry at t=0.
    // This verifies that M_sw correctly handles non-zero Z in the corner
    // positions — all existing M_sw tests use Z=0.  A bug in the translation
    // column (col 3) of the affine matrix would silently produce the wrong
    // world-space position for every UI vertex at depth.
    glm::vec3 P00{-2.0f,  2.5f, -2.5f};  // world corners at t=0 for the scene surface
    glm::vec3 P10{ 2.0f,  2.5f, -2.5f};
    glm::vec3 P01{-2.0f,  0.5f, -2.5f};
    glm::vec3 P11 = P00 + (P10 - P00) + (P01 - P00);  // = (2, 0.5, -2.5)

    glm::mat4 M = computeM_sw(P00, P10, P01);

    // (0,0,0,1) -> P00
    glm::vec4 r0 = M * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(vec3Near(glm::vec3(r0), P00)) << "origin did not map to P00";

    // (1,0,0,1) -> P10
    glm::vec4 r1 = M * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(vec3Near(glm::vec3(r1), P10)) << "e_u tip did not map to P10";

    // (0,1,0,1) -> P01
    glm::vec4 r2 = M * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_TRUE(vec3Near(glm::vec3(r2), P01)) << "e_v tip did not map to P01";

    // (1,1,0,1) -> P11
    glm::vec4 r3 = M * glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_TRUE(vec3Near(glm::vec3(r3), P11)) << "diagonal corner did not map to P11";
}

// ---------------------------------------------------------------------------
// M_total tests (identity view-projection)
// ---------------------------------------------------------------------------

TEST(TransformMath, M_total_MapsUICornerToWorldCorner_IdentityVP)
{
    glm::vec3 P00{0, 0, 0}, P10{1, 0, 0}, P01{0, 1, 0};
    float W = 512.0f, H = 128.0f;
    glm::mat4 identityVP(1.0f);

    auto t = computeSurfaceTransforms(P00, P10, P01, W, H, identityVP);

    // Top-left UI corner (0,0) -> P00
    glm::vec4 r0 = t.M_total * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(vec3Near(glm::vec3(r0) / r0.w, P00));

    // Top-right UI corner (W,0) -> P10
    glm::vec4 r1 = t.M_total * glm::vec4(W, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(vec3Near(glm::vec3(r1) / r1.w, P10));

    // Bottom-left UI corner (0,H) -> P01
    glm::vec4 r2 = t.M_total * glm::vec4(0.0f, H, 0.0f, 1.0f);
    EXPECT_TRUE(vec3Near(glm::vec3(r2) / r2.w, P01));
}

// ---------------------------------------------------------------------------
// Clip plane sign tests (unit square XY surface)
// ---------------------------------------------------------------------------

class ClipPlaneTest : public ::testing::Test {
protected:
    glm::vec3 P00{0, 0, 0}, P10{1, 0, 0}, P01{0, 1, 0};
    std::array<glm::vec4, 4> planes;

    void SetUp() override {
        planes = computeClipPlanes(P00, P10, P01);
    }

    float clipDot(int i, glm::vec3 p) const {
        return glm::dot(planes[i], glm::vec4(p, 1.0f));
    }
};

TEST_F(ClipPlaneTest, InsidePoint_AllPlanesNonNegative)
{
    glm::vec3 inside{0.5f, 0.5f, 0.0f};
    for (int i = 0; i < 4; ++i) {
        EXPECT_GE(clipDot(i, inside), 0.0f) << "plane " << i;
    }
}

TEST_F(ClipPlaneTest, OutsideLeft_AtLeastOnePlaneNegative)
{
    glm::vec3 outside{-10.0f, 0.5f, 0.0f};
    int negCount = 0;
    for (int i = 0; i < 4; ++i) {
        if (clipDot(i, outside) < 0.0f) ++negCount;
    }
    EXPECT_GE(negCount, 1);
}

TEST_F(ClipPlaneTest, OutsideRight_AtLeastOnePlaneNegative)
{
    glm::vec3 outside{1.5f, 0.5f, 0.0f};
    int negCount = 0;
    for (int i = 0; i < 4; ++i) {
        if (clipDot(i, outside) < 0.0f) ++negCount;
    }
    EXPECT_GE(negCount, 1);
}

TEST_F(ClipPlaneTest, OutsideTop_AtLeastOnePlaneNegative)
{
    glm::vec3 outside{0.5f, -0.5f, 0.0f};
    int negCount = 0;
    for (int i = 0; i < 4; ++i) {
        if (clipDot(i, outside) < 0.0f) ++negCount;
    }
    EXPECT_GE(negCount, 1);
}

TEST_F(ClipPlaneTest, OutsideBottom_AtLeastOnePlaneNegative)
{
    glm::vec3 outside{0.5f, 1.5f, 0.0f};
    int negCount = 0;
    for (int i = 0; i < 4; ++i) {
        if (clipDot(i, outside) < 0.0f) ++negCount;
    }
    EXPECT_GE(negCount, 1);
}

TEST_F(ClipPlaneTest, BoundaryLeftEdge_LeftPlaneNearZero)
{
    // Left edge: x = 0, any y in [0,1]
    glm::vec3 boundary{0.0f, 0.5f, 0.0f};
    // Left plane (index 0) should be ~0
    EXPECT_NEAR(clipDot(0, boundary), 0.0f, 1e-5f);
}

TEST_F(ClipPlaneTest, BoundaryTopEdge_TopPlaneNearZero)
{
    // Top edge: y = 0, any x in [0,1]
    glm::vec3 boundary{0.5f, 0.0f, 0.0f};
    // Top plane (index 2) should be ~0
    EXPECT_NEAR(clipDot(2, boundary), 0.0f, 1e-5f);
}

// ---------------------------------------------------------------------------
// Clip plane boundary conditions — all four edges (unit square surface)
// ---------------------------------------------------------------------------

TEST_F(ClipPlaneTest, BoundaryRightEdge_RightPlaneNearZero)
{
    // Right edge: x = 1, any y in [0,1]
    glm::vec3 boundary{1.0f, 0.5f, 0.0f};
    // Right plane (index 1) should be ~0
    EXPECT_NEAR(clipDot(1, boundary), 0.0f, 1e-5f);
}

TEST_F(ClipPlaneTest, BoundaryBottomEdge_BottomPlaneNearZero)
{
    // Bottom edge: y = 1, any x in [0,1]
    glm::vec3 boundary{0.5f, 1.0f, 0.0f};
    // Bottom plane (index 3) should be ~0
    EXPECT_NEAR(clipDot(3, boundary), 0.0f, 1e-5f);
}

TEST_F(ClipPlaneTest, AllFourEdgeMidpoints_ZeroForOwnPlane)
{
    // Each edge midpoint must lie exactly on its own clip plane.
    struct { glm::vec3 pt; int plane; } cases[] = {
        {{0.0f, 0.5f, 0.0f}, 0},  // left   -> plane 0
        {{1.0f, 0.5f, 0.0f}, 1},  // right  -> plane 1
        {{0.5f, 0.0f, 0.0f}, 2},  // top    -> plane 2
        {{0.5f, 1.0f, 0.0f}, 3},  // bottom -> plane 3
    };
    for (auto& c : cases) {
        EXPECT_NEAR(clipDot(c.plane, c.pt), 0.0f, 1e-5f)
            << "plane " << c.plane;
    }
}

// ---------------------------------------------------------------------------
// Clip planes with a rotated (non-axis-aligned) surface
// ---------------------------------------------------------------------------

class ClipPlaneTiltedTest : public ::testing::Test {
protected:
    // 45-degree rotated square in the XY plane:
    //   e_u = (1,1,0), e_v = (-1,1,0); center = (0,1,0)
    glm::vec3 P00{ 0.0f, 0.0f, 0.0f};
    glm::vec3 P10{ 1.0f, 1.0f, 0.0f};
    glm::vec3 P01{-1.0f, 1.0f, 0.0f};
    std::array<glm::vec4, 4> planes;

    void SetUp() override {
        planes = computeClipPlanes(P00, P10, P01);
    }

    float clipDot(int i, glm::vec3 p) const {
        return glm::dot(planes[i], glm::vec4(p, 1.0f));
    }
};

TEST_F(ClipPlaneTiltedTest, CenterPoint_AllPlanesNonNegative)
{
    // Center = P00 + 0.5*e_u + 0.5*e_v = (0, 1, 0)
    glm::vec3 center{0.0f, 1.0f, 0.0f};
    for (int i = 0; i < 4; ++i) {
        EXPECT_GE(clipDot(i, center), 0.0f) << "plane " << i;
    }
}

TEST_F(ClipPlaneTiltedTest, PointBeyondRightEdge_AtLeastOnePlaneNegative)
{
    // Far to the right of the rotated surface
    glm::vec3 outside{5.0f, 0.0f, 0.0f};
    int negCount = 0;
    for (int i = 0; i < 4; ++i) {
        if (clipDot(i, outside) < 0.0f) ++negCount;
    }
    EXPECT_GE(negCount, 1);
}

TEST_F(ClipPlaneTiltedTest, PointBelowSurface_AtLeastOnePlaneNegative)
{
    // Below the bottom edge of the rotated surface (y < 0 is behind all edges)
    glm::vec3 outside{0.0f, -1.0f, 0.0f};
    int negCount = 0;
    for (int i = 0; i < 4; ++i) {
        if (clipDot(i, outside) < 0.0f) ++negCount;
    }
    EXPECT_GE(negCount, 1);
}

TEST_F(ClipPlaneTiltedTest, TopCorner_OnTopAndLeftPlanes)
{
    // P00 = (0,0,0) lies exactly on the left plane (index 0) and top plane (index 2).
    EXPECT_NEAR(clipDot(0, P00), 0.0f, 1e-5f) << "left plane at P00";
    EXPECT_NEAR(clipDot(2, P00), 0.0f, 1e-5f) << "top plane at P00";
}

// ---------------------------------------------------------------------------
// M_total with non-identity view-projection matrix
// ---------------------------------------------------------------------------

TEST(TransformMath, M_total_MapsUICorners_PerspectiveVP)
{
    glm::vec3 P00{-0.5f,  0.5f, 0.0f};
    glm::vec3 P10{ 0.5f,  0.5f, 0.0f};
    glm::vec3 P01{-0.5f, -0.5f, 0.0f};
    const float W = 512.0f, H = 128.0f;

    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f),
                                 glm::vec3(0.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
    proj[1][1] *= -1.0f;  // Vulkan Y-flip
    glm::mat4 vp = proj * view;

    auto t = computeSurfaceTransforms(P00, P10, P01, W, H, vp);

    auto ndcOf = [](glm::vec4 c) { return glm::vec3(c) / c.w; };

    // UI (0, 0) -> P00
    EXPECT_TRUE(vec3Near(ndcOf(t.M_total * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)),
                         ndcOf(vp * glm::vec4(P00, 1.0f)), 1e-4f));

    // UI (W, 0) -> P10
    EXPECT_TRUE(vec3Near(ndcOf(t.M_total * glm::vec4(W, 0.0f, 0.0f, 1.0f)),
                         ndcOf(vp * glm::vec4(P10, 1.0f)), 1e-4f));

    // UI (0, H) -> P01
    EXPECT_TRUE(vec3Near(ndcOf(t.M_total * glm::vec4(0.0f, H, 0.0f, 1.0f)),
                         ndcOf(vp * glm::vec4(P01, 1.0f)), 1e-4f));
}

// ---------------------------------------------------------------------------
// Depth bias sensitivity
// ---------------------------------------------------------------------------

TEST(DepthBias, ZeroBias_LeavesClipPositionUnchanged)
{
    glm::vec4 clip{0.3f, -0.2f, 0.6f, 1.5f};
    glm::vec4 biased = clip;
    biased.z -= 0.0f * biased.w;
    EXPECT_FLOAT_EQ(biased.z, clip.z);
}

TEST(DepthBias, PositiveBias_ReducesNDCZByBiasAmount)
{
    // Spec formula: clip.z -= bias * clip.w  =>  NDC z decreases by exactly bias.
    glm::vec4 clip{0.0f, 0.0f, 0.8f, 2.0f};
    const float ndcZ_original = clip.z / clip.w;

    for (float bias : {0.0001f, 0.001f, 0.01f}) {
        glm::vec4 biased = clip;
        biased.z -= bias * biased.w;
        EXPECT_NEAR(biased.z / biased.w, ndcZ_original - bias, 1e-5f)
            << "bias = " << bias;
    }
}

TEST(DepthBias, BiasLinearInNDCRegardlessOfW)
{
    // Verify the NDC offset equals bias for different clip.w values.
    for (float w : {0.5f, 1.0f, 2.0f, 4.0f}) {
        glm::vec4 clip{0.0f, 0.0f, 0.5f * w, w};  // NDC z = 0.5 in all cases
        const float bias = 0.001f;
        glm::vec4 biased = clip;
        biased.z -= bias * biased.w;
        EXPECT_NEAR(biased.z / biased.w, clip.z / clip.w - bias, 1e-5f)
            << "w = " << w;
    }
}

// ---------------------------------------------------------------------------
// Surface transform with non-uniform scaling
// ---------------------------------------------------------------------------

TEST(TransformMath, M_total_NonUniformSurface_CornersAndCenter)
{
    // Wide shallow surface: world width 4, world height 0.5
    glm::vec3 P00{0.0f, 0.0f, 0.0f};
    glm::vec3 P10{4.0f, 0.0f, 0.0f};
    glm::vec3 P01{0.0f, 0.5f, 0.0f};
    const float W = 400.0f, H = 100.0f;
    glm::mat4 identityVP(1.0f);

    auto t = computeSurfaceTransforms(P00, P10, P01, W, H, identityVP);

    auto ndcOf = [](glm::vec4 c) { return glm::vec3(c) / c.w; };

    // Bottom-right UI corner -> P11
    glm::vec3 P11 = P00 + (P10 - P00) + (P01 - P00);
    EXPECT_TRUE(vec3Near(ndcOf(t.M_total * glm::vec4(W, H, 0.0f, 1.0f)), P11));

    // Surface center
    glm::vec3 center = P00 + 0.5f * (P10 - P00) + 0.5f * (P01 - P00);
    EXPECT_TRUE(vec3Near(ndcOf(t.M_total * glm::vec4(W * 0.5f, H * 0.5f, 0.0f, 1.0f)), center));

    // Top-right corner -> P10 (no y offset)
    EXPECT_TRUE(vec3Near(ndcOf(t.M_total * glm::vec4(W, 0.0f, 0.0f, 1.0f)), P10));
}

// ---------------------------------------------------------------------------
// Font-size invariance: proportional canvas+quad scaling preserves world position
// ---------------------------------------------------------------------------

TEST(TransformMath, FontSizeInvariance_ProportionalScalePreservesWorldPos)
{
    // When the quad corners and canvas dimensions are both scaled by the same factor,
    // a glyph at a fixed UI-space position must map to the same world-space location.
    // This validates that M_world = M_sw * M_us is invariant under proportional scaling.
    //
    // Derivation: M_us x-scale = 1/(W*s), M_sw x-scale = L_u*s  =>  product = L_u/W (s cancels).

    const float W_base = 512.0f, H_base = 128.0f;
    const float L_u = 2.0f, L_v = 0.5f;   // canonical quad physical dimensions

    glm::vec3 P00{0.0f, 0.0f, 0.0f};
    glm::vec3 P10_base{L_u, 0.0f, 0.0f};
    glm::vec3 P01_base{0.0f, L_v, 0.0f};
    glm::mat4 identityVP(1.0f);

    // Fixed UI-space glyph position (same vertex coordinates regardless of scale).
    glm::vec4 uiPos(64.0f, 16.0f, 0.0f, 1.0f);

    // Compute the canonical world-space landing point.
    auto t_base = computeSurfaceTransforms(P00, P10_base, P01_base, W_base, H_base, identityVP);
    glm::vec3 worldPos_base = glm::vec3(t_base.M_world * uiPos);

    for (float scale : {0.25f, 0.5f, 0.75f, 1.5f, 2.0f, 3.0f}) {
        glm::vec3 P10_s{L_u * scale, 0.0f, 0.0f};
        glm::vec3 P01_s{0.0f, L_v * scale, 0.0f};

        auto t_s = computeSurfaceTransforms(P00, P10_s, P01_s,
                                            W_base * scale, H_base * scale,
                                            identityVP);
        glm::vec3 worldPos_s = glm::vec3(t_s.M_world * uiPos);

        EXPECT_TRUE(vec3Near(worldPos_s, worldPos_base, 1e-5f))
            << "scale = " << scale
            << "  expected (" << worldPos_base.x << ", " << worldPos_base.y << ", " << worldPos_base.z << ")"
            << "  got ("     << worldPos_s.x     << ", " << worldPos_s.y     << ", " << worldPos_s.z     << ")";
    }
}

TEST(TransformMath, FontSizeInvariance_NonUniformScalePreservesWorldPos)
{
    // Same invariance holds when W and H are scaled independently (scaleW != scaleH),
    // as long as each axis is scaled proportionally between canvas and quad.

    const float W_base = 512.0f, H_base = 128.0f;
    const float L_u = 2.0f, L_v = 0.5f;

    glm::vec3 P00{0.0f, 0.0f, 0.0f};
    glm::mat4 identityVP(1.0f);

    glm::vec4 uiPos(64.0f, 16.0f, 0.0f, 1.0f);

    auto t_base = computeSurfaceTransforms(P00,
                                           {L_u, 0.0f, 0.0f},
                                           {0.0f, L_v, 0.0f},
                                           W_base, H_base, identityVP);
    glm::vec3 worldPos_base = glm::vec3(t_base.M_world * uiPos);

    // Non-uniform scales: scaleW and scaleH differ.
    struct ScalePair { float sw, sh; };
    for (auto [sw, sh] : std::initializer_list<ScalePair>{{0.5f, 2.0f}, {2.0f, 0.5f}, {0.3f, 1.7f}}) {
        auto t_s = computeSurfaceTransforms(P00,
                                            {L_u * sw, 0.0f, 0.0f},
                                            {0.0f, L_v * sh, 0.0f},
                                            W_base * sw, H_base * sh,
                                            identityVP);
        glm::vec3 worldPos_s = glm::vec3(t_s.M_world * uiPos);

        EXPECT_NEAR(worldPos_s.x, worldPos_base.x, 1e-5f)
            << "scaleW=" << sw << " scaleH=" << sh;
        EXPECT_NEAR(worldPos_s.y, worldPos_base.y, 1e-5f)
            << "scaleW=" << sw << " scaleH=" << sh;
        EXPECT_NEAR(worldPos_s.z, worldPos_base.z, 1e-5f)
            << "scaleW=" << sw << " scaleH=" << sh;
    }
}

// ---------------------------------------------------------------------------
// ShadowBias — slope-scaled bias formula: max(0.005*(1-NdotL), 0.001)
// ---------------------------------------------------------------------------

TEST(ShadowBias, BackWall_NdotLZero_YieldsFiveThousandths)
{
    // Back wall is perpendicular to light (N·L ≈ 0): largest bias case.
    float NdotL = 0.0f;
    float bias = std::max(0.005f * (1.0f - NdotL), 0.001f);
    EXPECT_NEAR(bias, 0.005f, 1e-6f);
}

TEST(ShadowBias, Floor_NdotLOne_YieldsMinimumClamp)
{
    // Floor directly facing the light (N·L = 1): formula gives 0, clamped to minimum.
    float NdotL = 1.0f;
    float bias = std::max(0.005f * (1.0f - NdotL), 0.001f);
    EXPECT_NEAR(bias, 0.001f, 1e-6f);
}

TEST(ShadowBias, GrazingAngle_NdotLHalf_YieldsIntermediate)
{
    // At N·L = 0.5: 0.005 * 0.5 = 0.0025, above minimum clamp.
    float NdotL = 0.5f;
    float bias = std::max(0.005f * (1.0f - NdotL), 0.001f);
    EXPECT_NEAR(bias, 0.0025f, 1e-6f);
}

// ---------------------------------------------------------------------------
// computeClipPlanes with a 3D surface (non-zero Z)
// All existing clip-plane tests use Z=0 surfaces.  The actual scene places
// the UI surface at Z=-2.5; these tests verify that the clip planes remain
// correct for depth-positioned surfaces.
// ---------------------------------------------------------------------------

class ClipPlane3DTest : public ::testing::Test {
protected:
    // Surface matching the scene geometry at t=0: axis-aligned, at Z=-2.5.
    //   P_00 = (-2,  2.5, -2.5)  top-left
    //   P_10 = ( 2,  2.5, -2.5)  top-right
    //   P_01 = (-2,  0.5, -2.5)  bottom-left
    glm::vec3 P00{-2.0f,  2.5f, -2.5f};
    glm::vec3 P10{ 2.0f,  2.5f, -2.5f};
    glm::vec3 P01{-2.0f,  0.5f, -2.5f};
    std::array<glm::vec4, 4> planes;

    void SetUp() override {
        planes = computeClipPlanes(P00, P10, P01);
    }

    float clipDot(int i, glm::vec3 p) const {
        return glm::dot(planes[i], glm::vec4(p, 1.0f));
    }
};

TEST_F(ClipPlane3DTest, SurfaceCenter_AllPlanesNonNegative)
{
    // Center of the surface: (0, 1.5, -2.5).
    glm::vec3 center{0.0f, 1.5f, -2.5f};
    for (int i = 0; i < 4; ++i) {
        EXPECT_GE(clipDot(i, center), 0.0f) << "plane " << i;
    }
}

TEST_F(ClipPlane3DTest, LeftEdgeMidpoint_LeftPlaneNearZero)
{
    // Midpoint of left edge: P_00 + 0.5*(P_01-P_00) = (-2, 1.5, -2.5).
    glm::vec3 pt{-2.0f, 1.5f, -2.5f};
    EXPECT_NEAR(clipDot(0, pt), 0.0f, 1e-4f) << "left plane (index 0) at left edge midpoint";
}

TEST_F(ClipPlane3DTest, RightEdgeMidpoint_RightPlaneNearZero)
{
    // Midpoint of right edge: P_10 + 0.5*(P_11-P_10) = (2, 1.5, -2.5).
    glm::vec3 pt{2.0f, 1.5f, -2.5f};
    EXPECT_NEAR(clipDot(1, pt), 0.0f, 1e-4f) << "right plane (index 1) at right edge midpoint";
}

TEST_F(ClipPlane3DTest, TopEdgeMidpoint_TopPlaneNearZero)
{
    // Midpoint of top edge: P_00 + 0.5*(P_10-P_00) = (0, 2.5, -2.5).
    glm::vec3 pt{0.0f, 2.5f, -2.5f};
    EXPECT_NEAR(clipDot(2, pt), 0.0f, 1e-4f) << "top plane (index 2) at top edge midpoint";
}

TEST_F(ClipPlane3DTest, BottomEdgeMidpoint_BottomPlaneNearZero)
{
    // Midpoint of bottom edge: P_01 + 0.5*(P_11-P_01) = (0, 0.5, -2.5).
    glm::vec3 pt{0.0f, 0.5f, -2.5f};
    EXPECT_NEAR(clipDot(3, pt), 0.0f, 1e-4f) << "bottom plane (index 3) at bottom edge midpoint";
}

TEST_F(ClipPlane3DTest, OutsideLeft_LeftPlaneNegative)
{
    // Point outside the left edge: x < -2 at same depth.
    glm::vec3 outside{-5.0f, 1.5f, -2.5f};
    EXPECT_LT(clipDot(0, outside), 0.0f) << "left plane should be negative outside left edge";
}

TEST_F(ClipPlane3DTest, OutsideRight_RightPlaneNegative)
{
    glm::vec3 outside{5.0f, 1.5f, -2.5f};
    EXPECT_LT(clipDot(1, outside), 0.0f) << "right plane should be negative outside right edge";
}

TEST_F(ClipPlane3DTest, OutsideTop_TopPlaneNegative)
{
    // Point above top edge: y > 2.5.
    glm::vec3 outside{0.0f, 5.0f, -2.5f};
    EXPECT_LT(clipDot(2, outside), 0.0f) << "top plane should be negative outside top edge";
}

TEST_F(ClipPlane3DTest, OutsideBottom_BottomPlaneNegative)
{
    // Point below bottom edge: y < 0.5.
    glm::vec3 outside{0.0f, -1.0f, -2.5f};
    EXPECT_LT(clipDot(3, outside), 0.0f) << "bottom plane should be negative outside bottom edge";
}

TEST_F(ClipPlane3DTest, OffDepth_CenterAtDifferentZ_StillInside)
{
    // A point at the surface center but displaced in Z (along the surface normal)
    // must still be inside all four clip planes, because the planes are derived
    // from world-space edges and the surface normal is in the Z-only direction here.
    // Displaced by ±0.5 m in Z, XY stays within surface bounds.
    for (float dz : {-0.5f, 0.5f}) {
        glm::vec3 pt{0.0f, 1.5f, -2.5f + dz};
        for (int i = 0; i < 4; ++i) {
            EXPECT_GE(clipDot(i, pt), 0.0f)
                << "plane " << i << " failed for Z-displaced center (dz=" << dz << ")";
        }
    }
}

// ---------------------------------------------------------------------------
// Clip plane structural invariants
// ---------------------------------------------------------------------------

// The clip plane normals must be perpendicular to the surface normal (they
// are "edge normals" lying in the surface plane).  A sign error in the cross
// product inside computeClipPlanes would produce normals with a non-zero
// surface-normal component, silently over- or under-clipping geometry.

TEST_F(ClipPlane3DTest, PlaneNormals_PerpendicularToSurfaceNormal)
{
    glm::vec3 e_u = P10 - P00;
    glm::vec3 e_v = P01 - P00;
    glm::vec3 surfaceNormal = glm::normalize(glm::cross(e_u, e_v));

    for (int i = 0; i < 4; ++i) {
        glm::vec3 planeNormal = glm::normalize(glm::vec3(planes[i]));
        float dot = std::abs(glm::dot(planeNormal, surfaceNormal));
        EXPECT_NEAR(dot, 0.0f, 1e-5f)
            << "plane " << i << " normal has non-zero component along surface normal"
            << " (|dot|=" << dot << "); clip plane should lie in the surface plane";
    }
}

TEST(ClipPlaneSymmetry, LeftRight_AreAntiParallelNormals)
{
    // For any rectangular surface the left and right clip plane normals must be
    // exactly anti-parallel (right = -left).  This is a structural property of
    // the derivation: n_right = -n_left.  A sign error would produce two clip
    // planes pointing the same direction, silently clipping the wrong side.
    glm::vec3 P00{0.0f, 0.0f, 0.0f}, P10{1.0f, 0.0f, 0.0f}, P01{0.0f, 1.0f, 0.0f};
    auto planes = computeClipPlanes(P00, P10, P01);
    glm::vec3 nLeft  = glm::normalize(glm::vec3(planes[0]));
    glm::vec3 nRight = glm::normalize(glm::vec3(planes[1]));
    EXPECT_NEAR(glm::dot(nLeft, nRight), -1.0f, 1e-5f)
        << "left and right clip plane normals must be anti-parallel";
}

TEST(ClipPlaneSymmetry, TopBottom_AreAntiParallelNormals)
{
    glm::vec3 P00{0.0f, 0.0f, 0.0f}, P10{1.0f, 0.0f, 0.0f}, P01{0.0f, 1.0f, 0.0f};
    auto planes = computeClipPlanes(P00, P10, P01);
    glm::vec3 nTop    = glm::normalize(glm::vec3(planes[2]));
    glm::vec3 nBottom = glm::normalize(glm::vec3(planes[3]));
    EXPECT_NEAR(glm::dot(nTop, nBottom), -1.0f, 1e-5f)
        << "top and bottom clip plane normals must be anti-parallel";
}
