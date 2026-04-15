#include <gtest/gtest.h>
#include "renderer.h"
#include "scene.h"
#include "ui_surface.h"
#include "ui_system.h"
#include "metrics.h"
#include "vk_utils.h"
#include "perf_reference.h"
#include "scene_ubo_helper.h"
#include "render_helpers.h"

#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>
#include <cmath>

// Number of headless frames to render for each measurement.
static constexpr int PERF_FRAME_COUNT = 60;

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
        render_helpers::createDummyAtlas(renderer.getDevice(), renderer.getAllocator(),
                                         renderer.getCommandPool(), renderer.getGraphicsQueue(),
                                         dummyImg, dummyAlloc, dummyView, dummySampler);
        renderer.bindAtlasDescriptor(dummyView, dummySampler);

        // Ensure offscreen RT descriptor is valid (needed even in direct mode to satisfy set 2).
        ASSERT_TRUE(renderer.initOffscreenRT());

        // UI vertex buffer: one full-canvas quad.
        uiVtxCount = UI_VTX_COUNT;
        render_helpers::createUIVertexBuffer(renderer.getAllocator(), uiVtxBuf, uiVtxAlloc);

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

        SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
        sceneUBO.lightIntensity = 1.0f;  // Ensure directional light is active
        renderer.updateSceneUBO(sceneUBO);

        glm::vec3 P00{-0.5f,  0.5f, 0.0f};
        glm::vec3 P10{ 0.5f,  0.5f, 0.0f};
        glm::vec3 P01{-0.5f, -0.5f, 0.0f};

        auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                                   static_cast<float>(W_UI),
                                                   static_cast<float>(H_UI),
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
            glm::mat4 uiOrtho = glm::ortho(0.0f, static_cast<float>(W_UI),
                                           static_cast<float>(H_UI), 0.0f,
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
