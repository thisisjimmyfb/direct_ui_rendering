#include <gtest/gtest.h>
#include "ui_surface.h"

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

// ---------------------------------------------------------------------------
// computeClipPlanes with a Y-axis-rotated 3D surface
//
// The existing ClipPlane3DTest covers a surface with normal = (0,0,-1)
// (axis-aligned at depth), which makes all edge cross-products trivial.
// This fixture rotates the surface around the Y-axis so that
//   e_u = (1, 0, -1),  e_v = (0, 1, 0)
//   n   = normalize(cross(e_u, e_v)) = (1/√2, 0, 1/√2)
// producing clip plane normals that have both X and Z components, exercising
// the full 3D normal computation path inside computeClipPlanes.
// ---------------------------------------------------------------------------

class ClipPlaneYRotatedTest : public ::testing::Test {
protected:
    // Y-rotated surface: top-left at origin, top-right at (1,0,-1),
    // bottom-left at (0,1,0).  Surface normal = (1/√2, 0, 1/√2).
    glm::vec3 P00{0.0f, 0.0f,  0.0f};
    glm::vec3 P10{1.0f, 0.0f, -1.0f};
    glm::vec3 P01{0.0f, 1.0f,  0.0f};
    std::array<glm::vec4, 4> planes;

    void SetUp() override {
        planes = computeClipPlanes(P00, P10, P01);
    }

    float clipDot(int i, glm::vec3 p) const {
        return glm::dot(planes[i], glm::vec4(p, 1.0f));
    }
};

TEST_F(ClipPlaneYRotatedTest, SurfaceCenter_AllPlanesNonNegative)
{
    // Center = P00 + 0.5*e_u + 0.5*e_v = (0.5, 0.5, -0.5)
    glm::vec3 center{0.5f, 0.5f, -0.5f};
    for (int i = 0; i < 4; ++i) {
        EXPECT_GE(clipDot(i, center), 0.0f) << "plane " << i;
    }
}

TEST_F(ClipPlaneYRotatedTest, LeftEdgeMidpoint_LeftPlaneNearZero)
{
    // Midpoint of left edge: P00 + 0.5*e_v = (0, 0.5, 0)
    glm::vec3 pt{0.0f, 0.5f, 0.0f};
    EXPECT_NEAR(clipDot(0, pt), 0.0f, 1e-5f) << "left plane (index 0) at left edge midpoint";
}

TEST_F(ClipPlaneYRotatedTest, RightEdgeMidpoint_RightPlaneNearZero)
{
    // Midpoint of right edge: P10 + 0.5*e_v = (1, 0.5, -1)
    glm::vec3 pt{1.0f, 0.5f, -1.0f};
    EXPECT_NEAR(clipDot(1, pt), 0.0f, 1e-5f) << "right plane (index 1) at right edge midpoint";
}

TEST_F(ClipPlaneYRotatedTest, TopEdgeMidpoint_TopPlaneNearZero)
{
    // Midpoint of top edge: P00 + 0.5*e_u = (0.5, 0, -0.5)
    glm::vec3 pt{0.5f, 0.0f, -0.5f};
    EXPECT_NEAR(clipDot(2, pt), 0.0f, 1e-5f) << "top plane (index 2) at top edge midpoint";
}

TEST_F(ClipPlaneYRotatedTest, BottomEdgeMidpoint_BottomPlaneNearZero)
{
    // Midpoint of bottom edge: P01 + 0.5*e_u = (0.5, 1, -0.5)
    glm::vec3 pt{0.5f, 1.0f, -0.5f};
    EXPECT_NEAR(clipDot(3, pt), 0.0f, 1e-5f) << "bottom plane (index 3) at bottom edge midpoint";
}

TEST_F(ClipPlaneYRotatedTest, OutsideLeft_LeftPlaneNegative)
{
    // P00 - e_u = (-1, 0, 1): behind the left edge in the -e_u direction
    glm::vec3 outside{-1.0f, 0.0f, 1.0f};
    EXPECT_LT(clipDot(0, outside), 0.0f) << "left plane should be negative outside left edge";
}

TEST_F(ClipPlaneYRotatedTest, OutsideRight_RightPlaneNegative)
{
    // P10 + e_u = (2, 0, -2): beyond the right edge in the +e_u direction
    glm::vec3 outside{2.0f, 0.0f, -2.0f};
    EXPECT_LT(clipDot(1, outside), 0.0f) << "right plane should be negative outside right edge";
}

TEST_F(ClipPlaneYRotatedTest, OutsideTop_TopPlaneNegative)
{
    // P00 - e_v = (0, -1, 0): above the top edge in the -e_v direction
    glm::vec3 outside{0.0f, -1.0f, 0.0f};
    EXPECT_LT(clipDot(2, outside), 0.0f) << "top plane should be negative outside top edge";
}

TEST_F(ClipPlaneYRotatedTest, OutsideBottom_BottomPlaneNegative)
{
    // P01 + e_v = (0, 2, 0): below the bottom edge in the +e_v direction
    glm::vec3 outside{0.0f, 2.0f, 0.0f};
    EXPECT_LT(clipDot(3, outside), 0.0f) << "bottom plane should be negative outside bottom edge";
}

TEST_F(ClipPlaneYRotatedTest, PlaneNormals_PerpendicularToSurfaceNormal)
{
    // The surface normal is (1/√2, 0, 1/√2).  All four clip plane normals must
    // be perpendicular to it — this validates the cross products inside
    // computeClipPlanes when the surface normal has both X and Z components.
    glm::vec3 e_u = P10 - P00;
    glm::vec3 e_v = P01 - P00;
    glm::vec3 surfaceNormal = glm::normalize(glm::cross(e_u, e_v));

    for (int i = 0; i < 4; ++i) {
        glm::vec3 planeNormal = glm::normalize(glm::vec3(planes[i]));
        float d = std::abs(glm::dot(planeNormal, surfaceNormal));
        EXPECT_NEAR(d, 0.0f, 1e-5f)
            << "plane " << i << " normal not perpendicular to surface normal (|dot|=" << d << ")";
    }
}

TEST_F(ClipPlaneYRotatedTest, AllFourCorners_OnTwoPlanesBoundary)
{
    // Each corner of the surface lies on exactly two clip planes simultaneously.
    // P00 -> left (0) and top (2)
    // P10 -> right (1) and top (2)
    // P01 -> left (0) and bottom (3)
    // P11 = P00 + e_u + e_v = (1, 1, -1) -> right (1) and bottom (3)
    glm::vec3 P11 = P00 + (P10 - P00) + (P01 - P00);

    EXPECT_NEAR(clipDot(0, P00), 0.0f, 1e-5f) << "P00 on left plane";
    EXPECT_NEAR(clipDot(2, P00), 0.0f, 1e-5f) << "P00 on top plane";
    EXPECT_NEAR(clipDot(1, P10), 0.0f, 1e-5f) << "P10 on right plane";
    EXPECT_NEAR(clipDot(2, P10), 0.0f, 1e-5f) << "P10 on top plane";
    EXPECT_NEAR(clipDot(0, P01), 0.0f, 1e-5f) << "P01 on left plane";
    EXPECT_NEAR(clipDot(3, P01), 0.0f, 1e-5f) << "P01 on bottom plane";
    EXPECT_NEAR(clipDot(1, P11), 0.0f, 1e-5f) << "P11 on right plane";
    EXPECT_NEAR(clipDot(3, P11), 0.0f, 1e-5f) << "P11 on bottom plane";
}
