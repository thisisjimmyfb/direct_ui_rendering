// test_direct_ui_normal.cpp
//
// Verifies that the surfaceNormal field in SurfaceUBO drives NdotL lighting for
// direct-mode UI drawcalls (ui_direct.frag).
//
// Setup:
//   - Horizontal surface at y=1, z=[-1.5,-2.5], x=[-1,1] — inside the spotlight cone.
//   - Fully-opaque white atlas (all pixels = 255,255,255,255) in bitmap mode
//     (sdfThreshold=0) so: outColor = vec4(lit, 1.0).
//   - Alpha=1 means the UI text completely overwrites the teal cube surface below.
//   - Center pixel brightness therefore reflects the lighting of the UI text alone.
//
// Spotlight geometry at the test surface centre (0,1,-2):
//   L          = normalize((0,2.8,0.5) - (0,1,-2))  ≈ (0, 0.584, 0.812)
//   spotFactor = smoothstep(outer, inner, cosAngle)  = 1.0 (fully inside cone)
//   shadow     = 1.0 (shadow cube placed at y=10)
//
// Expected brightness (avg RGB, 0-255):
//   +Y normal: NdotL ≈ 0.584, diffuse large → brightness ≈ 163/255
//   -Y normal: NdotL = 0,     ambient only   → brightness ≈ 8/255
//
// Failure mode: if surfaceNormal is zero (not set) or ignored by the shader,
//   normalize(vec3(0)) is undefined and both normals produce the same brightness.

#include <gtest/gtest.h>
#include "sdf_render_fixture.h"
#include "renderer.h"
#include "scene.h"
#include "ui_surface.h"
#include "scene_ubo_helper.h"
#include "render_helpers.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>

class DirectUINormalTest : public SDFRenderFixture {
protected:
    VkImage       atlasImg{VK_NULL_HANDLE};
    VmaAllocation atlasAlloc{};
    VkImageView   atlasView{VK_NULL_HANDLE};
    VkSampler     atlasSampler{VK_NULL_HANDLE};

    glm::mat4 m_view{};
    glm::mat4 m_proj{};

    void SetUp() override {
        // Horizontal test surface at y=1, inside the spotlight cone.
        m_P00 = glm::vec3(-1.0f, 1.0f, -1.5f);
        m_P10 = glm::vec3( 1.0f, 1.0f, -1.5f);
        m_P01 = glm::vec3(-1.0f, 1.0f, -2.5f);
        m_P11 = glm::vec3( 1.0f, 1.0f, -2.5f);

        ASSERT_TRUE(renderer.init(/*headless=*/true))
            << "Headless renderer init failed";
        scene.init();
        ASSERT_TRUE(renderer.uploadSceneGeometry(scene));

        m_view = render_helpers::makeTopView();
        m_proj = render_helpers::makeProj();

        // White atlas: all pixels = (255,255,255,255) — fully opaque white, linear filter.
        render_helpers::createAtlas(renderer.getDevice(), renderer.getAllocator(),
                                     renderer.getCommandPool(), renderer.getGraphicsQueue(),
                                     ATLAS_DIM, 255, 255, 255, 255,
                                     atlasImg, atlasAlloc, atlasView, atlasSampler,
                                     VK_FILTER_LINEAR);
        renderer.bindAtlasDescriptor(atlasView, atlasSampler);

        // Full-canvas UI quad in UI space [(0,0)..(W_UI,H_UI)].
        render_helpers::createUIVertexBuffer(renderer.getAllocator(), uiVtxBuf, uiVtxAlloc);

        ASSERT_TRUE(renderer.createHeadlessRT(FB_WIDTH, FB_HEIGHT, hrt));
        ASSERT_TRUE(renderer.initOffscreenRT());
    }

    void TearDown() override {
        if (renderer.getDevice() != VK_NULL_HANDLE)
            vkDeviceWaitIdle(renderer.getDevice());
        render_helpers::destroyAtlas(renderer.getDevice(), renderer.getAllocator(),
                                      atlasImg, atlasAlloc, atlasView, atlasSampler);
        cleanupBase();
    }

    // Render one direct-mode frame with the given surfaceNormal in SurfaceUBO.
    // Returns average RGB brightness (0–255) of the centre pixel.
    uint8_t renderCenterBrightness(glm::vec3 normal) {
        SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, m_view, m_proj);
        sceneUBO.lightIntensity = 1.0f;
        renderer.updateSceneUBO(sceneUBO);

        std::array<std::array<glm::vec3, 4>, 6> faceCorners;
        for (auto& f : faceCorners)
            f = {m_P00, m_P10, m_P01, m_P11};
        renderer.updateCubeSurface(faceCorners);

        std::array<std::array<glm::vec3, 4>, 6> shadowCorners;
        for (auto& f : shadowCorners)
            f[0] = f[1] = f[2] = f[3] = glm::vec3(0.0f, 10.0f, 0.0f);
        renderer.updateUIShadowCube(shadowCorners);

        auto transforms = computeSurfaceTransforms(m_P00, m_P10, m_P01,
                                                   static_cast<float>(W_UI),
                                                   static_cast<float>(H_UI),
                                                   m_proj * m_view);
        auto clipPlanes = computeClipPlanes(m_P00, m_P10, m_P01);
        SurfaceUBO surfaceUBO{};
        surfaceUBO.totalMatrix   = transforms.M_total;
        surfaceUBO.worldMatrix   = transforms.M_world;
        for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
        surfaceUBO.depthBias     = Renderer::DEPTH_BIAS_DEFAULT;
        surfaceUBO.surfaceNormal = glm::vec4(normal, 0.0f);
        renderer.updateSurfaceUBO(surfaceUBO);

        auto pixels = render_helpers::renderAndReadback(
            renderer, hrt, uiVtxBuf, UI_VTX_COUNT, /*directMode=*/true);

        const size_t ci = (static_cast<size_t>(FB_HEIGHT / 2) * FB_WIDTH + FB_WIDTH / 2) * 4;
        return static_cast<uint8_t>(
            (static_cast<uint32_t>(pixels[ci]) + pixels[ci + 1] + pixels[ci + 2]) / 3);
    }
};

// ---------------------------------------------------------------------------
// Test 1: Upward-facing normal receives diffuse lighting from the spotlight.
// ---------------------------------------------------------------------------
TEST_F(DirectUINormalTest, UpwardNormal_ReceivesDiffuseLighting)
{
    uint8_t brightness = renderCenterBrightness(glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_GT(brightness, 50)
        << "Direct UI with +Y surfaceNormal should receive diffuse lighting from "
           "the spotlight above. Got " << static_cast<int>(brightness) << "/255 "
           "(expected > 50). If surfaceNormal in SurfaceUBO is zero or ignored, "
           "NdotL is undefined and this test fails.";
}

// ---------------------------------------------------------------------------
// Test 2: Downward-facing normal receives ambient lighting only.
// ---------------------------------------------------------------------------
TEST_F(DirectUINormalTest, DownwardNormal_AmbientOnly)
{
    uint8_t brightness = renderCenterBrightness(glm::vec3(0.0f, -1.0f, 0.0f));
    EXPECT_LT(brightness, 30)
        << "Direct UI with -Y surfaceNormal (NdotL=0) should show only ambient "
           "lighting (~8/255). Got " << static_cast<int>(brightness) << "/255 "
           "(expected < 30). If surfaceNormal is always positive, diffuse is "
           "incorrectly applied to back-facing UI geometry.";
}

// ---------------------------------------------------------------------------
// Test 3: Upward-facing must be significantly brighter than downward-facing.
// ---------------------------------------------------------------------------
TEST_F(DirectUINormalTest, NdotL_UpwardBrighterThanDownward)
{
    uint8_t brightUp = renderCenterBrightness(glm::vec3(0.0f,  1.0f, 0.0f));
    uint8_t brightDn = renderCenterBrightness(glm::vec3(0.0f, -1.0f, 0.0f));

    EXPECT_GT(brightUp, brightDn + 50)
        << "Direct UI +Y surfaceNormal (" << static_cast<int>(brightUp)
        << "/255) should be significantly brighter than -Y surfaceNormal ("
        << static_cast<int>(brightDn) << "/255). Difference must exceed 50/255. "
        << "If equal, surfaceNormal in SurfaceUBO is not driving NdotL in "
           "ui_direct.frag.";
}
