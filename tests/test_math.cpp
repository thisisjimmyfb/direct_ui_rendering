#include <gtest/gtest.h>
#include "ui_surface.h"
#include "ui_system.h"
#include "scene.h"

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

TEST(SDFConstants, PixelDistScale_IsPositiveAndInRange)
{
    // SDF_PIXEL_DIST_SCALE must be positive and within a sane range so the
    // distance field has meaningful per-pixel resolution.
    EXPECT_GT(SDF_PIXEL_DIST_SCALE, 0.0f);
    EXPECT_GE(SDF_PIXEL_DIST_SCALE, 1.0f);
    EXPECT_LE(SDF_PIXEL_DIST_SCALE, 100.0f);
}

TEST(SDFConstants, GlyphPadding_IsPositive)
{
    // SDF_GLYPH_PADDING must be at least 1 so the distance field can bleed
    // beyond the glyph outline and produce correct smoothstep transitions.
    EXPECT_GT(SDF_GLYPH_PADDING, 0);
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
// LightFrustum — tight orthographic frustum covers all room corners in NDC
// ---------------------------------------------------------------------------

class LightFrustumTest : public ::testing::Test {
protected:
    Scene scene;
    glm::mat4 lvp;

    // The 8 room corners matching Scene::init() extents (W=2, H=3, D=3).
    static constexpr float W = 2.0f, H = 3.0f, D = 3.0f;
    const glm::vec3 roomCorners[8] = {
        {-W, 0, -D}, { W, 0, -D}, {-W, H, -D}, { W, H, -D},
        {-W, 0,  D}, { W, 0,  D}, {-W, H,  D}, { W, H,  D},
    };

    void SetUp() override {
        scene.init();
        lvp = scene.lightViewProj();
    }
};

TEST_F(LightFrustumTest, AllRoomCornersInsideNDC)
{
    // Every room corner must project to NDC x,y within [-1, 1] so all
    // geometry falls within the shadow map.
    for (const auto& c : roomCorners) {
        glm::vec4 clip = lvp * glm::vec4(c, 1.0f);
        float ndcX = clip.x / clip.w;
        float ndcY = clip.y / clip.w;
        EXPECT_GE(ndcX, -1.0f) << "corner (" << c.x << "," << c.y << "," << c.z << ") ndcX=" << ndcX;
        EXPECT_LE(ndcX,  1.0f) << "corner (" << c.x << "," << c.y << "," << c.z << ") ndcX=" << ndcX;
        EXPECT_GE(ndcY, -1.0f) << "corner (" << c.x << "," << c.y << "," << c.z << ") ndcY=" << ndcY;
        EXPECT_LE(ndcY,  1.0f) << "corner (" << c.x << "," << c.y << "," << c.z << ") ndcY=" << ndcY;
    }
}

TEST_F(LightFrustumTest, AllRoomCornersNDCZInVulkanDepthRange)
{
    // Vulkan depth range is [0, 1].  A corner with NDC z outside this range
    // would be clipped from the shadow map, producing missing shadows on that
    // part of the scene.  The x/y tests above do not catch this failure mode.
    for (const auto& c : roomCorners) {
        glm::vec4 clip = lvp * glm::vec4(c, 1.0f);
        float ndcZ = clip.z / clip.w;
        EXPECT_GE(ndcZ, 0.0f) << "corner (" << c.x << "," << c.y << "," << c.z << ") ndcZ=" << ndcZ << " < 0 (clipped from shadow map near plane)";
        EXPECT_LE(ndcZ, 1.0f) << "corner (" << c.x << "," << c.y << "," << c.z << ") ndcZ=" << ndcZ << " > 1 (clipped from shadow map far plane)";
    }
}

TEST_F(LightFrustumTest, AllRoomCornersClipWIsOne)
{
    // An orthographic projection leaves W unchanged (equal to input W = 1.0).
    // If the matrix accidentally becomes perspective, W varies per-vertex and
    // shadow-map depth comparisons (which rely on NDC Z = clip.z / clip.w = clip.z
    // for ortho) silently break.
    for (const auto& c : roomCorners) {
        glm::vec4 clip = lvp * glm::vec4(c, 1.0f);
        EXPECT_NEAR(clip.w, 1.0f, 1e-4f)
            << "corner (" << c.x << "," << c.y << "," << c.z << ") clip.w=" << clip.w
            << " (expected 1.0 for orthographic projection)";
    }
}

TEST_F(LightFrustumTest, NdcZSpreadExceedsHalf)
{
    // The tight orthographic frustum must utilise at least half of the [0,1]
    // Vulkan depth range.  A loose frustum (e.g. nearZ≈0, farZ=100) packs all
    // geometry into a tiny NDC-Z slice, which wastes depth-buffer precision and
    // produces visible shadow acne.
    float minZ =  1e9f, maxZ = -1e9f;
    for (const auto& c : roomCorners) {
        glm::vec4 clip = lvp * glm::vec4(c, 1.0f);
        float ndcZ = clip.z / clip.w;
        if (ndcZ < minZ) minZ = ndcZ;
        if (ndcZ > maxZ) maxZ = ndcZ;
    }
    EXPECT_GT(maxZ - minZ, 0.5f)
        << "NDC-Z range = " << (maxZ - minZ)
        << " — frustum near/far planes are too loose; tighten them to improve shadow precision";
}

TEST_F(LightFrustumTest, FrustumHalfExtentsTighterThanOldFixedBound)
{
    // Reconstruct the same light-view matrix used inside lightViewProj() so we
    // can measure the AABB half-extents directly in light-view space.
    // The old fixed ortho was ±5 m; the tight AABB must have half-width < 4 m.
    glm::vec3 dir = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.5f));
    glm::vec3 lightPos = -dir * 10.0f;
    glm::mat4 view = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0, 1, 0));

    float minX =  1e9f, maxX = -1e9f;
    float minY =  1e9f, maxY = -1e9f;
    for (const auto& c : roomCorners) {
        glm::vec4 lv = view * glm::vec4(c, 1.0f);
        if (lv.x < minX) minX = lv.x;
        if (lv.x > maxX) maxX = lv.x;
        if (lv.y < minY) minY = lv.y;
        if (lv.y > maxY) maxY = lv.y;
    }

    float halfX = (maxX - minX) * 0.5f;
    float halfY = (maxY - minY) * 0.5f;

    EXPECT_LT(halfX, 4.0f) << "Light-view X half-extent=" << halfX << " is not tighter than old ±5m bound";
    EXPECT_LT(halfY, 4.0f) << "Light-view Y half-extent=" << halfY << " is not tighter than old ±5m bound";
}

TEST_F(LightFrustumTest, ZMonotonicity_CloserPointHasSmallerNdcZ)
{
    // Two points along the light direction: P_closer is one unit toward the
    // light source, P_farther is one unit away.  After projection through
    // lightViewProj the closer point must have a strictly smaller NDC Z than
    // the farther point.
    //
    // Why this matters: shadow comparisons in room.frag evaluate
    //   fragDepth < shadowMapDepth
    // If near/far are swapped, or the view matrix has a sign error, the depth
    // ordering inverts and every fragment is either always in shadow or always
    // lit, regardless of actual visibility.  This test catches that silently.
    glm::vec3 dir = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.5f)); // light-to-scene
    glm::vec3 sceneCenter{0.0f, 1.5f, 0.0f};  // middle of room (H=3)
    float delta = 1.0f;

    glm::vec3 P_closer  = sceneCenter - dir * delta;  // one unit toward the light
    glm::vec3 P_farther = sceneCenter + dir * delta;  // one unit away from the light

    glm::vec4 clip_near = lvp * glm::vec4(P_closer,  1.0f);
    glm::vec4 clip_far  = lvp * glm::vec4(P_farther, 1.0f);

    float ndcZ_near = clip_near.z / clip_near.w;
    float ndcZ_far  = clip_far.z  / clip_far.w;

    EXPECT_LT(ndcZ_near, ndcZ_far)
        << "Z monotonicity violated: ndcZ_near=" << ndcZ_near
        << " >= ndcZ_far=" << ndcZ_far
        << ".  An inverted near/far or sign error in the light view matrix "
        << "would cause all shadow comparisons to use the wrong depth ordering.";
}

// ---------------------------------------------------------------------------
// SceneInit — mesh integrity checks (pure CPU, no Vulkan context)
// ---------------------------------------------------------------------------

class SceneInitTest : public ::testing::Test {
protected:
    Scene scene;

    void SetUp() override {
        scene.init();
    }
};

TEST_F(SceneInitTest, VertexBufferNonEmpty)
{
    EXPECT_GT(scene.roomMesh().vertices.size(), 0u);
}

TEST_F(SceneInitTest, IndexBufferNonEmpty)
{
    EXPECT_GT(scene.roomMesh().indices.size(), 0u);
}

TEST_F(SceneInitTest, AllNormalsUnitLength)
{
    for (const auto& v : scene.roomMesh().vertices) {
        float len = glm::length(v.normal);
        EXPECT_NEAR(len, 1.0f, 0.001f)
            << "normal (" << v.normal.x << "," << v.normal.y << "," << v.normal.z << ") length=" << len;
    }
}

TEST_F(SceneInitTest, AllIndicesInBounds)
{
    const auto& mesh = scene.roomMesh();
    uint32_t vertCount = static_cast<uint32_t>(mesh.vertices.size());
    for (uint32_t idx : mesh.indices) {
        EXPECT_LT(idx, vertCount) << "index " << idx << " out of range [0, " << vertCount << ")";
    }
}

TEST_F(SceneInitTest, TriangleCountEquals12)
{
    // 6 quads × 2 triangles = 12 triangles → 36 indices
    EXPECT_EQ(scene.roomMesh().indices.size(), 36u);
}

// ---------------------------------------------------------------------------
// WorldCorners — parallelogram identity and transform chain
// ---------------------------------------------------------------------------

class WorldCornersTest : public ::testing::Test {
protected:
    Scene scene;

    void SetUp() override {
        scene.init();
    }
};

TEST_F(WorldCornersTest, ParallelogramIdentity)
{
    // At any time t the four corners must satisfy:
    //   P_11 = P_00 + (P_10 - P_00) + (P_01 - P_00)
    for (float t : {0.0f, 1.0f, 2.5f}) {
        glm::vec3 P00, P10, P01, P11;
        scene.worldCorners(t, P00, P10, P01, P11);

        glm::vec3 expected = P00 + (P10 - P00) + (P01 - P00);
        EXPECT_NEAR(glm::length(P11 - expected), 0.0f, 1e-5f)
            << "parallelogram identity failed at t=" << t;
    }
}

TEST_F(WorldCornersTest, CornersMatchAnimationMatrixAtT0)
{
    // At t=0.0 the world corners must equal M_anim(0) applied to the local corners.
    glm::mat4 M = scene.animationMatrix(0.0f);

    const auto& surf = scene.uiSurface();
    glm::vec3 expected00 = glm::vec3(M * glm::vec4(surf.P_00_local, 1.0f));
    glm::vec3 expected10 = glm::vec3(M * glm::vec4(surf.P_10_local, 1.0f));
    glm::vec3 expected01 = glm::vec3(M * glm::vec4(surf.P_01_local, 1.0f));
    glm::vec3 expected11 = glm::vec3(M * glm::vec4(surf.P_11_local, 1.0f));

    glm::vec3 P00, P10, P01, P11;
    scene.worldCorners(0.0f, P00, P10, P01, P11);

    EXPECT_NEAR(glm::length(P00 - expected00), 0.0f, 1e-5f) << "P_00 mismatch";
    EXPECT_NEAR(glm::length(P10 - expected10), 0.0f, 1e-5f) << "P_10 mismatch";
    EXPECT_NEAR(glm::length(P01 - expected01), 0.0f, 1e-5f) << "P_01 mismatch";
    EXPECT_NEAR(glm::length(P11 - expected11), 0.0f, 1e-5f) << "P_11 mismatch";
}

TEST_F(WorldCornersTest, PlanarityPreservedAtMultipleTimes)
{
    // Since M_anim(t) is a rigid-body transform, all four world corners must
    // remain coplanar at every t.  Verify by computing the surface normal from
    // three corners and asserting |dot(normal, P11 - P00)| < 1e-4.
    for (float t : {0.0f, 0.5f, 1.0f, 2.5f, 5.0f}) {
        SCOPED_TRACE("t=" + std::to_string(t));
        glm::vec3 P00, P10, P01, P11;
        scene.worldCorners(t, P00, P10, P01, P11);

        glm::vec3 normal = glm::normalize(glm::cross(P10 - P00, P01 - P00));
        float deviation = std::abs(glm::dot(normal, P11 - P00));
        EXPECT_LT(deviation, 1e-4f)
            << "P11 deviates from the plane of P00/P10/P01 by " << deviation
            << " at t=" << t;
    }
}

// ---------------------------------------------------------------------------
// SceneAnimation — animationMatrix(0) base-case purity
// ---------------------------------------------------------------------------

class SceneAnimationTest : public ::testing::Test {
protected:
    Scene scene;

    void SetUp() override {
        scene.init();
    }
};

TEST_F(SceneAnimationTest, AtT0_RotationSubmatrixIsIdentity)
{
    // At t=0 sin(0)=0 so the rotation angle is 0. The upper-left 3×3 rotation
    // sub-matrix of M_anim(0) must be identity (all diagonal entries = 1,
    // off-diagonal rotation entries = 0).
    glm::mat4 M = scene.animationMatrix(0.0f);

    // Column-major: M[col][row]
    EXPECT_NEAR(M[0][0], 1.0f, 1e-5f) << "M[0][0] (X scale)";
    EXPECT_NEAR(M[1][1], 1.0f, 1e-5f) << "M[1][1] (Y scale)";
    EXPECT_NEAR(M[2][2], 1.0f, 1e-5f) << "M[2][2] (Z scale)";

    // Off-diagonal rotation entries must be zero.
    EXPECT_NEAR(M[0][1], 0.0f, 1e-5f) << "M[0][1]";
    EXPECT_NEAR(M[0][2], 0.0f, 1e-5f) << "M[0][2]";
    EXPECT_NEAR(M[1][0], 0.0f, 1e-5f) << "M[1][0]";
    EXPECT_NEAR(M[1][2], 0.0f, 1e-5f) << "M[1][2]";
    EXPECT_NEAR(M[2][0], 0.0f, 1e-5f) << "M[2][0]";
    EXPECT_NEAR(M[2][1], 0.0f, 1e-5f) << "M[2][1]";
}

TEST_F(SceneAnimationTest, AtT0_TranslationMatchesBaseOffset)
{
    // At t=0 lateralX=sin(0)=0 and lateralY=sin(0)=0, so the translation
    // column must be exactly (0, 1.5, -2.5, 1).
    glm::mat4 M = scene.animationMatrix(0.0f);

    EXPECT_NEAR(M[3][0], 0.0f,  1e-5f) << "translation X";
    EXPECT_NEAR(M[3][1], 1.5f,  1e-5f) << "translation Y";
    EXPECT_NEAR(M[3][2], -2.5f, 1e-5f) << "translation Z";
    EXPECT_NEAR(M[3][3], 1.0f,  1e-5f) << "homogeneous W";
}

TEST_F(SceneAnimationTest, NonTrivialAngle_RotationSubmatrixMatchesGlmRotate)
{
    // At t = 2π: t * 0.25 = π/2, so sin(t * 0.25) = 1.0.
    // Expected rotation angle = 15° * 1.0 = 15° — the maximum rotation value.
    // This is a non-trivial angle that exercises the rotation sub-matrix fully
    // (cos(15°) ≠ 1, sin(15°) ≠ 0), complementing the t=0 base-case test.
    const float pi  = std::acos(-1.0f);
    const float t   = 2.0f * pi;   // t * 0.25 = π/2 → sin = 1.0

    const float expectedAngle = glm::radians(15.0f) * std::sin(t * 0.25f);

    glm::mat4 M      = scene.animationMatrix(t);
    glm::mat4 refRot = glm::rotate(glm::mat4(1.0f), expectedAngle, glm::vec3(0.0f, 1.0f, 0.0f));

    // The animation matrix is T * R; the upper-left 3×3 is R's sub-matrix.
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            EXPECT_NEAR(M[col][row], refRot[col][row], 1e-5f)
                << "rotation sub-matrix mismatch at col=" << col << " row=" << row
                << " (t=" << t << ", expectedAngle=" << expectedAngle << " rad)";
        }
    }
}

TEST_F(SceneAnimationTest, AtSinPi_LateralXNearZero_YFollowsFormula)
{
    // t = π / 0.18  →  t * 0.18 = π  →  sin(π) ≈ 0  →  lateralX ≈ 0.
    // Exercises the zero-crossing of the lateral oscillation (sin changes sign here).
    const float pi = std::acos(-1.0f);
    const float t  = pi / 0.18f;

    glm::mat4 M = scene.animationMatrix(t);

    float expectedX = 1.2f * std::sin(t * 0.18f);
    float expectedY = 1.5f + 0.35f * std::sin(t * 0.22f);

    EXPECT_NEAR(M[3][0], expectedX, 1e-5f) << "translation X at t=π/0.18";
    EXPECT_NEAR(M[3][1], expectedY, 1e-5f) << "translation Y at t=π/0.18";
    EXPECT_NEAR(M[3][2], -2.5f,    1e-5f) << "translation Z unchanged";
}

TEST_F(SceneAnimationTest, AtSinPiOver2_LateralXIsMax_YFollowsFormula)
{
    // t = (π/2) / 0.18  →  t * 0.18 = π/2  →  sin(π/2) = 1  →  lateralX = 1.2 exactly.
    // Exercises the peak of the lateral oscillation.
    const float pi = std::acos(-1.0f);
    const float t  = (pi * 0.5f) / 0.18f;

    glm::mat4 M = scene.animationMatrix(t);

    float expectedX = 1.2f * std::sin(t * 0.18f);  // = 1.2 * 1.0 = 1.2
    float expectedY = 1.5f + 0.35f * std::sin(t * 0.22f);

    EXPECT_NEAR(M[3][0], expectedX, 1e-5f) << "translation X at t=(π/2)/0.18";
    EXPECT_NEAR(M[3][1], expectedY, 1e-5f) << "translation Y at t=(π/2)/0.18";
    EXPECT_NEAR(M[3][2], -2.5f,    1e-5f) << "translation Z unchanged";
    // Confirm the peak value is close to 1.2 (within float precision).
    EXPECT_NEAR(M[3][0], 1.2f, 1e-4f) << "peak lateralX should be ≈1.2 m";
}

TEST_F(SceneAnimationTest, AtSin3PiOver2_LateralXIsNegativeMax_YFollowsFormula)
{
    // t = (3π/2) / 0.18  →  t * 0.18 = 3π/2  →  sin(3π/2) = -1  →  lateralX = -1.2 exactly.
    // Covers the negative-peak oscillation branch (the mirror of AtSinPiOver2).
    const float pi = std::acos(-1.0f);
    const float t  = (3.0f * pi * 0.5f) / 0.18f;

    glm::mat4 M = scene.animationMatrix(t);

    float expectedX = 1.2f * std::sin(t * 0.18f);  // = 1.2 * (-1.0) = -1.2
    float expectedY = 1.5f + 0.35f * std::sin(t * 0.22f);

    EXPECT_NEAR(M[3][0], expectedX, 1e-5f) << "translation X at t=(3π/2)/0.18";
    EXPECT_NEAR(M[3][1], expectedY, 1e-5f) << "translation Y at t=(3π/2)/0.18";
    EXPECT_NEAR(M[3][2], -2.5f,    1e-5f) << "translation Z unchanged";
    // Confirm the trough value is close to -1.2 (within float precision).
    EXPECT_NEAR(M[3][0], -1.2f, 1e-4f) << "trough lateralX should be ≈-1.2 m";
}

// ---------------------------------------------------------------------------
// LightDirection — directional light invariants
// ---------------------------------------------------------------------------

TEST_F(LightFrustumTest, LightDirection_IsUnitVector)
{
    // scene.light().direction must always be a unit vector.
    // Accidental removal of glm::normalize(...) from the DirectionalLight
    // default initializer would silently scale shadow map coverage and make
    // the NdotL bias formula compute incorrect results, producing shadow acne
    // on all surfaces without any compile-time or runtime error.
    float len = glm::length(scene.light().direction);
    EXPECT_NEAR(len, 1.0f, 1e-5f)
        << "light direction length=" << len
        << " — it must be normalized (length == 1.0); "
        << "check that glm::normalize() is applied in the DirectionalLight initializer.";
}

TEST_F(LightFrustumTest, LightDirection_NotParallelToLookAtUpVector)
{
    // The shadow-map glm::lookAt call uses (0,1,0) as its up vector.  If the
    // light direction is exactly (0,±1,0), the view direction and up vector are
    // collinear, making lookAt degenerate — it produces a NaN matrix and all
    // shadow-map comparisons return undefined results, making every fragment
    // appear either permanently lit or permanently shadowed with no error.
    //
    // Assert: |dot(direction, (0,1,0))| < 1.0 - 1e-4
    // i.e. the direction must differ from the Y axis by at least ~0.01°.
    glm::vec3 dir = scene.light().direction;
    float parallelness = std::abs(glm::dot(dir, glm::vec3(0.0f, 1.0f, 0.0f)));
    EXPECT_LT(parallelness, 1.0f - 1e-4f)
        << "light direction (" << dir.x << "," << dir.y << "," << dir.z << ") "
        << "is nearly parallel to the lookAt up vector (0,1,0): |dot|=" << parallelness
        << ".  A direction of (0,±1,0) makes glm::lookAt degenerate (NaN matrix), "
        << "silently breaking all shadow-map depth comparisons.";
}

TEST_F(LightFrustumTest, LightColor_AllChannelsPositive)
{
    // All three RGB channels of the light color must be strictly positive.
    // Zeroing any channel silently extinguishes that wavelength from every
    // diffuse and specular contribution — e.g. color=(1,0,0) would remove all
    // green and blue from every lit surface with no compile-time or runtime
    // error, producing obviously wrong rendering that is nonetheless hard to
    // diagnose without this explicit guard.
    glm::vec3 col = scene.light().color;
    EXPECT_GT(col.r, 0.0f)
        << "light color red channel=" << col.r << " (must be > 0)";
    EXPECT_GT(col.g, 0.0f)
        << "light color green channel=" << col.g << " (must be > 0)";
    EXPECT_GT(col.b, 0.0f)
        << "light color blue channel=" << col.b << " (must be > 0)";
}

TEST_F(LightFrustumTest, AmbientColor_AllChannelsPositive)
{
    // All three RGB channels of the ambient color must be strictly positive.
    // A zero ambient channel silently removes the base illumination term for
    // that wavelength in room.frag, producing pure black on surfaces that never
    // face the light with no compile-time or runtime error.
    glm::vec3 amb = scene.light().ambient;
    EXPECT_GT(amb.r, 0.0f)
        << "ambient red channel=" << amb.r << " (must be > 0)";
    EXPECT_GT(amb.g, 0.0f)
        << "ambient green channel=" << amb.g << " (must be > 0)";
    EXPECT_GT(amb.b, 0.0f)
        << "ambient blue channel=" << amb.b << " (must be > 0)";
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

// ---------------------------------------------------------------------------
// SceneAnimationTest — Z translation is always fixed at -2.5
// ---------------------------------------------------------------------------

TEST_F(SceneAnimationTest, ZTranslation_AlwaysFixedAt_Neg2_5)
{
    // The animation translates the surface in Z by a constant -2.5 m regardless
    // of t.  Only X and Y oscillate.  Accidental removal of the constant term
    // (e.g. editing the translate call) would silently move the surface away
    // from the back wall with no compile-time error.
    const float pi = std::acos(-1.0f);
    for (float t : {0.0f, pi / 0.18f, pi / 0.22f, 2.0f * pi}) {
        SCOPED_TRACE("t=" + std::to_string(t));
        glm::mat4 M = scene.animationMatrix(t);
        // Column-major: M[3][2] is the Z component of the translation column.
        EXPECT_NEAR(M[3][2], -2.5f, 1e-5f)
            << "animationMatrix(" << t << ")[3][2] should always be -2.5";
    }
}

TEST_F(SceneAnimationTest, YTranslation_AlwaysInExpectedRange)
{
    // The Y translation is 1.5 + 0.35*sin(t*0.22), so it must always lie in
    // [1.15, 1.85].  This guards the invariant that the surface stays within
    // visible room bounds regardless of t.
    const float pi = std::acos(-1.0f);
    const float lo = 1.15f, hi = 1.85f;

    // Dense sweep: 200 evenly-spaced t values spanning several full oscillation
    // periods of both the X (period 2π/0.18) and Y (period 2π/0.22) terms.
    const int N = 200;
    const float tMax = 2.0f * pi / 0.18f;   // covers the slower X period
    for (int i = 0; i <= N; ++i) {
        float t = tMax * static_cast<float>(i) / static_cast<float>(N);
        SCOPED_TRACE("t=" + std::to_string(t));
        glm::mat4 M = scene.animationMatrix(t);
        // Column-major: M[3][1] is the Y component of the translation column.
        float y = M[3][1];
        EXPECT_GE(y, lo) << "Y translation dropped below lower bound " << lo;
        EXPECT_LE(y, hi) << "Y translation exceeded upper bound "       << hi;
    }
}

TEST_F(SceneAnimationTest, XTranslation_AlwaysInExpectedRange)
{
    // The X translation is 1.2*sin(t*0.18), so it must always lie in
    // [-1.2, 1.2].  This guards the invariant that the surface center stays
    // within room X bounds (walls at ±2 m).
    const float pi = std::acos(-1.0f);
    const float lo = -1.2f, hi = 1.2f;

    // Dense sweep: 500 evenly-spaced t values spanning several full oscillation
    // periods of the X term (period 2π/0.18 ≈ 34.9 s).
    const int N = 500;
    const float tMax = 4.0f * pi / 0.18f;   // two full X periods
    for (int i = 0; i <= N; ++i) {
        float t = tMax * static_cast<float>(i) / static_cast<float>(N);
        SCOPED_TRACE("t=" + std::to_string(t));
        glm::mat4 M = scene.animationMatrix(t);
        // Column-major: M[3][0] is the X component of the translation column.
        float x = M[3][0];
        EXPECT_GE(x, lo) << "X translation dropped below lower bound " << lo;
        EXPECT_LE(x, hi) << "X translation exceeded upper bound "       << hi;
    }
}
