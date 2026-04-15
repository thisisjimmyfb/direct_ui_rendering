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

// ---------------------------------------------------------------------------
// ContainmentTest fixture — shared GPU resources across all containment tests.
// Each test configures its own camera and surface, then calls renderAndCheck().
// ---------------------------------------------------------------------------

class ContainmentTest : public ::testing::Test {
protected:
    static constexpr uint32_t FB_WIDTH  = 1280;
    static constexpr uint32_t FB_HEIGHT = 720;

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
        VkBuffer      readbackBuf   = VK_NULL_HANDLE;
        VmaAllocation readbackAlloc = VK_NULL_HANDLE;
        const VkDeviceSize readbackSize = static_cast<VkDeviceSize>(FB_WIDTH) * FB_HEIGHT * 4;
        {
            VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bci.size        = readbackSize;
            bci.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_CPU_ONLY;
            vmaCreateBuffer(renderer.getAllocator(), &bci, &ai,
                            &readbackBuf, &readbackAlloc, nullptr);
        }

        VkCommandBufferAllocateInfo cbAI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cbAI.commandPool        = renderer.getCommandPool();
        cbAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbAI.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(renderer.getDevice(), &cbAI, &cmd);

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        renderer.recordShadowPass(cmd);
        if (!directMode) {
            renderer.recordUIRTPass(cmd, uiVtxBuf, UI_VTX_COUNT, uiOrtho);
        }
        renderer.recordMainPass(cmd, hrt.rt, directMode, uiVtxBuf, UI_VTX_COUNT);

        vku::imageBarrier(cmd, hrt.rt.image,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkBufferImageCopy copyRegion{};
        copyRegion.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.imageExtent       = {FB_WIDTH, FB_HEIGHT, 1};
        vkCmdCopyImageToBuffer(cmd, hrt.rt.image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readbackBuf, 1, &copyRegion);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &cmd;
        vkQueueSubmit(renderer.getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(renderer.getGraphicsQueue());

        void* mapped = nullptr;
        vmaMapMemory(renderer.getAllocator(), readbackAlloc, &mapped);
        std::vector<uint8_t> pixels(readbackSize);
        memcpy(pixels.data(), mapped, static_cast<size_t>(readbackSize));
        vmaUnmapMemory(renderer.getAllocator(), readbackAlloc);

        vkFreeCommandBuffers(renderer.getDevice(), renderer.getCommandPool(), 1, &cmd);
        vmaDestroyBuffer(renderer.getAllocator(), readbackBuf, readbackAlloc);

        return pixels;
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
                const uint8_t* px = pixels.data() + (y * FB_WIDTH + x) * 4;
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
        const uint8_t* px = pixels.data() + (y * FB_WIDTH + x) * 4;
        uint32_t sum = static_cast<uint32_t>(px[0]) + px[1] + px[2];
        return static_cast<uint8_t>(sum / 3);
    }
};
