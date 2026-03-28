#include <gtest/gtest.h>
#include "scene.h"
#include "ui_surface.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <string>

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

TEST_F(WorldCornersTest, Scale1_MatchesDefaultUnscaled)
{
    // worldCorners with scaleW=1.0, scaleH=1.0 must produce the same result
    // as the default (no scale arguments).  This guards the default-parameter
    // contract for both parameters.
    for (float t : {0.0f, 1.0f, 2.5f}) {
        SCOPED_TRACE("t=" + std::to_string(t));
        glm::vec3 P00a, P10a, P01a, P11a;
        glm::vec3 P00b, P10b, P01b, P11b;
        scene.worldCorners(t, P00a, P10a, P01a, P11a);
        scene.worldCorners(t, P00b, P10b, P01b, P11b, 1.0f, 1.0f);

        EXPECT_NEAR(glm::length(P00a - P00b), 0.0f, 1e-5f) << "P_00 mismatch at t=" << t;
        EXPECT_NEAR(glm::length(P10a - P10b), 0.0f, 1e-5f) << "P_10 mismatch at t=" << t;
        EXPECT_NEAR(glm::length(P01a - P01b), 0.0f, 1e-5f) << "P_01 mismatch at t=" << t;
        EXPECT_NEAR(glm::length(P11a - P11b), 0.0f, 1e-5f) << "P_11 mismatch at t=" << t;
    }
}

TEST_F(WorldCornersTest, Scale2_CornersAreDoubledRelativeToCenter)
{
    // At t=0 the animation matrix is a pure translation T.
    // The local center is at the origin, so scaled corners satisfy:
    //   P_xx_world(scaleW=2,scaleH=2) = T * (2 * P_xx_local)
    //   = T_translation + 2 * R * P_xx_local  (R=I at t=0)
    // Equivalently: P_scaled - center_world = 2 * (P_unscaled - center_world)
    // where center_world is the world position of the local origin.
    glm::mat4 M = scene.animationMatrix(0.0f);
    glm::vec3 center_world = glm::vec3(M * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

    glm::vec3 P00_1, P10_1, P01_1, P11_1;
    glm::vec3 P00_2, P10_2, P01_2, P11_2;
    scene.worldCorners(0.0f, P00_1, P10_1, P01_1, P11_1, 1.0f, 1.0f);
    scene.worldCorners(0.0f, P00_2, P10_2, P01_2, P11_2, 2.0f, 2.0f);

    EXPECT_NEAR(glm::length((P00_2 - center_world) - 2.0f * (P00_1 - center_world)), 0.0f, 1e-4f) << "P_00 scale=2 mismatch";
    EXPECT_NEAR(glm::length((P10_2 - center_world) - 2.0f * (P10_1 - center_world)), 0.0f, 1e-4f) << "P_10 scale=2 mismatch";
    EXPECT_NEAR(glm::length((P01_2 - center_world) - 2.0f * (P01_1 - center_world)), 0.0f, 1e-4f) << "P_01 scale=2 mismatch";
    EXPECT_NEAR(glm::length((P11_2 - center_world) - 2.0f * (P11_1 - center_world)), 0.0f, 1e-4f) << "P_11 scale=2 mismatch";
}

TEST_F(WorldCornersTest, ScaleHalf_EdgeLengthIsHalved)
{
    // Uniform scaling by 0.5 must halve both edge vectors of the quad.
    // Measured as the length of (P_10 - P_00) and (P_01 - P_00).
    glm::vec3 P00_1, P10_1, P01_1, P11_1;
    glm::vec3 P00_h, P10_h, P01_h, P11_h;
    scene.worldCorners(0.0f, P00_1, P10_1, P01_1, P11_1, 1.0f, 1.0f);
    scene.worldCorners(0.0f, P00_h, P10_h, P01_h, P11_h, 0.5f, 0.5f);

    float wFull = glm::length(P10_1 - P00_1);
    float hFull = glm::length(P01_1 - P00_1);
    float wHalf = glm::length(P10_h - P00_h);
    float hHalf = glm::length(P01_h - P00_h);

    EXPECT_NEAR(wHalf, wFull * 0.5f, 1e-4f) << "horizontal edge not halved";
    EXPECT_NEAR(hHalf, hFull * 0.5f, 1e-4f) << "vertical edge not halved";
}

TEST_F(WorldCornersTest, ScalePreservesParallelogramIdentity)
{
    // The parallelogram identity P_11 = P_00 + (P_10-P_00) + (P_01-P_00)
    // must hold for any uniform scale value.
    for (float scale : {0.5f, 1.5f, 2.0f, 3.0f}) {
        SCOPED_TRACE("scale=" + std::to_string(scale));
        glm::vec3 P00, P10, P01, P11;
        scene.worldCorners(0.0f, P00, P10, P01, P11, scale, scale);

        glm::vec3 expected = P00 + (P10 - P00) + (P01 - P00);
        EXPECT_NEAR(glm::length(P11 - expected), 0.0f, 1e-5f)
            << "parallelogram identity failed at scale=" << scale;
    }
}

TEST_F(WorldCornersTest, NonUniformScale_WidthDoubled_HeightUnchanged)
{
    // scaleW=2, scaleH=1: horizontal edge length doubles, vertical stays the same.
    // This verifies that scaleW and scaleH act independently on the local X/Y axes.
    glm::vec3 P00_1, P10_1, P01_1, P11_1;
    glm::vec3 P00_w, P10_w, P01_w, P11_w;
    scene.worldCorners(0.0f, P00_1, P10_1, P01_1, P11_1, 1.0f, 1.0f);
    scene.worldCorners(0.0f, P00_w, P10_w, P01_w, P11_w, 2.0f, 1.0f);

    float wBase   = glm::length(P10_1 - P00_1);
    float hBase   = glm::length(P01_1 - P00_1);
    float wScaled = glm::length(P10_w - P00_w);
    float hScaled = glm::length(P01_w - P00_w);

    EXPECT_NEAR(wScaled, wBase * 2.0f, 1e-4f) << "horizontal edge should be doubled";
    EXPECT_NEAR(hScaled, hBase,        1e-4f) << "vertical edge should be unchanged";
}

TEST_F(WorldCornersTest, NonUniformScale_HeightDoubled_WidthUnchanged)
{
    // scaleW=1, scaleH=2: vertical edge length doubles, horizontal stays the same.
    glm::vec3 P00_1, P10_1, P01_1, P11_1;
    glm::vec3 P00_h, P10_h, P01_h, P11_h;
    scene.worldCorners(0.0f, P00_1, P10_1, P01_1, P11_1, 1.0f, 1.0f);
    scene.worldCorners(0.0f, P00_h, P10_h, P01_h, P11_h, 1.0f, 2.0f);

    float wBase   = glm::length(P10_1 - P00_1);
    float hBase   = glm::length(P01_1 - P00_1);
    float wScaled = glm::length(P10_h - P00_h);
    float hScaled = glm::length(P01_h - P00_h);

    EXPECT_NEAR(wScaled, wBase,        1e-4f) << "horizontal edge should be unchanged";
    EXPECT_NEAR(hScaled, hBase * 2.0f, 1e-4f) << "vertical edge should be doubled";
}

TEST_F(WorldCornersTest, NonUniformScale_PreservesParallelogramIdentity)
{
    // The parallelogram identity must hold for non-uniform scale combinations.
    struct Case { float w, h; };
    for (auto [sw, sh] : std::initializer_list<Case>{{2.0f, 0.5f}, {0.5f, 2.0f}, {1.5f, 3.0f}}) {
        SCOPED_TRACE("scaleW=" + std::to_string(sw) + " scaleH=" + std::to_string(sh));
        glm::vec3 P00, P10, P01, P11;
        scene.worldCorners(0.0f, P00, P10, P01, P11, sw, sh);

        glm::vec3 expected = P00 + (P10 - P00) + (P01 - P00);
        EXPECT_NEAR(glm::length(P11 - expected), 0.0f, 1e-5f)
            << "parallelogram identity failed for scaleW=" << sw << " scaleH=" << sh;
    }
}

TEST_F(WorldCornersTest, NonUniformScale_CenterStaysFixed)
{
    // The center of the quad ((P_00 + P_11) / 2) must equal the world position
    // of the local origin under any scaleW/scaleH combination, because the local
    // corners are symmetric around the origin.  This confirms that non-uniform
    // scaling does not shift the quad's anchor point.
    glm::mat4 M = scene.animationMatrix(0.0f);
    glm::vec3 center_world = glm::vec3(M * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

    struct Case { float w, h; };
    for (auto [sw, sh] : std::initializer_list<Case>{{2.0f, 0.5f}, {0.5f, 2.0f}, {3.0f, 1.0f}}) {
        SCOPED_TRACE("scaleW=" + std::to_string(sw) + " scaleH=" + std::to_string(sh));
        glm::vec3 P00, P10, P01, P11;
        scene.worldCorners(0.0f, P00, P10, P01, P11, sw, sh);

        glm::vec3 center = (P00 + P11) * 0.5f;
        EXPECT_NEAR(glm::length(center - center_world), 0.0f, 1e-4f)
            << "center shifted for scaleW=" << sw << " scaleH=" << sh;
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

TEST_F(SceneAnimationTest, NormalWiggle_AtPeak_RotationMatchesCombinedYAndZ)
{
    // At t = π, sin(t * 0.5) = sin(π/2) = 1.0, so normalAngle = 25°.
    // The combined rotation is R_Y(yAngle) * R_Z(25°).
    // This verifies the normal-wiggle axis and amplitude are correct.
    const float pi = std::acos(-1.0f);
    const float t  = pi;   // t * 0.5 = π/2 → sin = 1.0

    const float yAngle = glm::radians(15.0f) * std::sin(t * 0.25f);
    const float zAngle = glm::radians(25.0f);   // sin(π * 0.5) = 1

    glm::mat4 M = scene.animationMatrix(t);

    // Build reference: T * R_Y * R_Z (applied in order: Z first, then Y)
    glm::mat4 refRot = glm::mat4(1.0f);
    refRot = glm::rotate(refRot, yAngle, glm::vec3(0.0f, 1.0f, 0.0f));
    refRot = glm::rotate(refRot, zAngle, glm::vec3(0.0f, 0.0f, 1.0f));

    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            EXPECT_NEAR(M[col][row], refRot[col][row], 1e-5f)
                << "combined rotation mismatch at col=" << col << " row=" << row
                << " (t=π, yAngle=" << yAngle << " rad, zAngle=" << zAngle << " rad)";
        }
    }
}

TEST_F(SceneAnimationTest, NormalWiggle_ZeroAt_T0_And_T2Pi)
{
    // The normal wiggle uses sin(t * 0.5f), so it is exactly zero at t=0 and
    // t=2π.  At those times the rotation sub-matrix must equal R_Y alone with no
    // Z-rotation contribution.
    const float pi = std::acos(-1.0f);
    for (float t : {0.0f, 2.0f * pi}) {
        SCOPED_TRACE("t=" + std::to_string(t));
        const float yAngle = glm::radians(15.0f) * std::sin(t * 0.25f);

        glm::mat4 M      = scene.animationMatrix(t);
        glm::mat4 refRot = glm::rotate(glm::mat4(1.0f), yAngle, glm::vec3(0.0f, 1.0f, 0.0f));

        for (int col = 0; col < 3; ++col) {
            for (int row = 0; row < 3; ++row) {
                EXPECT_NEAR(M[col][row], refRot[col][row], 1e-5f)
                    << "normal wiggle should be zero at t=" << t
                    << " — rotation should equal R_Y only"
                    << " (col=" << col << " row=" << row << ")";
            }
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

TEST_F(SceneAnimationTest, NormalWiggle_AtNegativePeak_RotationMatchesCombinedYAndZNeg)
{
    // At t = 3π, sin(t * 0.5) = sin(3π/2) = -1.0, so normalAngle = -25°.
    // The combined rotation is R_Y(yAngle) * R_Z(-25°).
    // This mirrors NormalWiggle_AtPeak which covers the +25° direction and
    // verifies that the Z-rotation is correctly negated, not clamped or
    // abs()-ed somewhere in the implementation.
    const float pi = std::acos(-1.0f);
    const float t  = 3.0f * pi;   // t * 0.5 = 3π/2 → sin = -1.0

    const float yAngle = glm::radians(15.0f) * std::sin(t * 0.25f);
    const float zAngle = -glm::radians(25.0f);   // sin(3π/2) = -1

    glm::mat4 M = scene.animationMatrix(t);

    // Build reference: T * R_Y * R_Z  (applied in order: Z first, then Y)
    glm::mat4 refRot = glm::mat4(1.0f);
    refRot = glm::rotate(refRot, yAngle, glm::vec3(0.0f, 1.0f, 0.0f));
    refRot = glm::rotate(refRot, zAngle, glm::vec3(0.0f, 0.0f, 1.0f));

    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            EXPECT_NEAR(M[col][row], refRot[col][row], 1e-5f)
                << "combined rotation mismatch at col=" << col << " row=" << row
                << " (t=3π, yAngle=" << yAngle << " rad, zAngle=" << zAngle << " rad)";
        }
    }
}
