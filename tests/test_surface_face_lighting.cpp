#include "containment_fixture.h"
#include "ui_surface.h"
#include <cmath>
#include <array>

// ---------------------------------------------------------------------------
// SurfaceFaceLightingTest — validates that surface.frag applies per-face
// NdotL and spotlight cone attenuation using the normal stored in QuadVertex.
//
// The spotlight is at (0, 2.8, 0.5) pointing toward (0, -1.3, -3.5).
// A horizontal quad at y=1.0, z=-2.0 is placed directly in the spotlight's
// cone.  Depending on face winding (normal +Y vs -Y):
//
//   +Y normal (upward):  NdotL ≈ 0.584, spotFactor = 1.0  → significantly lit
//   -Y normal (downward): NdotL = 0,     spotFactor = 1.0  → ambient only
//
// The teal surface colour is (0, 0.5, 0.5).  Expected pixel brightness:
//   +Y: avg(0, 0.317, 0.308) ≈ 0.208  → ~53 / 255
//   -Y: avg(0, 0.040, 0.060) ≈ 0.033  → ~8  / 255
//
// Tests that FAIL without implementing NdotL in surface.frag:
//   - Both faces would be identically lit (ambient only), so the comparison
//     test would fail.
// ---------------------------------------------------------------------------

class SurfaceFaceLightingTest : public ContainmentTest {
protected:
    // Render the cube surface with the given face corners and return the average
    // R+G+B brightness at the centre pixel.
    //
    // The SurfaceUBO is set to a surface far off-screen (-50 Z) so the UI direct
    // geometry is clipped and does not pollute the brightness measurement.
    // The shadow cube is placed at high altitude so it does not cast shadows on
    // the test surface.
    uint8_t renderFaces(const std::array<std::array<glm::vec3, 4>, 6>& faceCorners,
                        const glm::mat4& view, const glm::mat4& proj)
    {
        SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
        renderer.updateSceneUBO(sceneUBO);

        renderer.updateCubeSurface(faceCorners);

        // Shadow cube at high altitude — does not cast shadows on the test surface.
        std::array<std::array<glm::vec3, 4>, 6> shadowCorners;
        for (auto& f : shadowCorners)
            f[0] = f[1] = f[2] = f[3] = glm::vec3(0.0f, 10.0f, 0.0f);
        renderer.updateUIShadowCube(shadowCorners);

        // UI surface off-screen (far -Z) so direct-UI geometry is clipped.
        glm::vec3 P00{-0.5f,  0.5f, -50.0f};
        glm::vec3 P10{ 0.5f,  0.5f, -50.0f};
        glm::vec3 P01{-0.5f, -0.5f, -50.0f};
        glm::mat4 vp = proj * view;
        auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                                   static_cast<float>(W_UI),
                                                   static_cast<float>(H_UI), vp);
        auto clipPlanes = computeClipPlanes(P00, P10, P01);
        SurfaceUBO surfaceUBO{};
        surfaceUBO.totalMatrix = transforms.M_total;
        surfaceUBO.worldMatrix = transforms.M_world;
        for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
        surfaceUBO.depthBias = Renderer::DEPTH_BIAS_DEFAULT;
        renderer.updateSurfaceUBO(surfaceUBO);

        auto pixels = renderAndReadback(/*directMode=*/true);

        const uint8_t* px = pixels.data() +
                            (static_cast<size_t>(FB_HEIGHT / 2) * FB_WIDTH + FB_WIDTH / 2) * 4;
        return static_cast<uint8_t>(
            (static_cast<uint32_t>(px[0]) + px[1] + px[2]) / 3);
    }

};

// ---------------------------------------------------------------------------
// Test 1: UpwardFacing_ReceivesDiffuseLight
//
// A horizontal surface with normal +Y (facing the spotlight above) should be
// noticeably brighter than ambient-only.
//
// This test fails if surface.frag ignores the face normal (NdotL = constant)
// because the upward surface would then match the downward surface brightness.
// ---------------------------------------------------------------------------
TEST_F(SurfaceFaceLightingTest, UpwardFacingSurface_ReceivesDiffuseLight)
{
    auto view  = render_helpers::makeTopView();
    auto proj  = render_helpers::makeProj();

    auto faces = render_helpers::makeHorizontalFaces(1.0f, +1.0f);  // +Y normal
    uint8_t brightness = renderFaces(faces, view, proj);

    // Ambient-only brightness for teal (0, 0.5, 0.5) × ambient (0.08, 0.08, 0.12):
    //   avg(0, 0.04, 0.06) ≈ 8 / 255.
    // With NdotL ≈ 0.584 and full spotlight, expect > 30 / 255.
    EXPECT_GT(brightness, 30)
        << "Upward-facing surface (normal +Y) should be significantly lit by the "
           "spotlight. Expected brightness > 30/255, got "
        << static_cast<int>(brightness)
        << ". This indicates NdotL is not being applied in surface.frag.";
}

// ---------------------------------------------------------------------------
// Test 2: DownwardFacing_OnlyAmbient
//
// A horizontal surface with normal -Y (facing away from the spotlight above)
// receives NdotL = 0 and should show only ambient lighting.
//
// This test fails if surface.frag does not apply NdotL (the downward surface
// would incorrectly appear as bright as the upward surface).
// ---------------------------------------------------------------------------
TEST_F(SurfaceFaceLightingTest, DownwardFacingSurface_OnlyAmbient)
{
    auto view  = render_helpers::makeTopView();
    auto proj  = render_helpers::makeProj();

    auto faces = render_helpers::makeHorizontalFaces(1.0f, -1.0f);  // -Y normal
    uint8_t brightness = renderFaces(faces, view, proj);

    // With NdotL = 0 (facing away) the surface shows ambient only.
    // Teal × ambient ≈ avg(0, 0.04, 0.06) ≈ 8/255.
    // Allow up to 30/255 for shadow map sampling noise and PCF blending.
    EXPECT_LT(brightness, 30)
        << "Downward-facing surface (normal -Y) should receive only ambient "
           "lighting (NdotL = 0). Expected brightness < 30/255, got "
        << static_cast<int>(brightness)
        << ". This indicates NdotL is not being applied in surface.frag.";
}

// ---------------------------------------------------------------------------
// Test 3: NdotL_TopBrighterThanBottom
//
// The upward-facing surface (+Y) must be significantly brighter than the
// downward-facing surface (-Y) when both are in the same spotlight position.
//
// Expected difference: at least 20 brightness units out of 255.
// This is the primary regression test — it fails whenever NdotL is absent
// because both surfaces would be identically (ambient-only) lit.
// ---------------------------------------------------------------------------
TEST_F(SurfaceFaceLightingTest, NdotL_TopFaceBrighterThanBottomFace)
{
    auto view = render_helpers::makeTopView();
    auto proj = render_helpers::makeProj();

    uint8_t topBrightness = renderFaces(render_helpers::makeHorizontalFaces(1.0f, +1.0f), view, proj);
    uint8_t botBrightness = renderFaces(render_helpers::makeHorizontalFaces(1.0f, -1.0f), view, proj);

    EXPECT_GT(topBrightness, botBrightness + 20)
        << "Upward face (" << static_cast<int>(topBrightness)
        << ") should be significantly brighter than downward face ("
        << static_cast<int>(botBrightness)
        << "). Difference must exceed 20/255. "
           "If they are equal, NdotL is not applied in surface.frag.";
}
