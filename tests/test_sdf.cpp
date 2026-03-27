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

        auto transforms = computeSurfaceTransforms(m_P00, m_P10, m_P01,
                                                   static_cast<float>(Renderer::W_UI),
                                                   static_cast<float>(Renderer::H_UI),
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
        glm::mat4 ortho = glm::ortho(0.0f, static_cast<float>(Renderer::W_UI),
                                     0.0f, static_cast<float>(Renderer::H_UI),
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

        // Bind the on-edge atlas by default (tests can rebind as needed).
        renderer.bindAtlasDescriptor(atlasOnEdgeView, atlasOnEdgeSampler);
        ASSERT_TRUE(renderer.initOffscreenRT());

        // Full-canvas UI quad.
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

        // Camera looking straight at the surface.
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

        auto transforms = computeSurfaceTransforms(m_P00, m_P10, m_P01,
                                                   static_cast<float>(Renderer::W_UI),
                                                   static_cast<float>(Renderer::H_UI),
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

        glm::mat4 ortho = glm::ortho(0.0f, static_cast<float>(Renderer::W_UI),
                                     0.0f, static_cast<float>(Renderer::H_UI),
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
