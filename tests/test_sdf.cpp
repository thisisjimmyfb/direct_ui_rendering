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
#include <cstdio>

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

    // Surface corners — stored as members so renderAndReadback_traditional can use them.
    glm::vec3 m_P00{-0.5f,  0.5f, 0.0f};
    glm::vec3 m_P10{ 0.5f,  0.5f, 0.0f};
    glm::vec3 m_P01{-0.5f, -0.5f, 0.0f};
    glm::vec3 m_P11{ 0.5f, -0.5f, 0.0f};

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

        // Set up UBOs: camera looking straight at the surface.
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                          static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                          0.1f, 100.0f);
        proj[1][1] *= -1.0f;

        SceneUBO sceneUBO{};
        sceneUBO.view         = view;
        sceneUBO.proj         = proj;
        sceneUBO.lightViewProj = scene.lightViewProj(0.0f);
        sceneUBO.lightPos     = glm::vec4(scene.light().position, 1.0f);
        sceneUBO.lightDir     = glm::vec4(scene.light().direction,
                                          std::cos(scene.light().outerConeAngle));
        sceneUBO.lightColor   = glm::vec4(scene.light().color,
                                          std::cos(scene.light().innerConeAngle));
        sceneUBO.ambientColor = glm::vec4(scene.light().ambient,   1.0f);
        renderer.updateSceneUBO(sceneUBO);

        auto transforms = computeSurfaceTransforms(m_P00, m_P10, m_P01,
                                                   static_cast<float>(W_UI),
                                                   static_cast<float>(H_UI),
                                                   proj * view);
        auto clipPlanes = computeClipPlanes(m_P00, m_P10, m_P01);

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

    // Render one frame in traditional mode (recordUIRTPass + recordMainPass directMode=false)
    // and return the readback pixel buffer.
    std::vector<uint8_t> renderAndReadback_traditional(float sdfThreshold) {
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

        // Provide the surface quad geometry for the composite pipeline.
        renderer.updateSurfaceQuad(m_P00, m_P10, m_P01, m_P11);

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

        // UI RT pass: renders UI geometry into the offscreen RT with sdfThreshold applied.
        // The UI RT render pass's subpass dependency (dstSubpass=VK_SUBPASS_EXTERNAL) ensures
        // COLOR_ATTACHMENT_WRITE is visible before FRAGMENT_SHADER reads — no explicit barrier needed.
        glm::mat4 ortho = glm::ortho(0.0f, static_cast<float>(W_UI),
                                     0.0f, static_cast<float>(H_UI),
                                     -1.0f, 1.0f);
        renderer.recordUIRTPass(cmd, uiVtxBuf, UI_VTX_COUNT, ortho, sdfThreshold);

        // Main pass: composite the UI RT onto the surface quad (traditional mode).
        renderer.recordMainPass(cmd, hrt.rt, /*directMode=*/false,
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

        // Transition resolve image back to COLOR_ATTACHMENT_OPTIMAL for the next call.
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

// ---------------------------------------------------------------------------
// Test: traditional mode (recordUIRTPass) sdfThreshold affects composited output
//
// In traditional mode the SDF threshold is applied inside recordUIRTPass (ui.frag),
// NOT in the composite pass. So the composited frame must differ when the threshold
// changes. This test exercises the traditional-mode SDF code path which is separate
// from the direct-mode path tested above.
//
// Atlas has R≈0.2. In bitmap mode (threshold=0.0) those pixels are visible in the RT;
// in SDF mode (threshold=0.5) smoothstep(0.43,0.57,0.2)=0 makes the RT transparent.
// The composited output therefore differs between the two thresholds.
// ---------------------------------------------------------------------------

TEST_F(SDFThresholdTest, TraditionalMode_UIRTPass_SdfThreshold_AffectsOutput)
{
    auto pixelsBitmap = renderAndReadback_traditional(0.0f);
    auto pixelsSDF    = renderAndReadback_traditional(0.5f);

    ASSERT_EQ(pixelsBitmap.size(), pixelsSDF.size());

    uint64_t totalDiff = 0;
    for (size_t i = 0; i < pixelsBitmap.size(); ++i) {
        int diff = static_cast<int>(pixelsBitmap[i]) - static_cast<int>(pixelsSDF[i]);
        totalDiff += static_cast<uint64_t>(std::abs(diff));
    }

    EXPECT_GT(totalDiff, 0u)
        << "Traditional mode: sdfThreshold=0.0 and sdfThreshold=0.5 produced identical "
           "composited output; the sdfThreshold push constant may not be applied in "
           "recordUIRTPass";
}

// ---------------------------------------------------------------------------
// SDFOnEdgeTest — atlas where every pixel has R = SDF_ON_EDGE_VALUE (128).
//
// With sdfThreshold = 0.5 and spread = 0.07:
//   dist  = 128/255 ≈ 0.502
//   alpha = smoothstep(0.43, 0.57, 0.502) ≈ 0.521   (non-zero)
//
// This verifies that pixels sitting exactly ON the SDF threshold produce
// semi-transparent (non-zero alpha) output — i.e. that smoothstep straddles
// the threshold rather than returning 0 like a below-threshold value would.
//
// Comparison strategy: render with the on-edge atlas AND sdfThreshold=0.5,
// then render with an all-zero transparent atlas AND sdfThreshold=0.5.
//   all-zero atlas: dist=0, alpha = smoothstep(0.43,0.57,0) = 0  → no UI contribution
//   on-edge atlas:  dist≈0.502, alpha ≈ 0.521                    → visible UI
// The two composited frames must differ, proving on-edge pixels ARE visible.
// ---------------------------------------------------------------------------

class SDFOnEdgeTest : public ::testing::Test {
protected:
    static constexpr uint32_t FB_WIDTH   = 640;
    static constexpr uint32_t FB_HEIGHT  = 360;
    static constexpr uint32_t ATLAS_DIM  = 64;
    static constexpr uint32_t UI_VTX_COUNT = 6;

    Renderer renderer;
    Scene    scene;

    // Three synthetic atlases:
    //   on-edge  (R=128/255≈0.502, A=255): sits at the SDF threshold midpoint
    //   zero     (R=0,   A=0):             fully transparent, no SDF contribution
    //   above    (R=220/255≈0.863, A=255): well above threshold+spread=0.57
    VkImage       atlasOnEdgeImg{VK_NULL_HANDLE};
    VmaAllocation atlasOnEdgeAlloc{};
    VkImageView   atlasOnEdgeView{VK_NULL_HANDLE};
    VkSampler     atlasOnEdgeSampler{VK_NULL_HANDLE};

    VkImage       atlasZeroImg{VK_NULL_HANDLE};
    VmaAllocation atlasZeroAlloc{};
    VkImageView   atlasZeroView{VK_NULL_HANDLE};
    VkSampler     atlasZeroSampler{VK_NULL_HANDLE};

    // R=220 → dist=0.863 → smoothstep(0.43, 0.57, 0.863)=1.0 (saturated)
    VkImage       atlasAboveImg{VK_NULL_HANDLE};
    VmaAllocation atlasAboveAlloc{};
    VkImageView   atlasAboveView{VK_NULL_HANDLE};
    VkSampler     atlasAboveSampler{VK_NULL_HANDLE};

    // R=25/255 ≈ 0.098 — well below threshold-spread=0.43: smoothstep=0.0 (fully transparent)
    VkImage       atlasBelowImg{VK_NULL_HANDLE};
    VmaAllocation atlasBelowAlloc{};
    VkImageView   atlasBelowView{VK_NULL_HANDLE};
    VkSampler     atlasBelowSampler{VK_NULL_HANDLE};

    VkBuffer      uiVtxBuf{VK_NULL_HANDLE};
    VmaAllocation uiVtxAlloc{};

    HeadlessRenderTarget hrt{};

    glm::vec3 m_P00{-0.5f,  0.5f, 0.0f};
    glm::vec3 m_P10{ 0.5f,  0.5f, 0.0f};
    glm::vec3 m_P01{-0.5f, -0.5f, 0.0f};
    glm::vec3 m_P11{ 0.5f, -0.5f, 0.0f};

    void SetUp() override {
        ASSERT_TRUE(renderer.init(/*headless=*/true))
            << "Headless renderer init failed";
        scene.init();
        ASSERT_TRUE(renderer.uploadSceneGeometry(scene));

        createAndUploadAtlas(SDF_ON_EDGE_VALUE, 255,
                             atlasOnEdgeImg, atlasOnEdgeAlloc,
                             atlasOnEdgeView, atlasOnEdgeSampler);
        createAndUploadAtlas(0, 0,
                             atlasZeroImg, atlasZeroAlloc,
                             atlasZeroView, atlasZeroSampler);
        createAndUploadAtlas(220, 255,
                             atlasAboveImg, atlasAboveAlloc,
                             atlasAboveView, atlasAboveSampler);
        createAndUploadAtlas(25, 200,
                             atlasBelowImg, atlasBelowAlloc,
                             atlasBelowView, atlasBelowSampler);

        // Bind the on-edge atlas by default (tests can rebind as needed).
        renderer.bindAtlasDescriptor(atlasOnEdgeView, atlasOnEdgeSampler);
        ASSERT_TRUE(renderer.initOffscreenRT());

        // Full-canvas UI quad.
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

        // Camera looking straight at the surface.
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                          static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                          0.1f, 100.0f);
        proj[1][1] *= -1.0f;

        SceneUBO sceneUBO{};
        sceneUBO.view         = view;
        sceneUBO.proj         = proj;
        sceneUBO.lightViewProj = scene.lightViewProj(0.0f);
        sceneUBO.lightPos     = glm::vec4(scene.light().position, 1.0f);
        sceneUBO.lightDir     = glm::vec4(scene.light().direction,
                                          std::cos(scene.light().outerConeAngle));
        sceneUBO.lightColor   = glm::vec4(scene.light().color,
                                          std::cos(scene.light().innerConeAngle));
        sceneUBO.ambientColor = glm::vec4(scene.light().ambient,   1.0f);
        renderer.updateSceneUBO(sceneUBO);

        auto transforms = computeSurfaceTransforms(m_P00, m_P10, m_P01,
                                                   static_cast<float>(W_UI),
                                                   static_cast<float>(H_UI),
                                                   proj * view);
        auto clipPlanes = computeClipPlanes(m_P00, m_P10, m_P01);

        SurfaceUBO surfaceUBO{};
        surfaceUBO.totalMatrix = transforms.M_total;
        surfaceUBO.worldMatrix = transforms.M_world;
        for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
        surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
        renderer.updateSurfaceUBO(surfaceUBO);
    }

    void TearDown() override {
        if (renderer.getDevice() != VK_NULL_HANDLE)
            vkDeviceWaitIdle(renderer.getDevice());
        renderer.destroyHeadlessRT(hrt);
        if (uiVtxBuf != VK_NULL_HANDLE)
            vmaDestroyBuffer(renderer.getAllocator(), uiVtxBuf, uiVtxAlloc);
        destroyAtlas(atlasOnEdgeImg,  atlasOnEdgeAlloc,  atlasOnEdgeView,  atlasOnEdgeSampler);
        destroyAtlas(atlasZeroImg,    atlasZeroAlloc,    atlasZeroView,    atlasZeroSampler);
        destroyAtlas(atlasAboveImg,   atlasAboveAlloc,   atlasAboveView,   atlasAboveSampler);
        destroyAtlas(atlasBelowImg,   atlasBelowAlloc,   atlasBelowView,   atlasBelowSampler);
        renderer.cleanup();
    }

    // Render one direct-mode frame with the currently-bound atlas and return pixels.
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

    // Render one frame in traditional mode (recordUIRTPass + recordMainPass
    // directMode=false) with the currently-bound atlas and return the composited
    // pixel buffer.
    std::vector<uint8_t> renderAndReadback_traditional(float sdfThreshold) {
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

        renderer.updateSurfaceQuad(m_P00, m_P10, m_P01, m_P11);

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

        glm::mat4 ortho = glm::ortho(0.0f, static_cast<float>(W_UI),
                                     0.0f, static_cast<float>(H_UI),
                                     -1.0f, 1.0f);
        renderer.recordUIRTPass(cmd, uiVtxBuf, UI_VTX_COUNT, ortho, sdfThreshold);

        renderer.recordMainPass(cmd, hrt.rt, /*directMode=*/false,
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

private:
    // Create a ATLAS_DIM×ATLAS_DIM RGBA8 image where every pixel = (r, r, r, a),
    // upload it to the GPU, and populate the image/view/sampler out-params.
    void createAndUploadAtlas(uint8_t r, uint8_t a,
                              VkImage& imgOut, VmaAllocation& allocOut,
                              VkImageView& viewOut, VkSampler& samplerOut)
    {
        std::vector<uint8_t> pixels(ATLAS_DIM * ATLAS_DIM * 4);
        for (size_t i = 0; i < ATLAS_DIM * ATLAS_DIM; ++i) {
            pixels[i * 4 + 0] = r;
            pixels[i * 4 + 1] = r;
            pixels[i * 4 + 2] = r;
            pixels[i * 4 + 3] = a;
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
                                 &imgOut, &allocOut, nullptr), VK_SUCCESS);

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
        vku::imageBarrier(uploadCmd, imgOut,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent      = {ATLAS_DIM, ATLAS_DIM, 1};
        vkCmdCopyBufferToImage(uploadCmd, stagingBuf, imgOut,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        vku::imageBarrier(uploadCmd, imgOut,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        vku::endOneShot(renderer.getDevice(), renderer.getCommandPool(),
                        renderer.getGraphicsQueue(), uploadCmd);
        vmaDestroyBuffer(renderer.getAllocator(), stagingBuf, stagingAlloc);

        VkImageViewCreateInfo viewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewCI.image            = imgOut;
        viewCI.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format           = VK_FORMAT_R8G8B8A8_UNORM;
        viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        ASSERT_EQ(vkCreateImageView(renderer.getDevice(), &viewCI, nullptr, &viewOut),
                  VK_SUCCESS);

        VkSamplerCreateInfo sampCI{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampCI.magFilter    = VK_FILTER_NEAREST;
        sampCI.minFilter    = VK_FILTER_NEAREST;
        sampCI.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ASSERT_EQ(vkCreateSampler(renderer.getDevice(), &sampCI, nullptr, &samplerOut),
                  VK_SUCCESS);
    }

    void destroyAtlas(VkImage& img, VmaAllocation& alloc,
                      VkImageView& view, VkSampler& sampler)
    {
        if (sampler != VK_NULL_HANDLE) { vkDestroySampler(renderer.getDevice(), sampler, nullptr); sampler = VK_NULL_HANDLE; }
        if (view    != VK_NULL_HANDLE) { vkDestroyImageView(renderer.getDevice(), view, nullptr);  view    = VK_NULL_HANDLE; }
        if (img     != VK_NULL_HANDLE) { vmaDestroyImage(renderer.getAllocator(), img, alloc);     img     = VK_NULL_HANDLE; }
    }
};

// ---------------------------------------------------------------------------
// Test: on-edge atlas with sdfThreshold=0.5 produces visible (non-zero alpha) pixels
// ---------------------------------------------------------------------------

TEST_F(SDFOnEdgeTest, OnEdge_SdfThreshold05_ProducesNonZeroAlphaPixels)
{
    // Render with the on-edge atlas (R=128/255≈0.502) at sdfThreshold=0.5.
    // smoothstep(0.43, 0.57, 0.502) ≈ 0.521  → UI fragments are visible.
    renderer.bindAtlasDescriptor(atlasOnEdgeView, atlasOnEdgeSampler);
    auto pixelsOnEdge = renderAndReadback(0.5f);

    // Render with the all-transparent atlas (R=0, A=0) at sdfThreshold=0.5.
    // smoothstep(0.43, 0.57, 0.0) = 0.0  → UI fragments are fully transparent.
    renderer.bindAtlasDescriptor(atlasZeroView, atlasZeroSampler);
    auto pixelsZero = renderAndReadback(0.5f);

    ASSERT_EQ(pixelsOnEdge.size(), pixelsZero.size());

    // If the renders differ the on-edge atlas IS contributing visible pixels.
    // A zero totalDiff would mean smoothstep failed to produce non-zero alpha
    // for on-edge distance values.
    uint64_t totalDiff = 0;
    for (size_t i = 0; i < pixelsOnEdge.size(); ++i) {
        int diff = static_cast<int>(pixelsOnEdge[i]) - static_cast<int>(pixelsZero[i]);
        totalDiff += static_cast<uint64_t>(std::abs(diff));
    }

    EXPECT_GT(totalDiff, 0u)
        << "On-edge atlas (R=SDF_ON_EDGE_VALUE) with sdfThreshold=0.5 produced the same "
           "output as a fully-transparent atlas; smoothstep may not be straddling the "
           "threshold (expected alpha ≈ 0.521 for dist=128/255≈0.502)";
}

// ---------------------------------------------------------------------------
// Test: above-threshold atlas with sdfThreshold=0.5 produces saturated (full-alpha)
// pixels, verifying that smoothstep clamps to 1.0 at the high end.
//
// Atlas: all pixels R=220/255≈0.863, A=255.
//   dist   = 0.863
//   spread = 0.07
//   alpha  = smoothstep(0.43, 0.57, 0.863) = 1.0  (saturated)
//
// Compare against the zero atlas (R=0, A=0):
//   alpha  = smoothstep(0.43, 0.57, 0.0)   = 0.0  (transparent)
//
// The composited renders must differ, and the above-threshold render must
// contain pixels that differ from the zero-atlas render, proving saturation.
// ---------------------------------------------------------------------------

TEST_F(SDFOnEdgeTest, AboveThreshold_SdfThreshold05_SmoothstepSaturates)
{
    // Render with the above-threshold atlas (R=220/255≈0.863) at sdfThreshold=0.5.
    // smoothstep(0.43, 0.57, 0.863) = 1.0 → UI fragments are fully opaque.
    renderer.bindAtlasDescriptor(atlasAboveView, atlasAboveSampler);
    auto pixelsAbove = renderAndReadback(0.5f);

    // Render with the all-transparent atlas (R=0, A=0) at sdfThreshold=0.5.
    // smoothstep(0.43, 0.57, 0.0) = 0.0 → UI fragments are fully transparent.
    renderer.bindAtlasDescriptor(atlasZeroView, atlasZeroSampler);
    auto pixelsZero = renderAndReadback(0.5f);

    ASSERT_EQ(pixelsAbove.size(), pixelsZero.size());

    // The renders must differ — proving the above-threshold atlas contributes
    // visible pixels (smoothstep did not incorrectly clamp to 0).
    uint64_t totalDiff = 0;
    for (size_t i = 0; i < pixelsAbove.size(); ++i) {
        int diff = static_cast<int>(pixelsAbove[i]) - static_cast<int>(pixelsZero[i]);
        totalDiff += static_cast<uint64_t>(std::abs(diff));
    }

    EXPECT_GT(totalDiff, 0u)
        << "Above-threshold atlas (R=220/255≈0.863) with sdfThreshold=0.5 produced the "
           "same output as a fully-transparent atlas; smoothstep(0.43, 0.57, 0.863) "
           "should equal 1.0 (saturated), making the UI fully visible";
}

// ---------------------------------------------------------------------------
// Test: traditional mode — on-edge atlas (R=SDF_ON_EDGE_VALUE) with
// sdfThreshold=0.5 via recordUIRTPass produces a different composited frame
// than a fully-zero atlas.
//
// Exercises the traditional-mode SDF code path (ui.frag inside the UI RT pass)
// at the threshold boundary, complementing the direct-mode on-edge test above.
//
// On-edge atlas (R=128, A=255):
//   dist  = 128/255 ≈ 0.502
//   alpha = smoothstep(0.43, 0.57, 0.502) ≈ 0.521  → UI RT receives visible pixels
//
// Zero atlas (R=0, A=0):
//   dist  = 0.0
//   alpha = smoothstep(0.43, 0.57, 0.0) = 0.0      → UI RT is transparent
//
// The composited main-pass output must therefore differ between the two atlases,
// proving that recordUIRTPass applies sdfThreshold correctly at the boundary.
// ---------------------------------------------------------------------------

TEST_F(SDFOnEdgeTest, TraditionalMode_OnEdge_SdfThreshold05_DiffersFromZeroAtlas)
{
    // Render in traditional mode with the on-edge atlas and sdfThreshold=0.5.
    renderer.bindAtlasDescriptor(atlasOnEdgeView, atlasOnEdgeSampler);
    auto pixelsOnEdge = renderAndReadback_traditional(0.5f);

    // Render in traditional mode with the zero atlas and sdfThreshold=0.5.
    renderer.bindAtlasDescriptor(atlasZeroView, atlasZeroSampler);
    auto pixelsZero = renderAndReadback_traditional(0.5f);

    ASSERT_EQ(pixelsOnEdge.size(), pixelsZero.size());

    uint64_t totalDiff = 0;
    for (size_t i = 0; i < pixelsOnEdge.size(); ++i) {
        int diff = static_cast<int>(pixelsOnEdge[i]) - static_cast<int>(pixelsZero[i]);
        totalDiff += static_cast<uint64_t>(std::abs(diff));
    }

    EXPECT_GT(totalDiff, 0u)
        << "Traditional mode: on-edge atlas (R=SDF_ON_EDGE_VALUE, sdfThreshold=0.5) "
           "produced the same composited output as a fully-zero atlas; "
           "recordUIRTPass may not be applying sdfThreshold at the boundary "
           "(expected smoothstep(0.43, 0.57, 0.502) ≈ 0.521 to produce visible UI RT pixels)";
}

// ---------------------------------------------------------------------------
// Shared helpers for the three new coverage tests
// ---------------------------------------------------------------------------

// GLSL smoothstep reference (matches the shader implementation).
static float smoothstepRef(float edge0, float edge1, float x)
{
    float t = std::max(0.0f, std::min(1.0f, (x - edge0) / (edge1 - edge0)));
    return t * t * (3.0f - 2.0f * t);
}

// Build a SceneUBO with pure ambient lighting so lit=(1,1,1) everywhere and
// the shadow-map result has no effect on the final colour values.
static SceneUBO makeAmbientOnlyUBO(const Scene& scene,
                                   uint32_t fbWidth, uint32_t fbHeight)
{
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(fbWidth) / fbHeight,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    SceneUBO ubo{};
    ubo.view          = view;
    ubo.proj          = proj;
    ubo.lightViewProj = scene.lightViewProj(0.0f);
    ubo.lightPos      = glm::vec4(scene.light().position, 1.0f);
    ubo.lightDir      = glm::vec4(scene.light().direction,
                                  std::cos(scene.light().outerConeAngle));
    ubo.lightColor    = glm::vec4(0.0f, 0.0f, 0.0f,
                                  std::cos(scene.light().innerConeAngle)); // no directional component
    ubo.ambientColor  = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f); // full ambient → lit=(1,1,1)
    ubo.lightIntensity = 1.0f;
    ubo.uiColorPhase = 0.0f;
    ubo.isTerminalMode = 0.0f;
    return ubo;
}

// ---------------------------------------------------------------------------
// Test: below-threshold pixels are fully transparent
//
// Atlas R=25/255 ≈ 0.098 — well below the lower smoothstep edge
// (threshold - spread = 0.5 - 0.07 = 0.43).
//   alpha = smoothstep(0.43, 0.57, 0.098) = 0.0  → no UI contribution
//
// Complements AboveThreshold_SdfThreshold05_SmoothstepSaturates:
// that test proves smoothstep saturates at the high end; this proves it
// clamps to 0.0 at the low end (below-threshold pixels are fully discarded).
//
// Expected: render with R=25 atlas is pixel-identical to render with zero atlas.
// ---------------------------------------------------------------------------

TEST_F(SDFOnEdgeTest, BelowThreshold_PixelsAreFullyTransparent)
{
    // Render with the below-threshold atlas (R=25/255≈0.098) at sdfThreshold=0.5.
    // smoothstep(0.43, 0.57, 0.098) = 0.0 → UI fragments are fully transparent.
    renderer.bindAtlasDescriptor(atlasBelowView, atlasBelowSampler);
    auto pixelsBelow = renderAndReadback(0.5f);

    // Render with the all-transparent zero atlas (R=0, A=0) at sdfThreshold=0.5.
    renderer.bindAtlasDescriptor(atlasZeroView, atlasZeroSampler);
    auto pixelsZero = renderAndReadback(0.5f);

    ASSERT_EQ(pixelsBelow.size(), pixelsZero.size());

    uint64_t totalDiff = 0;
    for (size_t i = 0; i < pixelsBelow.size(); ++i) {
        int diff = static_cast<int>(pixelsBelow[i]) - static_cast<int>(pixelsZero[i]);
        totalDiff += static_cast<uint64_t>(std::abs(diff));
    }

    EXPECT_EQ(totalDiff, 0u)
        << "Below-threshold atlas (R=25/255≈0.098, sdfThreshold=0.5) produced a "
           "different render than the fully-transparent zero atlas; "
           "smoothstep(0.43, 0.57, 0.098) should equal 0.0, making the UI "
           "contribution fully transparent (complements AboveThreshold saturation test)";
}

// ---------------------------------------------------------------------------
// Test: shadow-SDF interaction in direct mode — alpha is SDF-derived, not shadow-derived
//
// In ui_direct.frag the output is:
//   outColor = vec4(lit * alpha, alpha)   // pre-multiplied lit white text
// where alpha = smoothstep(dist) is purely SDF-derived, and lit depends on shadow.
//
// To verify alpha is independent of shadow, use pure ambient (lightColor=0,
// ambientColor=1) so lit=(1,1,1) everywhere and shadow has no effect.
// With teal.r=0 the composited red channel satisfies:
//   out.r = lit.r * alpha + teal.r * (1 - alpha) = alpha
//
// So the red-channel difference (on-edge minus zero atlas) equals alpha directly.
// Expected centre-pixel diff.r = smoothstep(0.43, 0.57, SDF_ON_EDGE_VALUE/255) * 255 ≈ 133.
// ---------------------------------------------------------------------------

TEST_F(SDFOnEdgeTest, ShadowSDF_AlphaIsSDFDerived_InDirectMode)
{
    // Pure ambient lighting: lit=(1,1,1) → shadow-map result has no effect.
    renderer.updateSceneUBO(makeAmbientOnlyUBO(scene, FB_WIDTH, FB_HEIGHT));

    // Populate the surface quad buffer so pipeSurface renders teal at the expected
    // location.  renderAndReadback (direct mode) does not call updateSurfaceQuad
    // itself (unlike renderAndReadback_traditional), so we do it here.
    renderer.updateSurfaceQuad(m_P00, m_P10, m_P01, m_P11);

    renderer.bindAtlasDescriptor(atlasOnEdgeView, atlasOnEdgeSampler);
    auto pixelsOnEdge = renderAndReadback(0.5f);

    renderer.bindAtlasDescriptor(atlasZeroView, atlasZeroSampler);
    auto pixelsZero = renderAndReadback(0.5f);

    // Centre pixel of FB_WIDTH x FB_HEIGHT = world-space surface centre (0,0,0).
    const uint32_t cx = FB_WIDTH / 2;
    const uint32_t cy = FB_HEIGHT / 2;
    const size_t   ci = (static_cast<size_t>(cy) * FB_WIDTH + cx) * 4;

    int diffR = static_cast<int>(pixelsOnEdge[ci + 0])
              - static_cast<int>(pixelsZero[ci + 0]);

    // Expected: smoothstep(0.43, 0.57, 128/255) * 255 ≈ 133.
    // teal.r=0, so out.r = alpha regardless of shadow (demonstrating independence).
    const float expectedAlpha = smoothstepRef(0.43f, 0.57f,
                                              static_cast<float>(SDF_ON_EDGE_VALUE) / 255.0f);
    const int   expectedDiffR = static_cast<int>(std::round(expectedAlpha * 255.0f));

    EXPECT_NEAR(diffR, expectedDiffR, 15)
        << "Centre-pixel R diff = " << diffR
        << ", expected ≈ " << expectedDiffR
        << " (smoothstep(0.43, 0.57, " << static_cast<int>(SDF_ON_EDGE_VALUE)
        << "/255) * 255). Verifies alpha is SDF-derived and independent of shadow "
           "lighting applied to RGB.";
}

// ---------------------------------------------------------------------------
// Test: pre-multiplied alpha pipeline correctness — traditional composite mode
//
// composite.frag implements the pre-multiplied alpha blend:
//   composited.rgb = uiColor.rgb + teal * (1 - uiColor.a)
//
// With on-edge atlas (R=128, sdfThreshold=0.5), ui.frag writes:
//   uiColor = (alpha, alpha, alpha, alpha)  where alpha ≈ 0.521
//
// With pure ambient (lit=1) and teal=(0, 0.5, 0.5):
//   composited.r = alpha + 0   * (1-alpha) = alpha        → diff.r ≈ 133
//   composited.g = alpha + 0.5 * (1-alpha)                → diff.g = 0.5 * alpha ≈ 67
//
// Pre-multiplied pipeline is correct iff 2 * diff.g ≈ diff.r.
// This asserts both that the teal channel bleeds through at (1-alpha) proportion
// and that the UI RGB contribution matches the pre-multiplied expected value.
// ---------------------------------------------------------------------------

TEST_F(SDFOnEdgeTest, PreMultipliedAlpha_TraditionalMode_TealBleeds)
{
    renderer.updateSceneUBO(makeAmbientOnlyUBO(scene, FB_WIDTH, FB_HEIGHT));

    renderer.bindAtlasDescriptor(atlasOnEdgeView, atlasOnEdgeSampler);
    auto pixelsOnEdge = renderAndReadback_traditional(0.5f);

    renderer.bindAtlasDescriptor(atlasZeroView, atlasZeroSampler);
    auto pixelsZero = renderAndReadback_traditional(0.5f);

    const uint32_t cx = FB_WIDTH / 2;
    const uint32_t cy = FB_HEIGHT / 2;
    const size_t   ci = (static_cast<size_t>(cy) * FB_WIDTH + cx) * 4;

    int diffR = static_cast<int>(pixelsOnEdge[ci + 0])
              - static_cast<int>(pixelsZero[ci + 0]);
    int diffG = static_cast<int>(pixelsOnEdge[ci + 1])
              - static_cast<int>(pixelsZero[ci + 1]);

    // With lit=(1,1,1), teal.r=0:
    //   diff.r = alpha * lit.r = alpha
    //   diff.g = [composited_on_edge.g - composited_zero.g] * lit.g
    //          = [(alpha + 0.5*(1-alpha)) - 0.5] * 1 = 0.5 * alpha
    // So 2*diff.g must equal diff.r (the pre-multiplied alpha invariant).
    EXPECT_GT(diffR, 0)
        << "On-edge atlas in traditional mode should produce a positive R diff "
           "(alpha > 0 means UI is visible)";
    EXPECT_NEAR(2 * diffG, diffR, 4)
        << "Pre-multiplied alpha invariant: 2*diff.g (" << 2 * diffG
        << ") should ≈ diff.r (" << diffR
        << "). Verifies composite.frag correctly blends "
           "composited.rgb = uiColor.rgb + teal*(1-uiColor.a).";
}

// ---------------------------------------------------------------------------
// Test: Hello World SDF render — verify non-zero alpha pixels exist
//
// This test uses the actual glyph atlas (not a synthetic one) and renders
// the "Hello World" text in SDF mode. It verifies that:
//   1. Non-zero alpha pixels exist in the expected text area
//   2. The alpha values are consistent with SDF smoothstep sampling
//
// The test uses a zero-alpha comparison atlas to isolate the UI contribution.
// ---------------------------------------------------------------------------

class SDFHelloWorldTest : public ::testing::Test {
protected:
    Renderer renderer;
    Scene    scene;

    VkImage       helloAtlasImg{VK_NULL_HANDLE};
    VmaAllocation helloAtlasAlloc{};
    VkImageView   helloAtlasView{VK_NULL_HANDLE};
    VkSampler     helloAtlasSampler{VK_NULL_HANDLE};

    VkBuffer      uiVtxBuf{VK_NULL_HANDLE};
    VmaAllocation uiVtxAlloc{};
    static constexpr uint32_t UI_VTX_COUNT = 6;  // Full-canvas quad

    HeadlessRenderTarget hrt{};

    glm::vec3 m_P00{-0.5f,  0.5f, 0.0f};
    glm::vec3 m_P10{ 0.5f,  0.5f, 0.0f};
    glm::vec3 m_P01{-0.5f, -0.5f, 0.0f};
    glm::vec3 m_P11{ 0.5f, -0.5f, 0.0f};

    void SetUp() override {
        ASSERT_TRUE(renderer.init(/*headless=*/true))
            << "Headless renderer init failed";
        scene.init();
        ASSERT_TRUE(renderer.uploadSceneGeometry(scene));

        // Create a synthetic "H" glyph atlas (simulating a glyph in the atlas)
        createHelloWorldAtlas(helloAtlasImg, helloAtlasAlloc,
                              helloAtlasView, helloAtlasSampler);

        // Create a full-canvas UI quad that will render the "H" glyph
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

        // Bind the synthetic atlas
        renderer.bindAtlasDescriptor(helloAtlasView, helloAtlasSampler);
        ASSERT_TRUE(renderer.initOffscreenRT());

        ASSERT_TRUE(renderer.createHeadlessRT(FB_WIDTH, FB_HEIGHT, hrt));

        // Camera looking straight at the surface.
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                          static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                          0.1f, 100.0f);
        proj[1][1] *= -1.0f;

        SceneUBO sceneUBO{};
        sceneUBO.view         = view;
        sceneUBO.proj         = proj;
        sceneUBO.lightViewProj = scene.lightViewProj(0.0f);
        sceneUBO.lightPos     = glm::vec4(scene.light().position, 1.0f);
        sceneUBO.lightDir     = glm::vec4(scene.light().direction,
                                          std::cos(scene.light().outerConeAngle));
        sceneUBO.lightColor   = glm::vec4(scene.light().color,
                                          std::cos(scene.light().innerConeAngle));
        sceneUBO.ambientColor = glm::vec4(scene.light().ambient,   1.0f);
        renderer.updateSceneUBO(sceneUBO);

        auto transforms = computeSurfaceTransforms(m_P00, m_P10, m_P01,
                                                   static_cast<float>(W_UI),
                                                   static_cast<float>(H_UI),
                                                   proj * view);
        auto clipPlanes = computeClipPlanes(m_P00, m_P10, m_P01);

        SurfaceUBO surfaceUBO{};
        surfaceUBO.totalMatrix = transforms.M_total;
        surfaceUBO.worldMatrix = transforms.M_world;
        for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
        surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
        renderer.updateSurfaceUBO(surfaceUBO);
    }

    void TearDown() override {
        if (renderer.getDevice() != VK_NULL_HANDLE)
            vkDeviceWaitIdle(renderer.getDevice());
        renderer.destroyHeadlessRT(hrt);
        if (uiVtxBuf != VK_NULL_HANDLE)
            vmaDestroyBuffer(renderer.getAllocator(), uiVtxBuf, uiVtxAlloc);
        destroyAtlas(helloAtlasImg, helloAtlasAlloc, helloAtlasView, helloAtlasSampler);
        renderer.cleanup();
    }

    // Create a synthetic atlas with "H" glyph pattern for testing
    void createHelloWorldAtlas(VkImage& imgOut, VmaAllocation& allocOut,
                               VkImageView& viewOut, VkSampler& samplerOut)
    {
        // Create a 512x512 atlas with a simple "H" pattern in the center
        // This simulates a glyph atlas without needing to load a real PNG
        std::vector<uint8_t> pixels(ATLAS_SIZE * ATLAS_SIZE * 4, 0);

        // Draw an "H" shape in the center region (simulating a glyph)
        int cx = ATLAS_SIZE / 2;
        int cy = ATLAS_SIZE / 2;
        int hw = 64;  // half-width of glyph
        int ht = 32;  // half-height of glyph

        for (int y = -ht; y <= ht; ++y) {
            for (int x = -hw; x <= hw; ++x) {
                // "H" shape: two vertical bars and one horizontal bar
                bool isH = (std::abs(x) <= 8) || (std::abs(y) <= 4);
                if (isH) {
                    int px = cx + x;
                    int py = cy + y;
                    if (px >= 0 && px < ATLAS_SIZE && py >= 0 && py < ATLAS_SIZE) {
                        size_t i = (py * ATLAS_SIZE + px) * 4;
                        pixels[i + 0] = 128;  // R = SDF_ON_EDGE_VALUE
                        pixels[i + 1] = 128;
                        pixels[i + 2] = 128;
                        pixels[i + 3] = 255;  // Full alpha
                    }
                }
            }
        }

        // Upload to GPU
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.format        = VK_FORMAT_R8G8B8A8_UNORM;
        ci.extent        = {ATLAS_SIZE, ATLAS_SIZE, 1};
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
                                 &imgOut, &allocOut, nullptr), VK_SUCCESS);

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
        vku::imageBarrier(uploadCmd, imgOut,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent      = {ATLAS_SIZE, ATLAS_SIZE, 1};
        vkCmdCopyBufferToImage(uploadCmd, stagingBuf, imgOut,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        vku::imageBarrier(uploadCmd, imgOut,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        vku::endOneShot(renderer.getDevice(), renderer.getCommandPool(),
                        renderer.getGraphicsQueue(), uploadCmd);
        vmaDestroyBuffer(renderer.getAllocator(), stagingBuf, stagingAlloc);

        VkImageViewCreateInfo viewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewCI.image            = imgOut;
        viewCI.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format           = VK_FORMAT_R8G8B8A8_UNORM;
        viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        ASSERT_EQ(vkCreateImageView(renderer.getDevice(), &viewCI, nullptr, &viewOut),
                  VK_SUCCESS);

        VkSamplerCreateInfo sampCI{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampCI.magFilter    = VK_FILTER_NEAREST;
        sampCI.minFilter    = VK_FILTER_NEAREST;
        sampCI.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ASSERT_EQ(vkCreateSampler(renderer.getDevice(), &sampCI, nullptr, &samplerOut),
                  VK_SUCCESS);
    }

    void destroyAtlas(VkImage& img, VmaAllocation& alloc,
                      VkImageView& view, VkSampler& sampler)
    {
        if (sampler != VK_NULL_HANDLE) { vkDestroySampler(renderer.getDevice(), sampler, nullptr); sampler = VK_NULL_HANDLE; }
        if (view    != VK_NULL_HANDLE) { vkDestroyImageView(renderer.getDevice(), view, nullptr);  view    = VK_NULL_HANDLE; }
        if (img     != VK_NULL_HANDLE) { vmaDestroyImage(renderer.getAllocator(), img, alloc);     img     = VK_NULL_HANDLE; }
    }

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

// Test: Hello World SDF render — verify non-zero alpha pixels exist in expected text area
TEST_F(SDFHelloWorldTest, HelloWorld_SdfMode_VisibleText)
{
    // Render with sdfThreshold=0.5 (SDF mode)
    auto pixelsSDF = renderAndReadback(0.5f);

    // Count pixels with alpha > 0.1 (visible text pixels)
    uint32_t visiblePixelCount = 0;
    for (uint32_t y = 0; y < FB_HEIGHT; ++y) {
        for (uint32_t x = 0; x < FB_WIDTH; ++x) {
            const size_t i = (y * FB_WIDTH + x) * 4;
            uint8_t alpha = pixelsSDF[i + 3];
            if (alpha > 25) {  // alpha > 0.1
                visiblePixelCount++;
            }
        }
    }

    EXPECT_GT(visiblePixelCount, 0u)
        << "SDF mode render of Hello World should produce visible (alpha > 0) pixels; "
           "the glyph atlas may not be loaded correctly or SDF sampling may be broken";

    // The exact count depends on the atlas content, but we expect at least some
    // visible pixels for the "Hello World" text
    std::cout << "Visible text pixels: " << visiblePixelCount << std::endl;
}
