#pragma once

#include <gtest/gtest.h>
#include "renderer.h"
#include "scene.h"
#include "ui_surface.h"
#include "ui_system.h"
#include "vk_utils.h"
#include "render_helpers.h"
#include <array>

#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>
#include <cmath>

#include "scene_ubo_helper.h"
#include "test_pixel_helpers.h"

// ---------------------------------------------------------------------------
// ContainmentTest fixture — shared GPU resources across all containment tests.
// Each test configures its own camera and surface, then calls renderAndCheck().
// ---------------------------------------------------------------------------

class ContainmentTest : public ::testing::Test {
protected:
    Renderer renderer;
    Scene    scene;

    // Shared GPU resources allocated once in SetUp().
    VkImage       dummyImg{VK_NULL_HANDLE};
    VmaAllocation dummyAlloc{};
    VkImageView   dummyView{VK_NULL_HANDLE};
    VkSampler     dummySampler{VK_NULL_HANDLE};

    VkBuffer      uiVtxBuf{VK_NULL_HANDLE};
    VmaAllocation uiVtxAlloc{};

    HeadlessRenderTarget hrt{};

    void SetUp() override {
        ASSERT_TRUE(renderer.init(/*headless=*/true, nullptr, TEST_SHADER_DIR))
            << "Headless renderer init failed";
        scene.init();
        ASSERT_TRUE(renderer.uploadSceneGeometry(scene));

        // Dummy 1x1 atlas using shared helper.
        render_helpers::createDummyAtlas(renderer.getDevice(), renderer.getAllocator(),
                                         renderer.getCommandPool(), renderer.getGraphicsQueue(),
                                         dummyImg, dummyAlloc, dummyView, dummySampler);
        renderer.bindAtlasDescriptor(dummyView, dummySampler);

        // Offscreen RT descriptor must be valid even in direct mode (set 2 binding 1).
        ASSERT_TRUE(renderer.initOffscreenRT());

        // Full-canvas UI quad using shared helper.
        render_helpers::createUIVertexBuffer(renderer.getAllocator(), uiVtxBuf, uiVtxAlloc);

        ASSERT_TRUE(renderer.createHeadlessRT(FB_WIDTH, FB_HEIGHT, hrt));
    }

    void TearDown() override {
        renderer.destroyHeadlessRT(hrt);
        render_helpers::destroyAtlas(renderer.getDevice(), renderer.getAllocator(),
                                     dummyImg, dummyAlloc, dummyView, dummySampler);
        render_helpers::destroyBuffer(renderer.getAllocator(), uiVtxBuf, uiVtxAlloc);
        renderer.cleanup();
    }

    // Render one frame and read back pixels.
    // directMode=true  → shadow + main(direct)
    // directMode=false → shadow + UIRTPass + main(traditional)
    // surfaceCorners must be set up correctly; caller must have called updateSceneUBO/SurfaceUBO.
    std::vector<uint8_t> renderAndReadback(bool directMode,
                                           const glm::mat4& uiOrtho = glm::mat4(1.0f))
    {
        return render_helpers::renderAndReadback(
            renderer, hrt, uiVtxBuf, UI_VTX_COUNT, directMode, uiOrtho);
    }

    // Count how many pixels in the readback image are magenta.
    int countMagentaPixels(const std::vector<uint8_t>& pixels) const {
        return render_helpers::countMagentaPixels(pixels, FB_WIDTH, FB_HEIGHT);
    }

    // Assert that all magenta pixels in a readback image lie inside the screen-space
    // projection of the given four world-space corners.
    void assertMagentaContained(const std::vector<uint8_t>& pixels,
                                 const glm::mat4& viewProj,
                                 glm::vec3 P00, glm::vec3 P10,
                                 glm::vec3 P11, glm::vec3 P01,
                                 float margin = 2.0f)
    {
        glm::vec2 screenCorners[4] = {
            render_helpers::projectToScreen(P00, viewProj, FB_WIDTH, FB_HEIGHT),
            render_helpers::projectToScreen(P10, viewProj, FB_WIDTH, FB_HEIGHT),
            render_helpers::projectToScreen(P11, viewProj, FB_WIDTH, FB_HEIGHT),
            render_helpers::projectToScreen(P01, viewProj, FB_WIDTH, FB_HEIGHT),
        };

        int violations = 0;
        for (uint32_t y = 0; y < FB_HEIGHT; ++y) {
            for (uint32_t x = 0; x < FB_WIDTH; ++x) {
                const uint8_t* px = TestPixelHelpers::samplePixel(pixels, x, y, FB_WIDTH);
                if (render_helpers::isMagenta(px[0], px[1], px[2])) {
                    glm::vec2 coord{static_cast<float>(x), static_cast<float>(y)};
                    if (!render_helpers::insideConvexQuad(coord, screenCorners, margin)) {
                        ++violations;
                    }
                }
            }
        }
        EXPECT_EQ(violations, 0)
            << violations << " magenta pixel(s) found outside the surface quad boundary";
    }

    // Render a frame with the spotlight scene UBO and a dummy off-screen surface,
    // then return the average RGB brightness of the pixel at (x, y).
    uint8_t getPixelBrightness(uint32_t x, uint32_t y,
                               const glm::mat4& view, const glm::mat4& proj,
                               bool directMode = true) {
        SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
        renderer.updateSceneUBO(sceneUBO);

        glm::vec3 P00{-0.5f,  0.5f, -5.0f};
        glm::vec3 P10{ 0.5f,  0.5f, -5.0f};
        glm::vec3 P01{-0.5f, -0.5f, -5.0f};
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

        auto pixels = renderAndReadback(directMode);
        const uint8_t* px = TestPixelHelpers::samplePixel(pixels, x, y, FB_WIDTH);
        uint32_t sum = static_cast<uint32_t>(px[0]) + px[1] + px[2];
        return static_cast<uint8_t>(sum / 3);
    }
};
