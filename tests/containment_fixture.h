#pragma once

#include <gtest/gtest.h>
#include "renderer.h"
#include "scene.h"
#include "ui_surface.h"
#include "ui_system.h"
#include "vk_utils.h"
#include <array>

#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Project a world-space point through viewProj to screen-space pixel coords.
static glm::vec2 projectToScreen(glm::vec3 worldPos,
                                 const glm::mat4& viewProj,
                                 uint32_t width, uint32_t height)
{
    glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
    glm::vec3 ndc  = glm::vec3(clip) / clip.w;
    return {
        (ndc.x * 0.5f + 0.5f) * static_cast<float>(width),
        (ndc.y * 0.5f + 0.5f) * static_cast<float>(height)
    };
}

// Test if a pixel coordinate is inside a convex screen-space quad.
// quad[0..3] are the four screen-space corners in order (e.g. TL, TR, BR, BL).
// margin: allow N pixels outside the quad boundary.
static bool insideConvexQuad(glm::vec2 p,
                             const glm::vec2 quad[4],
                             float margin = 2.0f)
{
    for (int i = 0; i < 4; ++i) {
        glm::vec2 a    = quad[i];
        glm::vec2 b    = quad[(i + 1) % 4];
        glm::vec2 edge = b - a;
        float len = glm::length(edge);
        if (len < 1e-6f) continue;  // degenerate edge
        // Normalised inward normal (CCW winding in screen space).
        // Dividing by len gives d in units of pixels, so margin is truly in pixels.
        glm::vec2 perp = glm::vec2{-edge.y, edge.x} / len;
        float d = glm::dot(p - a, perp);
        if (d < -margin) return false;
    }
    return true;
}

// Check if a pixel (r,g,b) is "magenta-like" within a tolerance.
static bool isMagenta(uint8_t r, uint8_t g, uint8_t b, uint8_t threshold = 32)
{
    return r > (255 - threshold) && g < threshold && b > (255 - threshold);
}

// Axis-aligned bounding box of all magenta pixels in a readback buffer.
struct MagentaBBox {
    int minX{INT_MAX}, minY{INT_MAX};
    int maxX{INT_MIN}, maxY{INT_MIN};
    bool valid{false};
};

static MagentaBBox computeMagentaBBox(const std::vector<uint8_t>& pixels,
                                      uint32_t width, uint32_t height)
{
    MagentaBBox bb;
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* px = pixels.data() + (y * width + x) * 4;
            if (isMagenta(px[0], px[1], px[2])) {
                bb.valid = true;
                bb.minX  = std::min(bb.minX, (int)x);
                bb.minY  = std::min(bb.minY, (int)y);
                bb.maxX  = std::max(bb.maxX, (int)x);
                bb.maxY  = std::max(bb.maxY, (int)y);
            }
        }
    return bb;
}

// ---------------------------------------------------------------------------
// ContainmentTest fixture — shared GPU resources across all containment tests.
// Each test configures its own camera and surface, then calls renderAndCheck().
// ---------------------------------------------------------------------------

class ContainmentTest : public ::testing::Test {
protected:
    static constexpr uint32_t FB_WIDTH  = 1280;
    static constexpr uint32_t FB_HEIGHT = 720;
    static constexpr uint32_t UI_VTX_COUNT = 6;

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

        // Dummy 1x1 atlas (UI_TEST_COLOR overrides sampling, but descriptor must be valid).
        {
            VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            ci.imageType     = VK_IMAGE_TYPE_2D;
            ci.format        = VK_FORMAT_R8G8B8A8_UNORM;
            ci.extent        = {1, 1, 1};
            ci.mipLevels     = 1;
            ci.arrayLayers   = 1;
            ci.samples       = VK_SAMPLE_COUNT_1_BIT;
            ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
            ci.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
            ASSERT_EQ(vmaCreateImage(renderer.getAllocator(), &ci, &ai,
                                     &dummyImg, &dummyAlloc, nullptr), VK_SUCCESS);

            VkCommandBuffer transCmd = vku::beginOneShot(renderer.getDevice(),
                                                         renderer.getCommandPool());
            vku::imageBarrier(transCmd, dummyImg,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                0, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            vku::endOneShot(renderer.getDevice(), renderer.getCommandPool(),
                            renderer.getGraphicsQueue(), transCmd);

            VkImageViewCreateInfo viewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewCI.image            = dummyImg;
            viewCI.viewType         = VK_IMAGE_VIEW_TYPE_2D;
            viewCI.format           = VK_FORMAT_R8G8B8A8_UNORM;
            viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            ASSERT_EQ(vkCreateImageView(renderer.getDevice(), &viewCI, nullptr, &dummyView),
                      VK_SUCCESS);

            VkSamplerCreateInfo sampCI{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            sampCI.magFilter    = VK_FILTER_NEAREST;
            sampCI.minFilter    = VK_FILTER_NEAREST;
            sampCI.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            ASSERT_EQ(vkCreateSampler(renderer.getDevice(), &sampCI, nullptr, &dummySampler),
                      VK_SUCCESS);

            renderer.bindAtlasDescriptor(dummyView, dummySampler);
        }

        // Offscreen RT descriptor must be valid even in direct mode (set 2 binding 1).
        ASSERT_TRUE(renderer.initOffscreenRT());

        // Full-canvas UI quad (two triangles).
        {
            const float W = static_cast<float>(Renderer::W_UI);
            const float H = static_cast<float>(Renderer::H_UI);
            UIVertex verts[UI_VTX_COUNT] = {
                {{0, 0}, {0, 0}}, {{W, 0}, {1, 0}}, {{W, H}, {1, 1}},
                {{0, 0}, {0, 0}}, {{W, H}, {1, 1}}, {{0, H}, {0, 1}},
            };
            VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bci.size        = sizeof(verts);
            bci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            ASSERT_EQ(vmaCreateBuffer(renderer.getAllocator(), &bci, &ai,
                                      &uiVtxBuf, &uiVtxAlloc, nullptr), VK_SUCCESS);
            void* mapped = nullptr;
            vmaMapMemory(renderer.getAllocator(), uiVtxAlloc, &mapped);
            memcpy(mapped, verts, sizeof(verts));
            vmaUnmapMemory(renderer.getAllocator(), uiVtxAlloc);
        }

        ASSERT_TRUE(renderer.createHeadlessRT(FB_WIDTH, FB_HEIGHT, hrt));
    }

    void TearDown() override {
        renderer.destroyHeadlessRT(hrt);
        if (uiVtxBuf != VK_NULL_HANDLE)
            vmaDestroyBuffer(renderer.getAllocator(), uiVtxBuf, uiVtxAlloc);
        if (dummySampler != VK_NULL_HANDLE)
            vkDestroySampler(renderer.getDevice(), dummySampler, nullptr);
        if (dummyView != VK_NULL_HANDLE)
            vkDestroyImageView(renderer.getDevice(), dummyView, nullptr);
        if (dummyImg != VK_NULL_HANDLE)
            vmaDestroyImage(renderer.getAllocator(), dummyImg, dummyAlloc);
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
        int count = 0;
        for (uint32_t y = 0; y < FB_HEIGHT; ++y)
            for (uint32_t x = 0; x < FB_WIDTH; ++x) {
                const uint8_t* px = pixels.data() + (y * FB_WIDTH + x) * 4;
                if (isMagenta(px[0], px[1], px[2])) ++count;
            }
        return count;
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
            projectToScreen(P00, viewProj, FB_WIDTH, FB_HEIGHT),
            projectToScreen(P10, viewProj, FB_WIDTH, FB_HEIGHT),
            projectToScreen(P11, viewProj, FB_WIDTH, FB_HEIGHT),
            projectToScreen(P01, viewProj, FB_WIDTH, FB_HEIGHT),
        };

        int violations = 0;
        for (uint32_t y = 0; y < FB_HEIGHT; ++y) {
            for (uint32_t x = 0; x < FB_WIDTH; ++x) {
                const uint8_t* px = pixels.data() + (y * FB_WIDTH + x) * 4;
                if (isMagenta(px[0], px[1], px[2])) {
                    glm::vec2 coord{static_cast<float>(x), static_cast<float>(y)};
                    if (!insideConvexQuad(coord, screenCorners, margin)) {
                        ++violations;
                    }
                }
            }
        }
        EXPECT_EQ(violations, 0)
            << violations << " magenta pixel(s) found outside the surface quad boundary";
    }
};
