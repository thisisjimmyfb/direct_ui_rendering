#include <gtest/gtest.h>
#include "renderer.h"
#include "scene.h"
#include "ui_surface.h"
#include "ui_system.h"
#include "vk_utils.h"

#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>

// ---------------------------------------------------------------------------
// SDF threshold render test
//
// Uses production shaders (no UI_TEST_COLOR) so the actual atlas-sampling and
// SDF smoothstep logic in ui_direct.frag is exercised.
//
// Atlas design:
//   64x64 RGBA8, every pixel = (51, 51, 51, 200).
//   Normalised R = 51/255 ≈ 0.20.
//
//   sdfThreshold = 0.0  →  bitmap path:
//       outColor = texture(atlas, uv) ≈ (0.20, 0.20, 0.20, 0.78)  [visible]
//
//   sdfThreshold = 0.5  →  SDF path:
//       dist   = R = 0.20
//       spread = 0.07
//       alpha  = smoothstep(0.43, 0.57, 0.20) = 0.0              [transparent]
//       outColor = (0, 0, 0, 0)
//
// The two renders should therefore produce measurably different pixel data.
// ---------------------------------------------------------------------------

static constexpr uint32_t FB_WIDTH  = 640;
static constexpr uint32_t FB_HEIGHT = 360;
static constexpr uint32_t ATLAS_DIM = 64;  // synthetic atlas side length

class SDFThresholdTest : public ::testing::Test {
protected:
    Renderer renderer;
    Scene    scene;

    // Synthetic SDF-like atlas (R≈0.2 for every pixel, alpha≈0.78)
    VkImage       atlasImg{VK_NULL_HANDLE};
    VmaAllocation atlasAlloc{};
    VkImageView   atlasView{VK_NULL_HANDLE};
    VkSampler     atlasSampler{VK_NULL_HANDLE};

    // Full-canvas UI quad
    VkBuffer      uiVtxBuf{VK_NULL_HANDLE};
    VmaAllocation uiVtxAlloc{};
    static constexpr uint32_t UI_VTX_COUNT = 6;

    HeadlessRenderTarget hrt{};

    void SetUp() override {
        ASSERT_TRUE(renderer.init(/*headless=*/true))
            << "Headless renderer init failed";
        scene.init();
        ASSERT_TRUE(renderer.uploadSceneGeometry(scene));

        // Build synthetic atlas: all pixels (51, 51, 51, 200).
        {
            std::vector<uint8_t> pixels(ATLAS_DIM * ATLAS_DIM * 4);
            for (size_t i = 0; i < ATLAS_DIM * ATLAS_DIM; ++i) {
                pixels[i * 4 + 0] = 51;   // R ≈ 0.20
                pixels[i * 4 + 1] = 51;
                pixels[i * 4 + 2] = 51;
                pixels[i * 4 + 3] = 200;  // alpha ≈ 0.78
            }

            VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            ci.imageType     = VK_IMAGE_TYPE_2D;
            ci.format        = VK_FORMAT_R8G8B8A8_UNORM;
            ci.extent        = {ATLAS_DIM, ATLAS_DIM, 1};
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
                                     &atlasImg, &atlasAlloc, nullptr), VK_SUCCESS);

            // Upload via staging buffer.
            VkBuffer      stagingBuf;
            VmaAllocation stagingAlloc;
            {
                VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                bci.size        = pixels.size();
                bci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
                bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                VmaAllocationCreateInfo sai{};
                sai.usage = VMA_MEMORY_USAGE_CPU_ONLY;
                ASSERT_EQ(vmaCreateBuffer(renderer.getAllocator(), &bci, &sai,
                                          &stagingBuf, &stagingAlloc, nullptr), VK_SUCCESS);
                void* mapped = nullptr;
                vmaMapMemory(renderer.getAllocator(), stagingAlloc, &mapped);
                memcpy(mapped, pixels.data(), pixels.size());
                vmaUnmapMemory(renderer.getAllocator(), stagingAlloc);
            }

            VkCommandBuffer uploadCmd = vku::beginOneShot(renderer.getDevice(),
                                                          renderer.getCommandPool());
            vku::imageBarrier(uploadCmd, atlasImg,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent      = {ATLAS_DIM, ATLAS_DIM, 1};
            vkCmdCopyBufferToImage(uploadCmd, stagingBuf, atlasImg,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            vku::imageBarrier(uploadCmd, atlasImg,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            vku::endOneShot(renderer.getDevice(), renderer.getCommandPool(),
                            renderer.getGraphicsQueue(), uploadCmd);
            vmaDestroyBuffer(renderer.getAllocator(), stagingBuf, stagingAlloc);

            VkImageViewCreateInfo viewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewCI.image            = atlasImg;
            viewCI.viewType         = VK_IMAGE_VIEW_TYPE_2D;
            viewCI.format           = VK_FORMAT_R8G8B8A8_UNORM;
            viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            ASSERT_EQ(vkCreateImageView(renderer.getDevice(), &viewCI, nullptr, &atlasView),
                      VK_SUCCESS);

            VkSamplerCreateInfo sampCI{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            sampCI.magFilter    = VK_FILTER_NEAREST;
            sampCI.minFilter    = VK_FILTER_NEAREST;
            sampCI.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            ASSERT_EQ(vkCreateSampler(renderer.getDevice(), &sampCI, nullptr, &atlasSampler),
                      VK_SUCCESS);

            renderer.bindAtlasDescriptor(atlasView, atlasSampler);
        }

        // Offscreen RT descriptor must be valid (tests_sdf may exercise either mode).
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

        // Set up UBOs: camera looking straight at the surface.
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                          static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                          0.1f, 100.0f);
        proj[1][1] *= -1.0f;

        SceneUBO sceneUBO{};
        sceneUBO.view         = view;
        sceneUBO.proj         = proj;
        sceneUBO.lightViewProj = scene.lightViewProj();
        sceneUBO.lightDir     = glm::vec4(scene.light().direction, 0.0f);
        sceneUBO.lightColor   = glm::vec4(scene.light().color,     1.0f);
        sceneUBO.ambientColor = glm::vec4(scene.light().ambient,   1.0f);
        renderer.updateSceneUBO(sceneUBO);

        glm::vec3 P00{-0.5f,  0.5f, 0.0f};
        glm::vec3 P10{ 0.5f,  0.5f, 0.0f};
        glm::vec3 P01{-0.5f, -0.5f, 0.0f};

        auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                                   static_cast<float>(Renderer::W_UI),
                                                   static_cast<float>(Renderer::H_UI),
                                                   proj * view);
        auto clipPlanes = computeClipPlanes(P00, P10, P01);

        SurfaceUBO surfaceUBO{};
        surfaceUBO.totalMatrix = transforms.M_total;
        surfaceUBO.worldMatrix = transforms.M_world;
        for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
        surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
        renderer.updateSurfaceUBO(surfaceUBO);
    }

    void TearDown() override {
        if (renderer.getDevice() != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(renderer.getDevice());
        }
        renderer.destroyHeadlessRT(hrt);
        if (uiVtxBuf   != VK_NULL_HANDLE)
            vmaDestroyBuffer(renderer.getAllocator(), uiVtxBuf, uiVtxAlloc);
        if (atlasSampler != VK_NULL_HANDLE)
            vkDestroySampler(renderer.getDevice(), atlasSampler, nullptr);
        if (atlasView != VK_NULL_HANDLE)
            vkDestroyImageView(renderer.getDevice(), atlasView, nullptr);
        if (atlasImg != VK_NULL_HANDLE)
            vmaDestroyImage(renderer.getAllocator(), atlasImg, atlasAlloc);
        renderer.cleanup();
    }

    // Render one frame with the given sdfThreshold in direct mode and return
    // a flat RGBA pixel buffer of size FB_WIDTH * FB_HEIGHT * 4.
    std::vector<uint8_t> renderAndReadback(float sdfThreshold) {
        const VkDeviceSize readbackSize =
            static_cast<VkDeviceSize>(FB_WIDTH) * FB_HEIGHT * 4;

        VkBuffer      readbackBuf;
        VmaAllocation readbackAlloc;
        {
            VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bci.size        = readbackSize;
            bci.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_CPU_ONLY;
            EXPECT_EQ(vmaCreateBuffer(renderer.getAllocator(), &bci, &ai,
                                      &readbackBuf, &readbackAlloc, nullptr), VK_SUCCESS);
        }

        VkCommandBufferAllocateInfo cbAI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cbAI.commandPool        = renderer.getCommandPool();
        cbAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbAI.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        EXPECT_EQ(vkAllocateCommandBuffers(renderer.getDevice(), &cbAI, &cmd), VK_SUCCESS);

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        renderer.recordShadowPass(cmd);
        renderer.recordMainPass(cmd, hrt.rt, /*directMode=*/true,
                                uiVtxBuf, UI_VTX_COUNT, sdfThreshold);

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
        EXPECT_EQ(vkQueueSubmit(renderer.getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE),
                  VK_SUCCESS);
        vkQueueWaitIdle(renderer.getGraphicsQueue());

        void* mapped = nullptr;
        vmaMapMemory(renderer.getAllocator(), readbackAlloc, &mapped);
        std::vector<uint8_t> pixels(readbackSize);
        memcpy(pixels.data(), mapped, static_cast<size_t>(readbackSize));
        vmaUnmapMemory(renderer.getAllocator(), readbackAlloc);

        vkFreeCommandBuffers(renderer.getDevice(), renderer.getCommandPool(), 1, &cmd);
        vmaDestroyBuffer(renderer.getAllocator(), readbackBuf, readbackAlloc);

        // Transition resolve image back to COLOR_ATTACHMENT_OPTIMAL so it can
        // be rendered into again on the next call.
        VkCommandBuffer resetCmd = vku::beginOneShot(renderer.getDevice(),
                                                     renderer.getCommandPool());
        vku::imageBarrier(resetCmd, hrt.rt.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        vku::endOneShot(renderer.getDevice(), renderer.getCommandPool(),
                        renderer.getGraphicsQueue(), resetCmd);

        return pixels;
    }
};

// ---------------------------------------------------------------------------
// Test: sdfThreshold=0.0 and sdfThreshold=0.5 produce different renders
// ---------------------------------------------------------------------------

TEST_F(SDFThresholdTest, BitmapVsSDF_ProduceDifferentPixelOutput)
{
    // Render in bitmap mode (threshold=0.0): atlas R≈0.2 is returned as-is,
    // producing visible semi-transparent pixels on the surface quad.
    auto pixelsBitmap = renderAndReadback(0.0f);

    // Render in SDF mode (threshold=0.5): smoothstep(0.43, 0.57, 0.2) = 0,
    // so all UI fragments are fully transparent — only the background shows.
    auto pixelsSDF = renderAndReadback(0.5f);

    ASSERT_EQ(pixelsBitmap.size(), pixelsSDF.size());

    // Compute the total absolute difference between the two renders.
    // Any non-zero value proves the sdfThreshold push constant has an effect.
    uint64_t totalDiff = 0;
    for (size_t i = 0; i < pixelsBitmap.size(); ++i) {
        int diff = static_cast<int>(pixelsBitmap[i]) - static_cast<int>(pixelsSDF[i]);
        totalDiff += static_cast<uint64_t>(std::abs(diff));
    }

    EXPECT_GT(totalDiff, 0u)
        << "sdfThreshold=0.0 and sdfThreshold=0.5 produced identical pixel output; "
           "the sdfThreshold push constant may not be taking effect";
}
