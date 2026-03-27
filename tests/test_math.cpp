#include <gtest/gtest.h>
#include "ui_surface.h"
#include "ui_system.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
// SDF constant and UISystem accessor tests
// ---------------------------------------------------------------------------

TEST(SDFConstants, ThresholdMatchesOnEdgeValueRatio)
{
    // SDF_THRESHOLD_DEFAULT should be close to SDF_ON_EDGE_VALUE / 255.0f.
    float expected = static_cast<float>(SDF_ON_EDGE_VALUE) / 255.0f;
    EXPECT_NEAR(SDF_THRESHOLD_DEFAULT, expected, 0.01f);
}

TEST(SDFConstants, SdfThresholdReturnsZeroWhenNotSDF)
{
    // Default-constructed UISystem has isSDF()==false; sdfThreshold() must return 0.
    UISystem sys;
    EXPECT_FALSE(sys.isSDF());
    EXPECT_FLOAT_EQ(sys.sdfThreshold(), 0.0f);
}
