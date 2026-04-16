#include <gtest/gtest.h>
#include "sdf_render_fixture.h"
#include "renderer.h"
#include "scene.h"
#include "ui_surface.h"
#include "ui_system.h"
#include "vk_utils.h"
#include "render_helpers.h"

#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>
#include <cmath>
#include <array>

#include "scene_ubo_helper.h"

// ---------------------------------------------------------------------------
// TraditionalLightingTest — validates that composite.frag (traditional mode)
// applies the same world lighting model as surface.frag (direct mode).
//
// This test uses PRODUCTION shaders (no UI_TEST_COLOR) so composite.frag is
// exercised with real lighting, not the bypass path.  It is compiled as part
// of tests_sdf which links against production shaders.
//
// Bug: composite.frag did not apply NdotL or spotlight-cone attenuation.
// Faces facing away from the light appeared identically bright to faces
// facing toward it, because shadow*lightColor was added unconditionally.
//
// After the fix both modes share the same world lighting model:
//   ambient + shadow * NdotL * spotFactor * lightColor
//
// Setup:
//   Spotlight at (0, 2.8, 0.5) pointing toward (0, -1.3, -3.5).
//   A horizontal quad at y=1.0, z=-2.0 is inside the spotlight cone.
//
//   +Y normal (facing the light): NdotL > 0 → significantly lit
//   -Y normal (facing away):      NdotL = 0 → ambient only
// ---------------------------------------------------------------------------

class TraditionalLightingTest : public SDFRenderFixture {
protected:
    VkImage       atlasImg{VK_NULL_HANDLE};
    VmaAllocation atlasAlloc{};
    VkImageView   atlasView{VK_NULL_HANDLE};
    VkSampler     atlasSampler{VK_NULL_HANDLE};

    void SetUp() override {
        // Production shaders: renderer.init with no shader dir override.
        ASSERT_TRUE(renderer.init(/*headless=*/true))
            << "Headless renderer init failed";
        scene.init();
        ASSERT_TRUE(renderer.uploadSceneGeometry(scene));

        // Fully-transparent 1x1 atlas using shared helper.
        render_helpers::createDummyAtlas(renderer.getDevice(), renderer.getAllocator(),
                                         renderer.getCommandPool(), renderer.getGraphicsQueue(),
                                         atlasImg, atlasAlloc, atlasView, atlasSampler);
        renderer.bindAtlasDescriptor(atlasView, atlasSampler);

        ASSERT_TRUE(renderer.initOffscreenRT());

        // Full-canvas UI quad using shared helper.
        render_helpers::createUIVertexBuffer(renderer.getAllocator(), uiVtxBuf, uiVtxAlloc);

        ASSERT_TRUE(renderer.createHeadlessRT(FB_WIDTH, FB_HEIGHT, hrt));
    }

    void TearDown() override {
        if (renderer.getDevice() != VK_NULL_HANDLE)
            vkDeviceWaitIdle(renderer.getDevice());
        render_helpers::destroyAtlas(renderer.getDevice(), renderer.getAllocator(),
                                     atlasImg, atlasAlloc, atlasView, atlasSampler);
        cleanupBase();
    }

    // Render one frame in TRADITIONAL mode and return a flat RGBA pixel buffer.
    // uiOrtho = identity (default) → UI verts render off-screen → RT stays transparent
    // → composite.frag outputs teal * lit, letting us measure surface brightness.
    std::vector<uint8_t> renderTraditional(const glm::mat4& uiOrtho = glm::mat4(1.0f))
    {
        return render_helpers::renderAndReadback(
            renderer, hrt, uiVtxBuf, UI_VTX_COUNT, /*directMode=*/false, uiOrtho);
    }

    // Render the cube surface in traditional mode; return mean RGB brightness
    // of the centre pixel.  The RT is transparent so the output is teal * lit.
    uint8_t renderFaces(
        const std::array<std::array<glm::vec3, 4>, 6>& faceCorners,
        const glm::mat4& view, const glm::mat4& proj)
    {
        SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
        renderer.updateSceneUBO(sceneUBO);

        renderer.updateCubeSurface(faceCorners);

        // Shadow cube at high altitude so it doesn't cast shadows on the test quad.
        std::array<std::array<glm::vec3, 4>, 6> shadowCorners;
        for (auto& f : shadowCorners)
            f[0] = f[1] = f[2] = f[3] = glm::vec3(0.0f, 10.0f, 0.0f);
        renderer.updateUIShadowCube(shadowCorners);

        // SurfaceUBO for per-face direct-mode data (must be valid even in trad mode).
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

        auto pixels = renderTraditional();

        const uint8_t* px = pixels.data() +
            (static_cast<size_t>(FB_HEIGHT / 2) * FB_WIDTH +
             FB_WIDTH / 2) * 4;
        return static_cast<uint8_t>(
            (static_cast<uint32_t>(px[0]) + px[1] + px[2]) / 3);
    }

    // Render the cube surface in direct mode (no UI); return mean RGB brightness
    // of the centre pixel.  The output is teal * lit from surface.frag.
    uint8_t renderFacesDirect(
        const std::array<std::array<glm::vec3, 4>, 6>& faceCorners,
        const glm::mat4& view, const glm::mat4& proj)
    {
        SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
        renderer.updateSceneUBO(sceneUBO);

        renderer.updateCubeSurface(faceCorners);

        std::array<std::array<glm::vec3, 4>, 6> shadowCorners;
        for (auto& f : shadowCorners)
            f[0] = f[1] = f[2] = f[3] = glm::vec3(0.0f, 10.0f, 0.0f);
        renderer.updateUIShadowCube(shadowCorners);

        // SurfaceUBO placed off-screen so the UI draw (0 verts) is irrelevant.
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

        auto pixels = render_helpers::renderAndReadback(
            renderer, hrt, VK_NULL_HANDLE, 0, /*directMode=*/true);

        const uint8_t* px = pixels.data() +
            (static_cast<size_t>(FB_HEIGHT / 2) * FB_WIDTH + FB_WIDTH / 2) * 4;
        return static_cast<uint8_t>(
            (static_cast<uint32_t>(px[0]) + px[1] + px[2]) / 3);
    }

};

// ---------------------------------------------------------------------------
// Test 1: Traditional mode — upward-facing surface receives diffuse light
//
// A horizontal +Y surface inside the spotlight cone should be noticeably
// brighter than ambient-only.
//
// FAILS without fix: composite.frag ignores NdotL — shadow factor alone adds
// the full light colour regardless of face orientation, but the magnitude may
// still exceed ambient.  Paired with Tests 2 and 3 to be conclusive.
// ---------------------------------------------------------------------------
TEST_F(TraditionalLightingTest, UpwardFacingSurface_ReceivesDiffuseLight_Traditional)
{
    auto view  = render_helpers::makeTopView();
    auto proj  = render_helpers::makeProj();

    uint8_t brightness = renderFaces(render_helpers::makeHorizontalFaces(1.0f, +1.0f), view, proj);

    // Teal (0, 0.5, 0.5) × ambient (0.08, 0.08, 0.12): avg ≈ 8/255.
    // With NdotL ≈ 0.584 and spotlight inside cone, expect > 30/255.
    EXPECT_GT(brightness, 30)
        << "Traditional mode: upward-facing surface should receive diffuse light "
           "from the spotlight. Expected > 30/255, got "
        << static_cast<int>(brightness);
}

// ---------------------------------------------------------------------------
// Test 2: Traditional mode — downward-facing surface receives only ambient
//
// A horizontal -Y surface has NdotL = 0 with the spotlight above; it should
// show only ambient lighting.
//
// FAILS without fix: composite.frag adds shadow * lightColor unconditionally.
// For a -Y face inside the light frustum, shadow ≈ 1.0, so lit = ambient +
// lightColor → brightness >> ambient.  With the fix (NdotL applied), NdotL =
// 0 clamps the diffuse term and brightness stays near ambient.
// ---------------------------------------------------------------------------
TEST_F(TraditionalLightingTest, DownwardFacingSurface_OnlyAmbient_Traditional)
{
    auto view  = render_helpers::makeTopView();
    auto proj  = render_helpers::makeProj();

    uint8_t brightness = renderFaces(render_helpers::makeHorizontalFaces(1.0f, -1.0f), view, proj);

    // With NdotL = 0, only ambient light reaches the surface.
    // Teal × ambient ≈ avg(0, 0.04, 0.06) ≈ 8/255.
    // Allow up to 30/255 for shadow-map noise and PCF blending.
    EXPECT_LT(brightness, 30)
        << "Traditional mode: downward-facing surface should receive only ambient "
           "lighting (NdotL = 0). Expected < 30/255, got "
        << static_cast<int>(brightness)
        << ". Indicates composite.frag does not apply NdotL.";
}

// ---------------------------------------------------------------------------
// Test 3: Traditional mode — upward face significantly brighter than downward
//
// Primary regression guard.  Without NdotL both faces are equally (brightly)
// lit and the difference is near zero; the test fails.
// ---------------------------------------------------------------------------
TEST_F(TraditionalLightingTest, NdotL_TopFaceBrighterThanBottomFace_Traditional)
{
    auto view = render_helpers::makeTopView();
    auto proj = render_helpers::makeProj();

    uint8_t topBrightness = renderFaces(render_helpers::makeHorizontalFaces(1.0f, +1.0f), view, proj);
    uint8_t botBrightness = renderFaces(render_helpers::makeHorizontalFaces(1.0f, -1.0f), view, proj);

    EXPECT_GT(topBrightness, botBrightness + 20)
        << "Traditional mode: upward face (" << static_cast<int>(topBrightness)
        << ") should be significantly brighter than downward face ("
        << static_cast<int>(botBrightness)
        << "). Difference must exceed 20/255. "
           "Equal brightness indicates NdotL is not applied in composite.frag.";
}

// ---------------------------------------------------------------------------
// Test 4: Direct-vs-traditional lighting parity
//
// The core claim: both rendering modes produce identical lighting for the same
// surface with the same surfaceNormal/vertex normal.
//
// surface.frag (direct):    outColor = vec4(teal * lit, 1.0)
// composite.frag (trad, no UI): base = teal*(1-0)+0*0 = teal → vec4(teal*lit, 1.0)
//
// Center-pixel brightness must be within 5/255 between the two modes.
// A large difference would indicate a lighting model mismatch between shaders.
// ---------------------------------------------------------------------------
TEST_F(TraditionalLightingTest, DirectVsTraditional_LightingParity_SameSurface)
{
    auto view  = render_helpers::makeTopView();
    auto proj  = render_helpers::makeProj();
    auto faces = render_helpers::makeHorizontalFaces(1.0f, +1.0f);

    uint8_t brightnessTrad   = renderFaces(faces, view, proj);
    uint8_t brightnessDirect = renderFacesDirect(faces, view, proj);

    EXPECT_NEAR(static_cast<int>(brightnessDirect), static_cast<int>(brightnessTrad), 5)
        << "Direct mode brightness (" << static_cast<int>(brightnessDirect)
        << ") differs from traditional mode (" << static_cast<int>(brightnessTrad)
        << ") by more than 5/255. Both modes should produce identical lighting "
           "for the same surface normal and spotlight configuration.";
}
