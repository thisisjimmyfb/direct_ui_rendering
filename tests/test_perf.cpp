#include <gtest/gtest.h>
#include "renderer.h"
#include "scene.h"
#include "ui_surface.h"
#include "ui_system.h"
#include "metrics.h"
#include "vk_utils.h"
#include "perf_reference.h"

#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>
#include <cmath>

// Number of headless frames to render for each measurement.
static constexpr int PERF_FRAME_COUNT = 60;

static constexpr uint32_t FB_WIDTH  = 1280;
static constexpr uint32_t FB_HEIGHT = 720;

// ---------------------------------------------------------------------------
// Performance regression tests
// These tests will be skipped (GTEST_SKIP) until perf_reference.h is filled in.
// ---------------------------------------------------------------------------

class PerfTest : public ::testing::Test {
protected:
    Renderer renderer;
    Scene    scene;
    Metrics  metrics;

    // Shared per-test GPU resources
    VkImage       dummyImg{VK_NULL_HANDLE};
    VmaAllocation dummyAlloc{};
    VkImageView   dummyView{VK_NULL_HANDLE};
    VkSampler     dummySampler{VK_NULL_HANDLE};

    VkBuffer      uiVtxBuf{VK_NULL_HANDLE};
    VmaAllocation uiVtxAlloc{};
    uint32_t      uiVtxCount{0};

    HeadlessRenderTarget hrt{};
    VkCommandBuffer cmd{VK_NULL_HANDLE};

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
            ASSERT_EQ(vkCreateImageView(renderer.getDevice(), &viewCI, nullptr, &dummyView), VK_SUCCESS);

            VkSamplerCreateInfo sampCI{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            sampCI.magFilter    = VK_FILTER_NEAREST;
            sampCI.minFilter    = VK_FILTER_NEAREST;
            sampCI.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            ASSERT_EQ(vkCreateSampler(renderer.getDevice(), &sampCI, nullptr, &dummySampler), VK_SUCCESS);

            renderer.bindAtlasDescriptor(dummyView, dummySampler);
        }

        // Ensure offscreen RT descriptor is valid (needed even in direct mode to satisfy set 2).
        ASSERT_TRUE(renderer.initOffscreenRT());

        // UI vertex buffer: one full-canvas quad.
        {
            uiVtxCount = 6;
            const float W = static_cast<float>(Renderer::W_UI);
            const float H = static_cast<float>(Renderer::H_UI);
            UIVertex verts[6] = {
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

        // Headless render target.
        ASSERT_TRUE(renderer.createHeadlessRT(FB_WIDTH, FB_HEIGHT, hrt));

        // Reusable command buffer.
        VkCommandBufferAllocateInfo cbAI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cbAI.commandPool        = renderer.getCommandPool();
        cbAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbAI.commandBufferCount = 1;
        ASSERT_EQ(vkAllocateCommandBuffers(renderer.getDevice(), &cbAI, &cmd), VK_SUCCESS);

        // Upload initial UBOs.
        glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.5f, 4.0f),
                                     glm::vec3(0.0f, 1.5f, 0.0f),
                                     glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                          static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                          0.1f, 100.0f);
        proj[1][1] *= -1.0f;

        SceneUBO sceneUBO{};
        sceneUBO.view         = view;
        sceneUBO.proj         = proj;
        sceneUBO.lightViewProj = scene.lightViewProj();
        sceneUBO.lightPos     = glm::vec4(scene.light().position, 1.0f);
        sceneUBO.lightDir     = glm::vec4(scene.light().direction,
                                          std::cos(scene.light().outerConeAngle));
        sceneUBO.lightColor   = glm::vec4(scene.light().color,
                                          std::cos(scene.light().innerConeAngle));
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
            if (cmd != VK_NULL_HANDLE)
                vkFreeCommandBuffers(renderer.getDevice(), renderer.getCommandPool(), 1, &cmd);
        }
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

    // Record and submit one headless frame (shadow + optional UI RT + main pass).
    // The command buffer is reset and re-recorded each call.
    void renderOneFrame(bool directMode) {
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        renderer.recordShadowPass(cmd);

        if (!directMode) {
            glm::mat4 uiOrtho = glm::ortho(0.0f, static_cast<float>(Renderer::W_UI),
                                           static_cast<float>(Renderer::H_UI), 0.0f,
                                           -1.0f, 1.0f);
            renderer.recordUIRTPass(cmd, uiVtxBuf, uiVtxCount, uiOrtho);
        }

        renderer.recordMainPass(cmd, hrt.rt, directMode, uiVtxBuf, uiVtxCount);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &cmd;
        vkQueueSubmit(renderer.getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(renderer.getGraphicsQueue());
    }
};

TEST_F(PerfTest, DirectMode_FrameTime_WithinTolerance)
{
    if (perf_ref::FRAME_TIME_DIRECT_MS == 0.0f) {
        GTEST_SKIP() << "perf_reference.h not yet filled in";
    }
    if (std::getenv("CI")) {
        GTEST_SKIP() << "Skipped on CI: no real GPU available (software renderer skews timing)";
    }

    for (int i = 0; i < PERF_FRAME_COUNT; ++i) {
        metrics.beginFrame();
        renderOneFrame(/*directMode=*/true);
        metrics.endFrame();
    }

    float avg = metrics.averageFrameMs();
    float limit = perf_ref::FRAME_TIME_DIRECT_MS * (1.0f + perf_ref::TIME_TOLERANCE);
    EXPECT_LE(avg, limit)
        << "Direct mode frame time " << avg << " ms exceeds reference "
        << perf_ref::FRAME_TIME_DIRECT_MS << " ms (+" << perf_ref::TIME_TOLERANCE * 100 << "%)";
}

TEST_F(PerfTest, TraditionalMode_FrameTime_WithinTolerance)
{
    if (perf_ref::FRAME_TIME_TRADITIONAL_MS == 0.0f) {
        GTEST_SKIP() << "perf_reference.h not yet filled in";
    }
    if (std::getenv("CI")) {
        GTEST_SKIP() << "Skipped on CI: no real GPU available (software renderer skews timing)";
    }

    for (int i = 0; i < PERF_FRAME_COUNT; ++i) {
        metrics.beginFrame();
        renderOneFrame(/*directMode=*/false);
        metrics.endFrame();
    }

    float avg = metrics.averageFrameMs();
    float limit = perf_ref::FRAME_TIME_TRADITIONAL_MS * (1.0f + perf_ref::TIME_TOLERANCE);
    EXPECT_LE(avg, limit)
        << "Traditional mode frame time " << avg << " ms exceeds reference "
        << perf_ref::FRAME_TIME_TRADITIONAL_MS << " ms (+" << perf_ref::TIME_TOLERANCE * 100 << "%)";
}

TEST_F(PerfTest, DirectMode_GPUMem_WithinTolerance)
{
    if (perf_ref::GPU_MEM_DIRECT_BYTES == 0) {
        GTEST_SKIP() << "perf_reference.h not yet filled in";
    }

    renderOneFrame(/*directMode=*/true);
    metrics.updateGPUMem(renderer.getAllocator());
    uint64_t used = metrics.gpuAllocatedBytes();
    uint64_t limit = static_cast<uint64_t>(
        perf_ref::GPU_MEM_DIRECT_BYTES * (1.0 + perf_ref::MEM_TOLERANCE));

    EXPECT_LE(used, limit)
        << "Direct mode GPU mem " << used << " B exceeds reference "
        << perf_ref::GPU_MEM_DIRECT_BYTES << " B (+" << perf_ref::MEM_TOLERANCE * 100 << "%)";
}

TEST_F(PerfTest, TraditionalMode_GPUMem_WithinTolerance)
{
    if (perf_ref::GPU_MEM_TRADITIONAL_BYTES == 0) {
        GTEST_SKIP() << "perf_reference.h not yet filled in";
    }

    // Traditional mode allocates the offscreen RT on first use (initOffscreenRT called in SetUp).
    renderOneFrame(/*directMode=*/false);
    metrics.updateGPUMem(renderer.getAllocator());
    uint64_t used = metrics.gpuAllocatedBytes();
    uint64_t limit = static_cast<uint64_t>(
        perf_ref::GPU_MEM_TRADITIONAL_BYTES * (1.0 + perf_ref::MEM_TOLERANCE));

    EXPECT_LE(used, limit)
        << "Traditional mode GPU mem " << used << " B exceeds reference "
        << perf_ref::GPU_MEM_TRADITIONAL_BYTES << " B (+" << perf_ref::MEM_TOLERANCE * 100 << "%)";
}

// ---------------------------------------------------------------------------
// Memory stability test — render 300 frames and assert GPU memory does not
// grow between frame 60 and frame 300.  A leak would cause VMA allocations to
// accumulate across frames; transient-per-frame allocations must be freed or
// re-used each frame.  The 5% tolerance absorbs VMA internal bookkeeping jitter.
// ---------------------------------------------------------------------------

TEST_F(PerfTest, MemoryStable_After300Frames_DirectMode)
{
    Metrics m;

    // Warm-up: let VMA reach a stable working set.
    for (int i = 0; i < 60; ++i) renderOneFrame(/*directMode=*/true);
    m.updateGPUMem(renderer.getAllocator());
    uint64_t memAfterWarmup = m.gpuAllocatedBytes();

    // Extended run: 240 additional frames (total 300).
    for (int i = 0; i < 240; ++i) renderOneFrame(/*directMode=*/true);
    m.updateGPUMem(renderer.getAllocator());
    uint64_t memAfterExtended = m.gpuAllocatedBytes();

    // Memory must not have grown by more than 5% after warmup stabilises.
    uint64_t limit = static_cast<uint64_t>(static_cast<double>(memAfterWarmup) * 1.05);
    EXPECT_LE(memAfterExtended, limit)
        << "GPU memory grew from " << memAfterWarmup << " B (after 60 frames) to "
        << memAfterExtended << " B (after 300 frames), suggesting a per-frame leak";
}

// ---------------------------------------------------------------------------
// Memory stability test — traditional mode mirrors the direct-mode test above.
// The offscreen UI RT is allocated on the first frame and must be reused (not
// reallocated) on every subsequent frame.  A leak here would typically indicate
// that the RT or its associated resources are being recreated per-frame.
// The 5% tolerance absorbs VMA internal bookkeeping jitter.
// ---------------------------------------------------------------------------

TEST_F(PerfTest, MemoryStable_After300Frames_TraditionalMode)
{
    Metrics m;

    // Warm-up: the offscreen RT is allocated on the first traditional-mode frame.
    // After ~60 frames the working set should be stable.
    for (int i = 0; i < 60; ++i) renderOneFrame(/*directMode=*/false);
    m.updateGPUMem(renderer.getAllocator());
    uint64_t memAfterWarmup = m.gpuAllocatedBytes();

    // Extended run: 240 additional frames (total 300).
    for (int i = 0; i < 240; ++i) renderOneFrame(/*directMode=*/false);
    m.updateGPUMem(renderer.getAllocator());
    uint64_t memAfterExtended = m.gpuAllocatedBytes();

    // Memory must not have grown by more than 5% after warmup stabilises.
    uint64_t limit = static_cast<uint64_t>(static_cast<double>(memAfterWarmup) * 1.05);
    EXPECT_LE(memAfterExtended, limit)
        << "GPU memory grew from " << memAfterWarmup << " B (after 60 frames) to "
        << memAfterExtended << " B (after 300 frames) in traditional mode, "
        << "suggesting a per-frame RT or resource leak";
}
