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
#include "ui_system.h"
#include "vk_utils.h"
#include "scene_ubo_helper.h"

#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>
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

        // Camera above the surface, looking down at its centre (0,1,-2).
        // up=(1,0,0) because look direction is mostly -Y.
        m_view = glm::lookAt(glm::vec3(0.0f, 2.5f, -1.0f),
                             glm::vec3(0.0f, 1.0f, -2.0f),
                             glm::vec3(1.0f, 0.0f,  0.0f));
        m_proj = glm::perspective(glm::radians(60.0f),
                                  static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                  0.1f, 100.0f);
        m_proj[1][1] *= -1.0f;  // Vulkan Y-flip

        // White atlas: all pixels = (255,255,255,255) — fully opaque white.
        {
            std::vector<uint8_t> pixels(ATLAS_DIM * ATLAS_DIM * 4, 255);

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

            VkBuffer stagingBuf;
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
            sampCI.magFilter    = VK_FILTER_LINEAR;
            sampCI.minFilter    = VK_FILTER_LINEAR;
            sampCI.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            ASSERT_EQ(vkCreateSampler(renderer.getDevice(), &sampCI, nullptr, &atlasSampler),
                      VK_SUCCESS);

            renderer.bindAtlasDescriptor(atlasView, atlasSampler);
        }

        // Full-canvas UI quad in UI space [(0,0)..(W_UI,H_UI)].
        {
            const float W = static_cast<float>(W_UI);
            const float H = static_cast<float>(H_UI);
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
        ASSERT_TRUE(renderer.initOffscreenRT());
    }

    void TearDown() override {
        if (renderer.getDevice() != VK_NULL_HANDLE)
            vkDeviceWaitIdle(renderer.getDevice());
        if (atlasSampler != VK_NULL_HANDLE) {
            vkDestroySampler(renderer.getDevice(), atlasSampler, nullptr);
            atlasSampler = VK_NULL_HANDLE;
        }
        if (atlasView != VK_NULL_HANDLE) {
            vkDestroyImageView(renderer.getDevice(), atlasView, nullptr);
            atlasView = VK_NULL_HANDLE;
        }
        if (atlasImg != VK_NULL_HANDLE) {
            vmaDestroyImage(renderer.getAllocator(), atlasImg, atlasAlloc);
            atlasImg = VK_NULL_HANDLE;
        }
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

        const VkDeviceSize readbackSize = static_cast<VkDeviceSize>(FB_WIDTH) * FB_HEIGHT * 4;
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
                                uiVtxBuf, UI_VTX_COUNT, /*sdfThreshold=*/0.0f);

        vku::imageBarrier(cmd, hrt.rt.image,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkBufferImageCopy copyRegion{};
        copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.imageExtent      = {FB_WIDTH, FB_HEIGHT, 1};
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
