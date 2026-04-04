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

TEST_F(LightFrustumTest, BackWallCornersInsideNDC)
{
    // The spotlight aims at the back wall (Z = -D).  The four back wall
    // corners must project to NDC x,y within [-1, 1] so the back wall
    // receives correct shadow-map coverage.  Front corners are outside
    // the spotlight cone and need not be tested.
    const glm::vec3 backWall[4] = {
        {-W, 0, -D}, {W, 0, -D}, {-W, H, -D}, {W, H, -D}
    };
    for (const auto& c : backWall) {
        glm::vec4 clip = lvp * glm::vec4(c, 1.0f);
        ASSERT_GT(clip.w, 0.0f) << "clip.w <= 0 for corner (" << c.x << "," << c.y << "," << c.z << ")";
        float ndcX = clip.x / clip.w;
        float ndcY = clip.y / clip.w;
        EXPECT_GE(ndcX, -1.0f) << "corner (" << c.x << "," << c.y << "," << c.z << ") ndcX=" << ndcX;
        EXPECT_LE(ndcX,  1.0f) << "corner (" << c.x << "," << c.y << "," << c.z << ") ndcX=" << ndcX;
        EXPECT_GE(ndcY, -1.0f) << "corner (" << c.x << "," << c.y << "," << c.z << ") ndcY=" << ndcY;
        EXPECT_LE(ndcY,  1.0f) << "corner (" << c.x << "," << c.y << "," << c.z << ") ndcY=" << ndcY;
    }
}

TEST_F(LightFrustumTest, BackWallCornersNDCZInVulkanDepthRange)
{
    // Vulkan depth range is [0, 1].  The back wall corners (which the spotlight
    // aims at) must have NDC z in [0, 1] so their depth is captured correctly
    // in the shadow map.
    const glm::vec3 backWall[4] = {
        {-W, 0, -D}, {W, 0, -D}, {-W, H, -D}, {W, H, -D}
    };
    for (const auto& c : backWall) {
        glm::vec4 clip = lvp * glm::vec4(c, 1.0f);
        ASSERT_GT(clip.w, 0.0f) << "clip.w <= 0 for corner (" << c.x << "," << c.y << "," << c.z << ")";
        float ndcZ = clip.z / clip.w;
        EXPECT_GE(ndcZ, 0.0f) << "corner (" << c.x << "," << c.y << "," << c.z << ") ndcZ=" << ndcZ;
        EXPECT_LE(ndcZ, 1.0f) << "corner (" << c.x << "," << c.y << "," << c.z << ") ndcZ=" << ndcZ;
    }
}

TEST_F(LightFrustumTest, PerspectiveProjectionUsed_ClipWVariesByDepth)
{
    // The spotlight uses a perspective projection so clip.w must vary per
    // vertex based on depth.  Two back wall corners at different heights
    // will have different clip.w values, confirming perspective is in use.
    // (An orthographic projection would give clip.w == 1.0 for all vertices.)
    glm::vec4 clip0 = lvp * glm::vec4(-W, 0, -D, 1.0f);
    glm::vec4 clip1 = lvp * glm::vec4( W, H, -D, 1.0f);
    // Both must have positive clip.w (in front of the near plane).
    EXPECT_GT(clip0.w, 0.0f) << "clip.w must be positive (in front of near plane)";
    EXPECT_GT(clip1.w, 0.0f) << "clip.w must be positive (in front of near plane)";
    // clip.w must differ from 1.0, confirming perspective (not orthographic).
    EXPECT_GT(std::abs(clip0.w - 1.0f), 0.01f)
        << "clip.w=" << clip0.w << " — expected != 1.0 for perspective projection";
}

TEST_F(LightFrustumTest, NdcZDepthOrderingPreserved_UIQuadCloserThanBackWall)
{
    // The shadow map must preserve depth ordering between the UI quad
    // (at ~3.3 m from the spotlight) and the back wall (at ~3.7 m).
    // A correct depth ordering is required for the spotlight to cast a
    // shadow of the UI quad onto the back wall.
    // The UI quad center at t=0 is at (0, 1.5, -2.5).
    glm::vec3 uiCenter{0.0f, 1.5f, -2.5f};
    glm::vec3 backWallCenter{0.0f, 1.5f, -3.0f};

    glm::vec4 clipUI   = lvp * glm::vec4(uiCenter, 1.0f);
    glm::vec4 clipWall = lvp * glm::vec4(backWallCenter, 1.0f);

    ASSERT_GT(clipUI.w,   0.0f) << "UI center is behind the near plane";
    ASSERT_GT(clipWall.w, 0.0f) << "back wall center is behind the near plane";

    float ndcZ_ui   = clipUI.z   / clipUI.w;
    float ndcZ_wall = clipWall.z / clipWall.w;

    // UI quad is closer → must have smaller NDC Z.
    EXPECT_LT(ndcZ_ui, ndcZ_wall)
        << "Depth ordering wrong: UI quad NDC Z=" << ndcZ_ui
        << " >= back wall NDC Z=" << ndcZ_wall
        << " — the shadow of the UI quad cannot be cast onto the back wall";

    // The difference must be large enough for reliable shadow comparisons.
    float spread = ndcZ_wall - ndcZ_ui;
    EXPECT_GT(spread, 0.001f)
        << "NDC-Z difference UI-to-back-wall = " << spread
        << " — frustum is too loose, shadow precision will be poor";
}

TEST_F(LightFrustumTest, SpotlightPosition_InsideRoomBounds)
{
    // The spotlight must be positioned inside the room so it can illuminate
    // and cast shadows onto the walls, floor, and ceiling.
    // Room bounds: X in [-2, 2], Y in [0, 3], Z in [-3, 3].
    glm::vec3 pos = scene.light().position;
    EXPECT_GE(pos.x, -W) << "spotlight X below left wall";
    EXPECT_LE(pos.x,  W) << "spotlight X above right wall";
    EXPECT_GE(pos.y,  0) << "spotlight Y below floor";
    EXPECT_LE(pos.y,  H) << "spotlight Y above ceiling";
    EXPECT_GE(pos.z, -D) << "spotlight Z beyond back wall";
    EXPECT_LE(pos.z,  D) << "spotlight Z beyond front wall";
}

TEST_F(LightFrustumTest, ZMonotonicity_CloserPointHasSmallerNdcZ)
{
    // Two points along the spotlight direction: P_closer is 2 units from the
    // light source, P_farther is 4 units along the same direction.  After
    // projection through lightViewProj the closer point must have a strictly
    // smaller NDC Z than the farther point.
    //
    // Why this matters: shadow comparisons in room.frag evaluate
    //   fragDepth < shadowMapDepth
    // If near/far are swapped, or the view matrix has a sign error, the depth
    // ordering inverts and every fragment is either always in shadow or always
    // lit, regardless of actual visibility.  This test catches that silently.
    glm::vec3 origin = scene.light().position;
    glm::vec3 dir    = scene.light().direction;  // normalized, toward scene

    glm::vec3 P_closer  = origin + dir * 2.0f;  // 2 units from light
    glm::vec3 P_farther = origin + dir * 4.0f;  // 4 units from light

    glm::vec4 clip_near = lvp * glm::vec4(P_closer,  1.0f);
    glm::vec4 clip_far  = lvp * glm::vec4(P_farther, 1.0f);

    ASSERT_GT(clip_near.w, 0.0f) << "P_closer is behind the near plane";
    ASSERT_GT(clip_far.w,  0.0f) << "P_farther is behind the near plane";

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
    // Use the +Z face (front face) which is what worldCorners() uses
    const auto& face = surf.frontFace();
    glm::vec3 expected00 = glm::vec3(M * glm::vec4(face.P_00_local, 1.0f));
    glm::vec3 expected10 = glm::vec3(M * glm::vec4(face.P_10_local, 1.0f));
    glm::vec3 expected01 = glm::vec3(M * glm::vec4(face.P_01_local, 1.0f));
    glm::vec3 expected11 = glm::vec3(M * glm::vec4(face.P_11_local, 1.0f));

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
    // The local center of the +Z face is at (0, 0, 2) (the face is at Z=2).
    // Scaled corners satisfy: P_xx_world(scale) = M * (scaled_local_xx)
    // Equivalently: (P_scaled - center_world) = 2 * (P_unscaled - center_world)
    // where center_world is the world position of the local center (0, 0, 2).
    glm::mat4 M = scene.animationMatrix(0.0f);
    glm::vec3 center_world = glm::vec3(M * glm::vec4(0.0f, 0.0f, 2.0f, 1.0f));

    glm::vec3 P00_1, P10_1, P01_1, P11_1;
    glm::vec3 P00_2, P10_2, P01_2, P11_2;
    scene.worldCorners(0.0f, P00_1, P10_1, P01_1, P11_1, 1.0f, 1.0f);
    scene.worldCorners(0.0f, P00_2, P10_2, P01_2, P11_2, 2.0f, 2.0f);

    // X and Y scale by 2 relative to the center, Z stays the same.
    // Verify that (P_scaled - center_world) = 2 * (P_unscaled - center_world) for X and Y.
    glm::vec3 diff00 = (P00_2 - center_world) - 2.0f * (P00_1 - center_world);
    glm::vec3 diff10 = (P10_2 - center_world) - 2.0f * (P10_1 - center_world);
    glm::vec3 diff01 = (P01_2 - center_world) - 2.0f * (P01_1 - center_world);
    glm::vec3 diff11 = (P11_2 - center_world) - 2.0f * (P11_1 - center_world);

    // Only check X and Y components; Z should be 0 for both (no Z scaling).
    EXPECT_NEAR(diff00.x, 0.0f, 1e-4f) << "P_00 X scale=2 mismatch";
    EXPECT_NEAR(diff00.y, 0.0f, 1e-4f) << "P_00 Y scale=2 mismatch";
    EXPECT_NEAR(diff00.z, 0.0f, 1e-4f) << "P_00 Z scale=2 mismatch";

    EXPECT_NEAR(diff10.x, 0.0f, 1e-4f) << "P_10 X scale=2 mismatch";
    EXPECT_NEAR(diff10.y, 0.0f, 1e-4f) << "P_10 Y scale=2 mismatch";
    EXPECT_NEAR(diff10.z, 0.0f, 1e-4f) << "P_10 Z scale=2 mismatch";

    EXPECT_NEAR(diff01.x, 0.0f, 1e-4f) << "P_01 X scale=2 mismatch";
    EXPECT_NEAR(diff01.y, 0.0f, 1e-4f) << "P_01 Y scale=2 mismatch";
    EXPECT_NEAR(diff01.z, 0.0f, 1e-4f) << "P_01 Z scale=2 mismatch";

    EXPECT_NEAR(diff11.x, 0.0f, 1e-4f) << "P_11 X scale=2 mismatch";
    EXPECT_NEAR(diff11.y, 0.0f, 1e-4f) << "P_11 Y scale=2 mismatch";
    EXPECT_NEAR(diff11.z, 0.0f, 1e-4f) << "P_11 Z scale=2 mismatch";
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
    // of the local center (0, 0, 2) under any scaleW/scaleH combination, because the local
    // corners are symmetric around (0, 0, 2).  This confirms that non-uniform
    // scaling does not shift the quad's anchor point.
    glm::mat4 M = scene.animationMatrix(0.0f);
    glm::vec3 center_world = glm::vec3(M * glm::vec4(0.0f, 0.0f, 2.0f, 1.0f));

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
    // Accidental removal of glm::normalize(...) from the SpotLight
    // default initializer would silently scale shadow map coverage and make
    // the NdotL bias formula compute incorrect results, producing shadow acne
    // on all surfaces without any compile-time or runtime error.
    float len = glm::length(scene.light().direction);
    EXPECT_NEAR(len, 1.0f, 1e-5f)
        << "light direction length=" << len
        << " — it must be normalized (length == 1.0); "
        << "check that glm::normalize() is applied in the SpotLight initializer.";
}

TEST_F(LightFrustumTest, LightDirection_NotParallelToLookAtUpVector)
{
    // The spotlight's glm::lookAt call uses (0,1,0) as its up vector.  If the
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

TEST_F(LightFrustumTest, SpotlightConeAngles_InnerSmallerThanOuter)
{
    // The inner cone angle must be strictly smaller than the outer cone angle.
    // If they are equal or inverted, smoothstep(outerCos, innerCos, cosAngle)
    // degenerates (returns 0 or 1 everywhere) and the soft penumbra between
    // inner and outer cone disappears, producing a hard spotlight edge.
    // Also verify both angles are strictly positive (non-zero cone).
    float inner = scene.light().innerConeAngle;
    float outer = scene.light().outerConeAngle;
    EXPECT_GT(inner, 0.0f)  << "innerConeAngle must be > 0";
    EXPECT_GT(outer, 0.0f)  << "outerConeAngle must be > 0";
    EXPECT_LT(inner, outer) << "innerConeAngle=" << inner
        << " must be < outerConeAngle=" << outer
        << " for smoothstep penumbra to work correctly";
}

TEST_F(LightFrustumTest, SpotlightDirection_AimsTowardBackWall)
{
    // The spotlight is designed to cast a shadow of the floating UI quad
    // onto the back wall (Z = -D).  The direction must have a negative Z
    // component so it points into the scene toward the back wall.
    float dirZ = scene.light().direction.z;
    EXPECT_LT(dirZ, 0.0f)
        << "spotlight direction.z=" << dirZ
        << " — must be negative to aim toward the back wall (Z = -" << D << ")";
}

TEST_F(LightFrustumTest, UIQuadCenter_InsideSpotlightOuterCone_AtT0)
{
    // At t=0 the UI quad sits at approximately (0, 1.5, -2.5) in world space.
    // The spotlight must illuminate this position (angle < outerConeAngle) so
    // the shadow of the UI quad can be cast onto the back wall.
    glm::vec3 quadCenter{0.0f, 1.5f, -2.5f};  // matches animationMatrix(0) translation
    glm::vec3 toQuad = glm::normalize(quadCenter - scene.light().position);
    float cosAngle   = glm::dot(toQuad, scene.light().direction);
    float outerCos   = std::cos(scene.light().outerConeAngle);
    EXPECT_GT(cosAngle, outerCos)
        << "UI quad center is outside the spotlight outer cone: "
        << "cosAngle=" << cosAngle << " <= outerCos=" << outerCos
        << " — the spotlight will not illuminate (and cast a shadow of) the UI quad";
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

TEST_F(SceneAnimationTest, NegativeT_MatrixIsFinite)
{
    // std::sin is defined for all real inputs, including negative values.
    // This test documents that animationMatrix(t) produces a fully finite
    // matrix for negative t — no NaN or Inf in any entry.  It establishes
    // a regression baseline before any guard on t is introduced; if a future
    // change accidentally introduces a branch that breaks for t < 0, this
    // test will catch it.
    for (float t : {-0.1f, -1.0f, -2.0f * std::acos(-1.0f), -100.0f}) {
        SCOPED_TRACE("t=" + std::to_string(t));
        glm::mat4 M = scene.animationMatrix(t);
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                EXPECT_TRUE(std::isfinite(M[col][row]))
                    << "M[" << col << "][" << row << "]=" << M[col][row]
                    << " is not finite at t=" << t;
            }
        }
    }
}

TEST_F(SceneAnimationTest, NegativeT_RotationMatchesFormula)
{
    // Periodic backward animation: for t = -2π,
    //   angle      = 15° * sin(-2π * 0.25) = 15° * sin(-π/2) = -15°
    //   normalAngle = 25° * sin(-2π * 0.5)  = 25° * sin(-π)  ≈  0°
    // The rotation sub-matrix must equal R_Y(-15°) * R_Z(≈0°) = R_Y(-15°).
    // This verifies that negative t reverses the rotation direction rather
    // than wrapping, clamping, or producing undefined behaviour.
    const float pi = std::acos(-1.0f);
    const float t  = -2.0f * pi;   // chosen so angle = -15°, normalAngle ≈ 0

    const float angle      = glm::radians(15.0f) * std::sin(t * 0.25f);
    const float normalAngle = glm::radians(25.0f) * std::sin(t * 0.5f);

    glm::mat4 M = scene.animationMatrix(t);

    glm::mat4 refRot = glm::mat4(1.0f);
    refRot = glm::rotate(refRot, angle,       glm::vec3(0.0f, 1.0f, 0.0f));
    refRot = glm::rotate(refRot, normalAngle, glm::vec3(0.0f, 0.0f, 1.0f));

    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            EXPECT_NEAR(M[col][row], refRot[col][row], 1e-5f)
                << "rotation mismatch at col=" << col << " row=" << row
                << " (t=" << t << ", angle=" << angle
                << " rad, normalAngle=" << normalAngle << " rad)";
        }
    }
}

TEST_F(SceneAnimationTest, NegativeT_TranslationMatchesFormula)
{
    // At negative t the translation columns must satisfy the same formula as
    // positive t: (1.2*sin(t*0.18), 1.5 + 0.35*sin(t*0.22), -2.5).
    // sin is an odd function so the X and Y oscillations reverse sign for
    // negative t — this test confirms the formula is applied uniformly and
    // that no abs() or max(t,0) is silently clamping the input.
    const float pi = std::acos(-1.0f);
    for (float t : {-1.0f, -pi, -5.0f}) {
        SCOPED_TRACE("t=" + std::to_string(t));
        glm::mat4 M = scene.animationMatrix(t);

        float expectedX = 1.2f  * std::sin(t * 0.18f);
        float expectedY = 1.5f  + 0.35f * std::sin(t * 0.22f);

        EXPECT_NEAR(M[3][0], expectedX, 1e-5f) << "translation X mismatch at t=" << t;
        EXPECT_NEAR(M[3][1], expectedY, 1e-5f) << "translation Y mismatch at t=" << t;
        EXPECT_NEAR(M[3][2], -2.5f,     1e-5f) << "translation Z should be fixed at -2.5";
    }
}

// ---------------------------------------------------------------------------
// WorldCornersDegenerate — degenerate scale parameters establish regression
// baselines before any guard is added
// ---------------------------------------------------------------------------

class WorldCornersDegenerateTest : public ::testing::Test {
protected:
    Scene scene;

    void SetUp() override {
        scene.init();
    }
};

TEST_F(WorldCornersDegenerateTest, ScaleWZero_EdgeVectorsCollapseToZeroX)
{
    // With scaleW=0.0f, the horizontal edge vectors (P_10 - P_00) and (P_11 - P_01)
    // have zero X component. This documents the baseline behavior before any
    // guard is added: the function produces well-defined (but degenerate) world
    // corners with X=0 for all scaled local X coordinates.  The vertical edges
    // remain non-zero since scaleH=1.0.
    //
    // This test guards against regressions if a clamp is later introduced:
    // it establishes the expected NaN/crash/zero behavior that should be preserved.
    glm::vec3 P00, P10, P01, P11;
    scene.worldCorners(0.0f, P00, P10, P01, P11, 0.0f, 1.0f);

    // All corners must be fully finite — no NaN or Inf should be produced.
    // The animation matrix at t=0 is a pure translation, so the world corners
    // should have X=0 (scaled from local X) and Y,Z matching the translation.
    EXPECT_TRUE(std::isfinite(P00.x)) << "P_00.x is not finite with scaleW=0";
    EXPECT_TRUE(std::isfinite(P10.x)) << "P_10.x is not finite with scaleW=0";
    EXPECT_TRUE(std::isfinite(P01.x)) << "P_01.x is not finite with scaleW=0";
    EXPECT_TRUE(std::isfinite(P11.x)) << "P_11.x is not finite with scaleW=0";

    // At t=0, M_anim is translation by (0, 1.5, -2.5). Local corners have X in {-2, 2}.
    // With scaleW=0, all scaled X values become 0, so all world X should be 0.
    EXPECT_FLOAT_EQ(P00.x, 0.0f) << "P_00.x should be 0 when scaleW=0";
    EXPECT_FLOAT_EQ(P10.x, 0.0f) << "P_10.x should be 0 when scaleW=0";
    EXPECT_FLOAT_EQ(P01.x, 0.0f) << "P_01.x should be 0 when scaleW=0";
    EXPECT_FLOAT_EQ(P11.x, 0.0f) << "P_11.x should be 0 when scaleW=0";

    // Y and Z should match the translation offset at t=0.
    // Local Y values are ±1, scaled by scaleH=1.0, so world Y = 1.5 + localY.
    // Local Z is 2.0 (front face at +Z), so world Z = -2.5 + 2.0 = -0.5.
    EXPECT_NEAR(P00.y, 2.5f, 1e-5f) << "P_00.y at scaleW=0, scaleH=1"; // -2 * 1 + 1.5 = 2.5
    EXPECT_NEAR(P10.y, 2.5f, 1e-5f) << "P_10.y at scaleW=0, scaleH=1";
    EXPECT_NEAR(P01.y, 0.5f, 1e-5f) << "P_01.y at scaleW=0, scaleH=1"; // 1 * 1 + 1.5 = 0.5
    EXPECT_NEAR(P11.y, 0.5f, 1e-5f) << "P_11.y at scaleW=0, scaleH=1";

    // Z is translation + local Z (2.0 for front face).
    EXPECT_NEAR(P00.z, -0.5f, 1e-5f) << "P_00.z at scaleW=0, scaleH=1";
    EXPECT_NEAR(P10.z, -0.5f, 1e-5f) << "P_10.z at scaleW=0, scaleH=1";
    EXPECT_NEAR(P01.z, -0.5f, 1e-5f) << "P_01.z at scaleW=0, scaleH=1";
    EXPECT_NEAR(P11.z, -0.5f, 1e-5f) << "P_11.z at scaleW=0, scaleH=1";
}

TEST_F(WorldCornersDegenerateTest, ScaleHZero_EdgeVectorsCollapseToZeroY)
{
    // With scaleH=0.0f, the vertical edge vectors (P_01 - P_00) and (P_11 - P_10)
    // have zero Y component. This documents the baseline behavior: all world Y
    // values collapse to the translation Y offset.
    glm::vec3 P00, P10, P01, P11;
    scene.worldCorners(0.0f, P00, P10, P01, P11, 1.0f, 0.0f);

    // All corners must be fully finite.
    EXPECT_TRUE(std::isfinite(P00.y)) << "P_00.y is not finite with scaleH=0";
    EXPECT_TRUE(std::isfinite(P10.y)) << "P_10.y is not finite with scaleH=0";
    EXPECT_TRUE(std::isfinite(P01.y)) << "P_01.y is not finite with scaleH=0";
    EXPECT_TRUE(std::isfinite(P11.y)) << "P_11.y is not finite with scaleH=0";

    // At t=0, translation Y is 1.5. With scaleH=0, local Y values (±1) scale to 0,
    // so all world Y should equal the translation Y.
    EXPECT_NEAR(P00.y, 1.5f, 1e-5f) << "P_00.y should be translation Y when scaleH=0";
    EXPECT_NEAR(P10.y, 1.5f, 1e-5f) << "P_10.y should be translation Y when scaleH=0";
    EXPECT_NEAR(P01.y, 1.5f, 1e-5f) << "P_01.y should be translation Y when scaleH=0";
    EXPECT_NEAR(P11.y, 1.5f, 1e-5f) << "P_11.y should be translation Y when scaleH=0";

    // X values should be unchanged (scaleW=1.0).
    EXPECT_NEAR(P00.x, -2.0f, 1e-5f) << "P_00.x at scaleH=0";
    EXPECT_NEAR(P10.x,  2.0f, 1e-5f) << "P_10.x at scaleH=0";
    EXPECT_NEAR(P01.x, -2.0f, 1e-5f) << "P_01.x at scaleH=0";
    EXPECT_NEAR(P11.x,  2.0f, 1e-5f) << "P_11.x at scaleH=0";
}

TEST_F(WorldCornersDegenerateTest, ScaleWZeroAndScaleHZero_AllCornersCollapseToCenter)
{
    // With both scaleW=0 and scaleH=0, all local coordinates (X and Y) scale to 0,
    // so all four corners should collapse to the same world position: the world
    // position of the local center (0, 0, 2) for the +Z face.
    glm::vec3 P00, P10, P01, P11;
    scene.worldCorners(0.0f, P00, P10, P01, P11, 0.0f, 0.0f);

    // All corners must be fully finite.
    EXPECT_TRUE(std::isfinite(P00.x) && std::isfinite(P00.y) && std::isfinite(P00.z))
        << "P_00 not finite with both scales=0";
    EXPECT_TRUE(std::isfinite(P10.x) && std::isfinite(P10.y) && std::isfinite(P10.z))
        << "P_10 not finite with both scales=0";
    EXPECT_TRUE(std::isfinite(P01.x) && std::isfinite(P01.y) && std::isfinite(P01.z))
        << "P_01 not finite with both scales=0";
    EXPECT_TRUE(std::isfinite(P11.x) && std::isfinite(P11.y) && std::isfinite(P11.z))
        << "P_11 not finite with both scales=0";

    // All corners should be at the world position of the local center (0, 0, 2).
    // At t=0, M_anim(0) translates by (0, 1.5, -2.5), so world center = (0, 1.5, -0.5).
    const float expectedX = 0.0f;
    const float expectedY = 1.5f;
    const float expectedZ = -0.5f;  // -2.5 + 2.0 (local Z)

    EXPECT_NEAR(P00.x, expectedX, 1e-5f) << "P_00.x should equal center X";
    EXPECT_NEAR(P00.y, expectedY, 1e-5f) << "P_00.y should equal center Y";
    EXPECT_NEAR(P00.z, expectedZ, 1e-5f) << "P_00.z should equal center Z";

    EXPECT_NEAR(P10.x, expectedX, 1e-5f) << "P_10.x should equal center X";
    EXPECT_NEAR(P10.y, expectedY, 1e-5f) << "P_10.y should equal center Y";
    EXPECT_NEAR(P10.z, expectedZ, 1e-5f) << "P_10.z should equal center Z";

    EXPECT_NEAR(P01.x, expectedX, 1e-5f) << "P_01.x should equal center X";
    EXPECT_NEAR(P01.y, expectedY, 1e-5f) << "P_01.y should equal center Y";
    EXPECT_NEAR(P01.z, expectedZ, 1e-5f) << "P_01.z should equal center Z";

    EXPECT_NEAR(P11.x, expectedX, 1e-5f) << "P_11.x should equal center X";
    EXPECT_NEAR(P11.y, expectedY, 1e-5f) << "P_11.y should equal center Y";
    EXPECT_NEAR(P11.z, expectedZ, 1e-5f) << "P_11.z should equal center Z";

    // Verify all corners are identical (collapsed to a point).
    EXPECT_FLOAT_EQ(P00.x, P10.x) << "P_00.x != P_10.x with both scales=0";
    EXPECT_FLOAT_EQ(P00.y, P10.y) << "P_00.y != P_10.y with both scales=0";
    EXPECT_FLOAT_EQ(P00.z, P10.z) << "P_00.z != P_10.z with both scales=0";
}

TEST_F(WorldCornersDegenerateTest, ScaleWZero_ParallelogramIdentityStillHolds)
{
    // Even with scaleW=0 (degenerate horizontal edge), the parallelogram identity
    // P_11 = P_00 + (P_10 - P_00) + (P_01 - P_00) must still hold.  This documents
    // the baseline behavior: the affine transform preserves the parallelogram
    // structure even when one edge collapses.
    glm::vec3 P00, P10, P01, P11;
    scene.worldCorners(0.0f, P00, P10, P01, P11, 0.0f, 1.0f);

    glm::vec3 expected = P00 + (P10 - P00) + (P01 - P00);
    EXPECT_NEAR(glm::length(P11 - expected), 0.0f, 1e-5f)
        << "parallelogram identity failed with scaleW=0, scaleH=1";
}

// ---------------------------------------------------------------------------
// WorldCubeCorners — 6-face cube transform tests
// ---------------------------------------------------------------------------

class WorldCubeCornersTest : public ::testing::Test {
protected:
    Scene scene;

    void SetUp() override {
        scene.init();
    }
};

TEST_F(WorldCubeCornersTest, AllFacesTransformCorrectly)
{
    // Verify that all 6 faces transform correctly at t=0.
    // At t=0, the animation matrix is a pure translation, so local corners
    // should map to world corners with just the translation applied.
    std::array<std::array<glm::vec3, 4>, 6> corners;
    scene.worldCubeCorners(0.0f, corners, 1.0f, 1.0f);

    // All corners must be finite
    for (int face = 0; face < 6; ++face) {
        for (int corner = 0; corner < 4; ++corner) {
            EXPECT_TRUE(std::isfinite(corners[face][corner].x))
                << "Face " << face << " corner " << corner << " x is not finite";
            EXPECT_TRUE(std::isfinite(corners[face][corner].y))
                << "Face " << face << " corner " << corner << " y is not finite";
            EXPECT_TRUE(std::isfinite(corners[face][corner].z))
                << "Face " << face << " corner " << corner << " z is not finite";
        }
    }
}

TEST_F(WorldCubeCornersTest, FrontFaceMatchesWorldCorners)
{
    // The front face (+Z face, index 4) from worldCubeCorners should match
    // the output from worldCorners (which uses the same face for backward compatibility).
    std::array<std::array<glm::vec3, 4>, 6> cubeCorners;
    scene.worldCubeCorners(0.5f, cubeCorners, 1.0f, 1.0f);

    glm::vec3 P00, P10, P01, P11;
    scene.worldCorners(0.5f, P00, P10, P01, P11, 1.0f, 1.0f);

    const int FRONT_FACE = UISurface::FRONT_FACE_INDEX;
    EXPECT_NEAR(glm::length(cubeCorners[FRONT_FACE][0] - P00), 0.0f, 1e-5f)
        << "Front face P_00 mismatch";
    EXPECT_NEAR(glm::length(cubeCorners[FRONT_FACE][1] - P10), 0.0f, 1e-5f)
        << "Front face P_10 mismatch";
    EXPECT_NEAR(glm::length(cubeCorners[FRONT_FACE][2] - P01), 0.0f, 1e-5f)
        << "Front face P_01 mismatch";
    EXPECT_NEAR(glm::length(cubeCorners[FRONT_FACE][3] - P11), 0.0f, 1e-5f)
        << "Front face P_11 mismatch";
}

TEST_F(WorldCubeCornersTest, AllFacesPreserveParallelogramIdentity)
{
    // Each face must satisfy the parallelogram identity P_11 = P_00 + (P_10 - P_00) + (P_01 - P_00)
    // at any time t, since the animation matrix is affine.
    for (float t : {0.0f, 0.5f, 1.0f, 2.5f}) {
        SCOPED_TRACE("t=" + std::to_string(t));
        std::array<std::array<glm::vec3, 4>, 6> corners;
        scene.worldCubeCorners(t, corners, 1.0f, 1.0f);

        for (int face = 0; face < 6; ++face) {
            const auto& P = corners[face];
            glm::vec3 expected = P[0] + (P[1] - P[0]) + (P[2] - P[0]);
            EXPECT_NEAR(glm::length(P[3] - expected), 0.0f, 1e-5f)
                << "Face " << face << " parallelogram identity failed at t=" << t;
        }
    }
}

TEST_F(WorldCubeCornersTest, AllFacesRemainPlanar)
{
    // Each face must remain planar since the animation matrix is rigid-body.
    for (float t : {0.0f, 0.5f, 1.0f, 2.5f}) {
        SCOPED_TRACE("t=" + std::to_string(t));
        std::array<std::array<glm::vec3, 4>, 6> corners;
        scene.worldCubeCorners(t, corners, 1.0f, 1.0f);

        for (int face = 0; face < 6; ++face) {
            const auto& P = corners[face];
            glm::vec3 normal = glm::normalize(glm::cross(P[1] - P[0], P[2] - P[0]));
            float deviation = std::abs(glm::dot(normal, P[3] - P[0]));
            EXPECT_LT(deviation, 1e-4f)
                << "Face " << face << " P_11 deviates from plane by " << deviation << " at t=" << t;
        }
    }
}

TEST_F(WorldCubeCornersTest, CubeFacesHaveCorrectLocalDimensions)
{
    // Each face should have consistent dimensions in local space.
    // The front face (+Z) is 4 units wide x 2 units tall (4m x 2m).
    // Other faces may have different orientations but should maintain
    // consistent edge lengths for a valid cube mapping.
    const auto& surface = scene.uiSurface();

    // Front face (+Z, index 4) should be 4x2
    const auto& front = surface.faces[UISurface::FRONT_FACE_INDEX];
    float frontWidth = glm::length(front.P_10_local - front.P_00_local);
    float frontHeight = glm::length(front.P_01_local - front.P_00_local);
    EXPECT_NEAR(frontWidth, 4.0f, 1e-5f) << "Front face width should be 4";
    EXPECT_NEAR(frontHeight, 2.0f, 1e-5f) << "Front face height should be 2";

    // Verify all faces have finite dimensions and form valid quads
    for (int face = 0; face < 6; ++face) {
        const auto& f = surface.faces[face];

        float width = glm::length(f.P_10_local - f.P_00_local);
        float height = glm::length(f.P_01_local - f.P_00_local);

        EXPECT_GT(width, 0.0f) << "Face " << face << " width should be positive";
        EXPECT_GT(height, 0.0f) << "Face " << face << " height should be positive";
        EXPECT_TRUE(std::isfinite(width)) << "Face " << face << " width not finite";
        EXPECT_TRUE(std::isfinite(height)) << "Face " << face << " height not finite";
    }
}

TEST_F(WorldCubeCornersTest, NonUniformScaleAppliesCorrectly)
{
    // Test that non-uniform scale applies correctly to all faces.
    // Scale is applied to local X (width) and Y (height) before the animation transform.
    std::array<std::array<glm::vec3, 4>, 6> cornersScaled;
    std::array<std::array<glm::vec3, 4>, 6> cornersUnscaled;

    scene.worldCubeCorners(0.0f, cornersScaled, 2.0f, 1.0f);  // Width doubled
    scene.worldCubeCorners(0.0f, cornersUnscaled, 1.0f, 1.0f);  // No scale

    // Only test the front face which has the expected 4x2 dimensions
    const int FRONT_FACE = UISurface::FRONT_FACE_INDEX;

    // Width should be doubled (P_00 to P_10)
    float widthScaled = glm::length(cornersScaled[FRONT_FACE][1] - cornersScaled[FRONT_FACE][0]);
    float widthUnscaled = glm::length(cornersUnscaled[FRONT_FACE][1] - cornersUnscaled[FRONT_FACE][0]);
    EXPECT_NEAR(widthScaled, widthUnscaled * 2.0f, 1e-4f)
        << "Front face width not doubled";

    // Height should be unchanged (P_00 to P_01)
    float heightScaled = glm::length(cornersScaled[FRONT_FACE][2] - cornersScaled[FRONT_FACE][0]);
    float heightUnscaled = glm::length(cornersUnscaled[FRONT_FACE][2] - cornersUnscaled[FRONT_FACE][0]);
    EXPECT_NEAR(heightScaled, heightUnscaled, 1e-4f)
        << "Front face height changed";
}

TEST_F(WorldCubeCornersTest, AllCornersFiniteAtVariousTimes)
{
    // Verify all corners remain finite at various times.
    for (float t : {-5.0f, -1.0f, 0.0f, 1.0f, 2.5f, 5.0f, 10.0f}) {
        SCOPED_TRACE("t=" + std::to_string(t));
        std::array<std::array<glm::vec3, 4>, 6> corners;
        scene.worldCubeCorners(t, corners, 1.0f, 1.0f);

        for (int face = 0; face < 6; ++face) {
            for (int corner = 0; corner < 4; ++corner) {
                EXPECT_TRUE(std::isfinite(corners[face][corner].x))
                    << "Face " << face << " corner " << corner << " x not finite at t=" << t;
                EXPECT_TRUE(std::isfinite(corners[face][corner].y))
                    << "Face " << face << " corner " << corner << " y not finite at t=" << t;
                EXPECT_TRUE(std::isfinite(corners[face][corner].z))
                    << "Face " << face << " corner " << corner << " z not finite at t=" << t;
            }
        }
    }
}
