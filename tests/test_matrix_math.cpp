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
// M_sw with a 3D parallelogram (non-zero Z AND non-orthogonal e_u/e_v)
//
// All existing M_sw tests cover these properties separately but never combined:
//   • M_sw_SurfaceWithZComponent_AllFourCorners: non-zero Z, but rectangular
//   • M_sw_Parallelogram_AllFourCorners: non-orthogonal edges, but Z=0
//
// This test combines both: a parallelogram at Z=-2.5 with dot(e_u, e_v) ≠ 0.
// This exercises the full M_sw computation where the translation column must
// handle non-zero Z AND the basis vectors must handle non-orthogonal e_u/e_v.
//
//   P_00 = (-1, 0.5, -2.5)
//   P_10 = (2,  0.5, -2.5)   e_u = (3, 0, 0)
//   P_01 = (-0.5, 2.5, -2.5) e_v = (0.5, 2, 0)   dot(e_u, e_v) = 1.5 ≠ 0
//   P_11 = P_00 + e_u + e_v = (2.5, 2.5, -2.5)
// ---------------------------------------------------------------------------

TEST(TransformMath, M_sw_3DParallelogram_AllFourCorners)
{
    // 3D parallelogram with non-zero Z and non-orthogonal edges
    glm::vec3 P00{-1.0f,  0.5f, -2.5f};
    glm::vec3 P10{ 2.0f,  0.5f, -2.5f};
    glm::vec3 P01{-0.5f,  2.5f, -2.5f};
    glm::vec3 P11 = P00 + (P10 - P00) + (P01 - P00);  // = (2.5, 2.5, -2.5)

    glm::mat4 M = computeM_sw(P00, P10, P01);

    // Guard: confirm non-orthogonal edges
    glm::vec3 e_u = P10 - P00;
    glm::vec3 e_v = P01 - P00;
    ASSERT_GT(std::abs(glm::dot(e_u, e_v)), 0.5f)
        << "prerequisite: e_u and e_v must be non-orthogonal for this test";

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
    EXPECT_TRUE(vec3Near(glm::vec3(r3), P11)) << "diagonal corner (1,1) did not map to P11";
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
// M_total — 3D parallelogram + real perspective VP, all four corners
//
// The existing M_total_MapsUICorners_PerspectiveVP test uses a rectangular
// surface (e_u ⊥ e_v) at Z=0 and checks only three corners.  This test
// combines the two properties that existing tests cover separately:
//   • non-zero Z (all corners at Z=-2.5, like the actual scene surface)
//   • non-orthogonal edges  (dot(e_u, e_v) = 3 ≠ 0, same skew as the
//     M_sw_Parallelogram_* fixtures)
// and additionally verifies all four UI-space corners including P11 (the
// bottom-right, which was absent from the perspective test).
//
// The test drives computeSurfaceTransforms with a realistic Vulkan
// view-projection matrix (glm::lookAt + glm::perspective + Y-flip) and
// asserts that M_total * ui_corner == vp * world_corner in NDC for every
// corner.  A bug in M_sw's translation column, in M_us's scale, or in the
// M_total product would violate at least one of these equalities.
// ---------------------------------------------------------------------------

TEST(TransformMath, M_total_Parallelogram3D_PerspectiveVP_AllFourCorners)
{
    // 3D parallelogram with non-orthogonal edges at Z=-2.5:
    //   e_u = P10 - P00 = (3, 0, 0)
    //   e_v = P01 - P00 = (1, 2, 0)   dot(e_u, e_v) = 3 ≠ 0
    glm::vec3 P00{-1.0f, 0.0f, -2.5f};
    glm::vec3 P10{ 2.0f, 0.0f, -2.5f};
    glm::vec3 P01{ 0.0f, 2.0f, -2.5f};
    glm::vec3 P11 = P00 + (P10 - P00) + (P01 - P00);  // = (3, 2, -2.5)

    // Guard: confirm non-orthogonal edges
    glm::vec3 e_u = P10 - P00, e_v = P01 - P00;
    ASSERT_GT(std::abs(glm::dot(e_u, e_v)), 1.0f)
        << "prerequisite: e_u and e_v must be non-orthogonal";

    const float W = 512.0f, H = 128.0f;

    // Realistic Vulkan perspective VP: camera above-and-behind, looking at
    // the surface centre; Y-flip applied for Vulkan NDC convention.
    glm::mat4 view = glm::lookAt(glm::vec3(1.0f, 2.0f, 3.0f),
                                 glm::vec3(1.0f, 1.0f, -2.5f),
                                 glm::vec3(0.0f, 1.0f,  0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    auto t = computeSurfaceTransforms(P00, P10, P01, W, H, vp);

    auto ndcOf = [](glm::vec4 c) { return glm::vec3(c) / c.w; };

    // UI (0, 0)  -> P00
    EXPECT_TRUE(vec3Near(ndcOf(t.M_total * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)),
                         ndcOf(vp * glm::vec4(P00, 1.0f)), 1e-4f))
        << "UI top-left (0,0) did not map to P00 in NDC";

    // UI (W, 0)  -> P10
    EXPECT_TRUE(vec3Near(ndcOf(t.M_total * glm::vec4(W, 0.0f, 0.0f, 1.0f)),
                         ndcOf(vp * glm::vec4(P10, 1.0f)), 1e-4f))
        << "UI top-right (W,0) did not map to P10 in NDC";

    // UI (0, H)  -> P01
    EXPECT_TRUE(vec3Near(ndcOf(t.M_total * glm::vec4(0.0f, H, 0.0f, 1.0f)),
                         ndcOf(vp * glm::vec4(P01, 1.0f)), 1e-4f))
        << "UI bottom-left (0,H) did not map to P01 in NDC";

    // UI (W, H)  -> P11
    EXPECT_TRUE(vec3Near(ndcOf(t.M_total * glm::vec4(W, H, 0.0f, 1.0f)),
                         ndcOf(vp * glm::vec4(P11, 1.0f)), 1e-4f))
        << "UI bottom-right (W,H) did not map to P11 in NDC";
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
// Font-size invariance for parallelogram (skewed) surfaces
//
// The existing FontSizeInvariance tests cover rectangular surfaces (orthogonal
// e_u / e_v). This test verifies the same invariance property holds for a
// parallelogram where e_u ∦ e_v, as described in direct_ui_rendering.md §4
// under "Compatible Primitives". The affine M_sw matrix handles non-orthogonal
// edges without modification, so proportional canvas+quad scaling should still
// preserve world position.
//
//   P_00 = (0, 0, 0)
//   P_10 = (3, 0, 0)   e_u = (3, 0, 0)
//   P_01 = (1, 2, 0)   e_v = (1, 2, 0)   dot(e_u, e_v) = 3 ≠ 0
// ---------------------------------------------------------------------------

TEST(TransformMath, FontSizeInvariance_Parallelogram_ProportionalScalePreservesWorldPos)
{
    // When the quad corners and canvas dimensions are both scaled by the same factor,
    // a glyph at a fixed UI-space position must map to the same world-space location
    // even for a skewed (non-orthogonal) surface.
    //
    // Derivation: M_us x-scale = 1/(W*s), M_sw x-scale = L_u*s  =>  product = L_u/W (s cancels).
    // This holds regardless of whether e_u and e_v are orthogonal.

    const float W_base = 512.0f, H_base = 128.0f;
    glm::vec3 P00{0.0f, 0.0f, 0.0f};
    glm::vec3 P10_base{3.0f, 0.0f, 0.0f};   // e_u = (3, 0, 0)
    glm::vec3 P01_base{1.0f, 2.0f, 0.0f};   // e_v = (1, 2, 0), dot(e_u, e_v) = 3
    glm::mat4 identityVP(1.0f);

    // Fixed UI-space glyph position (same vertex coordinates regardless of scale).
    glm::vec4 uiPos(64.0f, 16.0f, 0.0f, 1.0f);

    // Compute the canonical world-space landing point.
    auto t_base = computeSurfaceTransforms(P00, P10_base, P01_base, W_base, H_base, identityVP);
    glm::vec3 worldPos_base = glm::vec3(t_base.M_world * uiPos);

    // Verify the fixture has non-orthogonal edges.
    glm::vec3 e_u_base = P10_base - P00;
    glm::vec3 e_v_base = P01_base - P00;
    ASSERT_GT(std::abs(glm::dot(e_u_base, e_v_base)), 1.0f)
        << "prerequisite: e_u and e_v must be non-orthogonal for this test";

    // Scale the parallelogram and canvas proportionally; world position must remain invariant.
    for (float scale : {0.25f, 0.5f, 0.75f, 1.5f, 2.0f, 3.0f}) {
        glm::vec3 P10_s = P00 + scale * e_u_base;
        glm::vec3 P01_s = P00 + scale * e_v_base;

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

// ---------------------------------------------------------------------------
// M_sw parallelogram (non-orthogonal e_u / e_v)
//
// All existing M_sw tests use rectangular surfaces where e_u ⊥ e_v.
// This test uses a parallelogram (dot(e_u,e_v) ≠ 0) to verify that
// computeM_sw handles the non-orthogonal affine frame correctly — the spec
// (direct_ui_rendering.md §4) explicitly classifies parallelograms as
// Compatible Primitives that require no changes to M_sw.
//
//   P_00 = (0, 0, 0)
//   P_10 = (3, 0, 0)   e_u = (3, 0, 0)
//   P_01 = (1, 2, 0)   e_v = (1, 2, 0)   dot(e_u, e_v) = 3 ≠ 0
//   P_11 = P_00 + e_u + e_v = (4, 2, 0)
// ---------------------------------------------------------------------------

TEST(TransformMath, M_sw_Parallelogram_AllFourCorners)
{
    glm::vec3 P00{0.0f, 0.0f, 0.0f};
    glm::vec3 P10{3.0f, 0.0f, 0.0f};
    glm::vec3 P01{1.0f, 2.0f, 0.0f};
    glm::vec3 P11 = P00 + (P10 - P00) + (P01 - P00);  // = (4, 2, 0)

    glm::mat4 M = computeM_sw(P00, P10, P01);

    // dot(e_u, e_v) = 3*1 + 0*2 = 3 — non-orthogonal by construction
    glm::vec3 e_u = P10 - P00;
    glm::vec3 e_v = P01 - P00;
    ASSERT_GT(std::abs(glm::dot(e_u, e_v)), 1.0f)
        << "prerequisite: e_u and e_v must be non-orthogonal";

    glm::vec4 r0 = M * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(vec3Near(glm::vec3(r0), P00)) << "origin did not map to P00";

    glm::vec4 r1 = M * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(vec3Near(glm::vec3(r1), P10)) << "e_u tip did not map to P10";

    glm::vec4 r2 = M * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_TRUE(vec3Near(glm::vec3(r2), P01)) << "e_v tip did not map to P01";

    glm::vec4 r3 = M * glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_TRUE(vec3Near(glm::vec3(r3), P11)) << "diagonal corner (1,1) did not map to P11";
}

TEST(TransformMath, M_sw_Parallelogram_InteriorPoint)
{
    // A point at surface parameter (0.5, 0.5) must land at the parallelogram centre.
    glm::vec3 P00{0.0f, 0.0f, 0.0f};
    glm::vec3 P10{3.0f, 0.0f, 0.0f};
    glm::vec3 P01{1.0f, 2.0f, 0.0f};
    glm::vec3 e_u = P10 - P00;
    glm::vec3 e_v = P01 - P00;
    glm::vec3 centre = P00 + 0.5f * e_u + 0.5f * e_v;  // = (2, 1, 0)

    glm::mat4 M = computeM_sw(P00, P10, P01);
    glm::vec4 r = M * glm::vec4(0.5f, 0.5f, 0.0f, 1.0f);
    EXPECT_TRUE(vec3Near(glm::vec3(r), centre))
        << "surface centre (s=0.5,t=0.5) did not map to parallelogram centre";
}

// ---------------------------------------------------------------------------
// computeSurfaceTransforms struct field correctness
//
// Verify that the SurfaceTransforms fields returned by computeSurfaceTransforms
// match what individual computations produce:
//   M_world == M_sw * M_us
//   M_total == viewProj * M_sw * M_us
//
// These tests catch bugs in computeSurfaceTransforms itself (e.g., wrong
// multiplication order) that would not be caught by the corner-mapping tests.
// ---------------------------------------------------------------------------

static bool mat4Near(const glm::mat4& a, const glm::mat4& b, float eps = 1e-5f)
{
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            if (std::abs(a[col][row] - b[col][row]) > eps)
                return false;
    return true;
}

TEST(TransformMath, SurfaceTransforms_MWorldEqualsMswTimesMus)
{
    glm::vec3 P00{-1.0f, 0.5f, -2.5f};
    glm::vec3 P10{ 2.0f, 0.5f, -2.5f};
    glm::vec3 P01{-0.5f, 2.5f, -2.5f};
    const float W = 512.0f, H = 128.0f;
    glm::mat4 identityVP(1.0f);

    auto t       = computeSurfaceTransforms(P00, P10, P01, W, H, identityVP);
    glm::mat4 expected = computeM_sw(P00, P10, P01) * computeM_us(W, H);

    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            EXPECT_NEAR(t.M_world[col][row], expected[col][row], 1e-5f)
                << "M_world[" << col << "][" << row << "] != M_sw*M_us["
                << col << "][" << row << "]";
}

TEST(TransformMath, SurfaceTransforms_MTotalEqualsViewProjTimesMswTimesMus)
{
    glm::vec3 P00{-1.0f, 0.5f, -2.5f};
    glm::vec3 P10{ 2.0f, 0.5f, -2.5f};
    glm::vec3 P01{-0.5f, 2.5f, -2.5f};
    const float W = 512.0f, H = 128.0f;

    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 2.0f, 5.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    proj[1][1] *= -1.0f;  // Vulkan Y-flip
    glm::mat4 vp = proj * view;

    auto t       = computeSurfaceTransforms(P00, P10, P01, W, H, vp);
    glm::mat4 expected = vp * computeM_sw(P00, P10, P01) * computeM_us(W, H);

    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            EXPECT_NEAR(t.M_total[col][row], expected[col][row], 1e-4f)
                << "M_total[" << col << "][" << row << "] != vp*M_sw*M_us["
                << col << "][" << row << "]";
}

// ---------------------------------------------------------------------------
// M_us Degenerate — document baseline behavior for zero canvas dimensions
// ---------------------------------------------------------------------------

// Tests to establish regression baselines before any guard is added.
// These document whether the functions produce NaN, Inf, or well-defined values
// when given degenerate inputs (zero dimensions, zero-length edges, collinear corners).

TEST(TransformMath, M_us_ZeroWidth_W_uiEqualsZero_InfInColumn0)
{
    // With W_ui = 0, the matrix entry 1/W_ui becomes infinity.
    // This documents the baseline behavior before any guard is added.
    glm::mat4 M = computeM_us(0.0f, 128.0f);

    // 1/0.0f = +inf in IEEE 754
    EXPECT_TRUE(std::isinf(M[0][0])) << "M[0][0] should be +inf when W_ui=0";

    // H_ui is non-zero, so 1/H_ui is finite
    EXPECT_TRUE(std::isfinite(M[1][1])) << "M[1][1] should be finite when H_ui=128";
    EXPECT_NEAR(M[1][1], 1.0f / 128.0f, 1e-6f) << "M[1][1] should be 1/H_ui";

    // Other columns unchanged
    EXPECT_NEAR(M[2][2], 1.0f, 1e-6f) << "M[2][2] should be 1";
    EXPECT_NEAR(M[3][3], 1.0f, 1e-6f) << "M[3][3] should be 1";
}

TEST(TransformMath, M_us_ZeroHeight_H_uiEqualsZero_InfInColumn1)
{
    // With H_ui = 0, the matrix entry 1/H_ui becomes infinity.
    // This documents the baseline behavior before any guard is added.
    glm::mat4 M = computeM_us(512.0f, 0.0f);

    // W_ui is non-zero, so 1/W_ui is finite
    EXPECT_TRUE(std::isfinite(M[0][0])) << "M[0][0] should be finite when W_ui=512";
    EXPECT_NEAR(M[0][0], 1.0f / 512.0f, 1e-6f) << "M[0][0] should be 1/W_ui";

    // 1/0.0f = +inf in IEEE 754
    EXPECT_TRUE(std::isinf(M[1][1])) << "M[1][1] should be +inf when H_ui=0";

    // Other columns unchanged
    EXPECT_NEAR(M[2][2], 1.0f, 1e-6f) << "M[2][2] should be 1";
    EXPECT_NEAR(M[3][3], 1.0f, 1e-6f) << "M[3][3] should be 1";
}

TEST(TransformMath, M_us_BothZero_BothDiagonalEntriesInf)
{
    // With W_ui = 0 and H_ui = 0, both 1/W_ui and 1/H_ui become infinity.
    glm::mat4 M = computeM_us(0.0f, 0.0f);

    EXPECT_TRUE(std::isinf(M[0][0])) << "M[0][0] should be +inf when W_ui=0";
    EXPECT_TRUE(std::isinf(M[1][1])) << "M[1][1] should be +inf when H_ui=0";
}

// ---------------------------------------------------------------------------
// M_sw Degenerate — document baseline behavior for zero-length edges
// ---------------------------------------------------------------------------

TEST(TransformMath, M_sw_ZeroEdgeU_P10EqualsP00_ZeroColumn0)
{
    // With P_10 == P_00, the horizontal edge e_u = P_10 - P_00 = (0, 0, 0).
    // The first column of M_sw (which is e_u) becomes a zero column.
    // The normal n = normalize(cross(e_u, e_v)) = normalize(0) = (0, 0, 0).
    // glm::normalize(0) returns (0, 0, 0), so the normal column is also zero.
    glm::vec3 P00{0.0f, 0.0f, 0.0f};
    glm::vec3 P10{0.0f, 0.0f, 0.0f};  // Same as P_00
    glm::vec3 P01{1.0f, 0.0f, 0.0f};

    glm::mat4 M = computeM_sw(P00, P10, P01);

    // e_u = (0, 0, 0) -> first column is zero
    // GLM uses M[col][row] access pattern, so column 0 elements are M[0][0], M[0][1], M[0][2]
    EXPECT_NEAR(M[0][0], 0.0f, 1e-6f) << "M col0.x = e_u.x";
    EXPECT_NEAR(M[0][1], 0.0f, 1e-6f) << "M col0.y = e_u.y";
    EXPECT_NEAR(M[0][2], 0.0f, 1e-6f) << "M col0.z = e_u.z";

    // n = normalize(cross((0,0,0), (1,0,0))) = normalize((0,0,0)) = (0,0,0)
    // Column 2 elements are M[2][0], M[2][1], M[2][2]
    EXPECT_NEAR(M[2][0], 0.0f, 1e-6f) << "M col2.x = n.x";
    EXPECT_NEAR(M[2][1], 0.0f, 1e-6f) << "M col2.y = n.y";
    EXPECT_NEAR(M[2][2], 0.0f, 1e-6f) << "M col2.z = n.z";

    // P_00 is the translation column (column 3)
    EXPECT_NEAR(M[3][0], 0.0f, 1e-6f) << "M col3.x = P_00.x";
    EXPECT_NEAR(M[3][1], 0.0f, 1e-6f) << "M col3.y = P_00.y";
    EXPECT_NEAR(M[3][2], 0.0f, 1e-6f) << "M col3.z = P_00.z";
}

TEST(TransformMath, M_sw_ZeroEdgeV_P01EqualsP00_ZeroColumn1)
{
    // With P_01 == P_00, the vertical edge e_v = P_01 - P_00 = (0, 0, 0).
    // The second column of M_sw (which is e_v) becomes a zero column.
    glm::vec3 P00{0.0f, 0.0f, 0.0f};
    glm::vec3 P10{1.0f, 0.0f, 0.0f};
    glm::vec3 P01{0.0f, 0.0f, 0.0f};  // Same as P_00

    glm::mat4 M = computeM_sw(P00, P10, P01);

    // e_v = (0, 0, 0) -> second column is zero
    EXPECT_NEAR(M[0][1], 0.0f, 1e-6f) << "M col1.x = e_v.x";
    EXPECT_NEAR(M[1][1], 0.0f, 1e-6f) << "M col1.y = e_v.y";
    EXPECT_NEAR(M[2][1], 0.0f, 1e-6f) << "M col1.z = e_v.z";

    // e_u = (1, 0, 0), n = normalize(cross((1,0,0), (0,0,0))) = normalize((0,0,0)) = (0,0,0)
    EXPECT_NEAR(M[0][2], 0.0f, 1e-6f) << "M col2.x = n.x";
    EXPECT_NEAR(M[1][2], 0.0f, 1e-6f) << "M col2.y = n.y";
    EXPECT_NEAR(M[2][2], 0.0f, 1e-6f) << "M col2.z = n.z";
}

TEST(TransformMath, M_sw_BothEdgesZero_AllCornersSame_ZeroMatrixExceptTranslation)
{
    // With all corners at the same position, both e_u and e_v are zero.
    // All columns except the translation column become zero.
    glm::vec3 P00{1.0f, 2.0f, 3.0f};
    glm::vec3 P10{1.0f, 2.0f, 3.0f};
    glm::vec3 P01{1.0f, 2.0f, 3.0f};

    glm::mat4 M = computeM_sw(P00, P10, P01);

    // e_u = (0, 0, 0) -> first column is zero
    // GLM uses M[col][row] access pattern
    EXPECT_NEAR(M[0][0], 0.0f, 1e-6f) << "M col0.x";
    EXPECT_NEAR(M[0][1], 0.0f, 1e-6f) << "M col0.y";
    EXPECT_NEAR(M[0][2], 0.0f, 1e-6f) << "M col0.z";

    // e_v = (0, 0, 0) -> second column is zero
    EXPECT_NEAR(M[1][0], 0.0f, 1e-6f) << "M col1.x";
    EXPECT_NEAR(M[1][1], 0.0f, 1e-6f) << "M col1.y";
    EXPECT_NEAR(M[1][2], 0.0f, 1e-6f) << "M col1.z";

    // n = normalize(cross((0,0,0), (0,0,0))) = (0, 0, 0) -> third column is zero
    EXPECT_NEAR(M[2][0], 0.0f, 1e-6f) << "M col2.x";
    EXPECT_NEAR(M[2][1], 0.0f, 1e-6f) << "M col2.y";
    EXPECT_NEAR(M[2][2], 0.0f, 1e-6f) << "M col2.z";

    // Translation column is P_00
    EXPECT_NEAR(M[3][0], 1.0f, 1e-6f) << "M col3.x = P_00.x";
    EXPECT_NEAR(M[3][1], 2.0f, 1e-6f) << "M col3.y = P_00.y";
    EXPECT_NEAR(M[3][2], 3.0f, 1e-6f) << "M col3.z = P_00.z";
}

// ---------------------------------------------------------------------------
// ClipPlanes Degenerate — document baseline behavior for collinear corners
// ---------------------------------------------------------------------------

TEST(TransformMath, ClipPlanes_CollinearCorners_ZeroNormal)
{
    // When P_00, P_10, and P_01 are collinear, the cross product e_u × e_v = 0,
    // so the surface normal is undefined (zero vector).
    // glm::normalize(0) returns (0, 0, 0).
    glm::vec3 P00{0.0f, 0.0f, 0.0f};
    glm::vec3 P10{1.0f, 0.0f, 0.0f};   // Along X axis
    glm::vec3 P01{2.0f, 0.0f, 0.0f};   // Also along X axis (collinear)

    auto planes = computeClipPlanes(P00, P10, P01);

    // e_u = (1, 0, 0), e_v = (2, 0, 0)
    // cross(e_u, e_v) = (0, 0, 0), so n = (0, 0, 0)
    // All edge normals become (0, 0, 0) since they're derived from n
    for (int i = 0; i < 4; ++i) {
        SCOPED_TRACE("plane " + std::to_string(i));
        EXPECT_EQ(planes[i].x, 0.0f) << "plane " << i << ".x";
        EXPECT_EQ(planes[i].y, 0.0f) << "plane " << i << ".y";
        EXPECT_EQ(planes[i].z, 0.0f) << "plane " << i << ".z";
    }

    // All plane constants are also zero since dot((0,0,0), point) = 0
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(planes[i].w, 0.0f) << "plane " << i << ".w";
    }
}

TEST(TransformMath, ClipPlanes_NonCollinear_ValidNormals)
{
    // Verify that non-collinear corners produce valid non-zero normals.
    glm::vec3 P00{0.0f, 0.0f, 0.0f};
    glm::vec3 P10{1.0f, 0.0f, 0.0f};   // Along X axis
    glm::vec3 P01{0.0f, 1.0f, 0.0f};   // Along Y axis (orthogonal, not collinear)

    auto planes = computeClipPlanes(P00, P10, P01);

    // e_u = (1, 0, 0), e_v = (0, 1, 0)
    // cross(e_u, e_v) = (0, 0, 1), so n = (0, 0, 1)
    // n_left = cross(e_v, n) = cross((0,1,0), (0,0,1)) = (1,0,0)
    // n_right = -n_left = (-1,0,0)
    // n_top = cross(n, e_u) = cross((0,0,1), (1,0,0)) = (0,1,0)
    // n_bottom = -n_top = (0,-1,0)
    // GLM uses M[col][row] access pattern
    EXPECT_NEAR(planes[0].x, 1.0f, 1e-6f) << "left plane normal x";
    EXPECT_NEAR(planes[1].x, -1.0f, 1e-6f) << "right plane normal x";
    EXPECT_NEAR(planes[2].y, 1.0f, 1e-6f) << "top plane normal y";
    EXPECT_NEAR(planes[3].y, -1.0f, 1e-6f) << "bottom plane normal y";
}

