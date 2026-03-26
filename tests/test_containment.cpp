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
        glm::vec2 a = quad[i];
        glm::vec2 b = quad[(i + 1) % 4];
        glm::vec2 edge = b - a;
        glm::vec2 perp = {-edge.y, edge.x};  // inward normal (CCW winding)
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

// ---------------------------------------------------------------------------
// UI Containment Test
// ---------------------------------------------------------------------------

class ContainmentTest : public ::testing::Test {
protected:
    static constexpr uint32_t FB_WIDTH  = 1280;
    static constexpr uint32_t FB_HEIGHT = 720;

    Renderer renderer;
    Scene    scene;

    void SetUp() override {
        ASSERT_TRUE(renderer.init(/*headless=*/true))
            << "Headless renderer init failed";
        scene.init();
    }

    void TearDown() override {
        renderer.cleanup();
    }
};

TEST_F(ContainmentTest, DirectMode_MagentaPixels_InsideSurfaceQuad)
{
    // Static surface — no animation, fixed position for deterministic test.
    glm::vec3 P00{-0.5f,  0.5f, 0.0f};
    glm::vec3 P10{ 0.5f,  0.5f, 0.0f};
    glm::vec3 P01{-0.5f, -0.5f, 0.0f};
    glm::vec3 P11{ 0.5f, -0.5f, 0.0f};

    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;  // Vulkan Y-flip
    glm::mat4 vp = proj * view;

    // Upload room geometry.
    ASSERT_TRUE(renderer.uploadSceneGeometry(scene));

    // Create a 1x1 dummy atlas image (UI_TEST_COLOR overrides the sample, but
    // the descriptor must be valid for validation layers).
    VkImage       dummyImg   = VK_NULL_HANDLE;
    VmaAllocation dummyAlloc = VK_NULL_HANDLE;
    VkImageView   dummyView  = VK_NULL_HANDLE;
    VkSampler     dummySampler = VK_NULL_HANDLE;
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

        // Transition to SHADER_READ_ONLY_OPTIMAL.
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

    // Ensure the offscreen RT descriptor (set 2 binding 1) is valid.
    ASSERT_TRUE(renderer.initOffscreenRT());

    // Create a simple UI vertex buffer: one quad covering the full canvas.
    // With UI_TEST_COLOR defined in the test shaders, every rendered UI pixel
    // is solid magenta regardless of UVs.
    VkBuffer      uiVtxBuf   = VK_NULL_HANDLE;
    VmaAllocation uiVtxAlloc = VK_NULL_HANDLE;
    constexpr uint32_t UI_VTX_COUNT = 6;
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

    // Compute surface transforms and clip planes.
    auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                               static_cast<float>(Renderer::W_UI),
                                               static_cast<float>(Renderer::H_UI),
                                               vp);
    auto clipPlanes = computeClipPlanes(P00, P10, P01);

    SceneUBO sceneUBO{};
    sceneUBO.view         = view;
    sceneUBO.proj         = proj;
    sceneUBO.lightViewProj = scene.lightViewProj();
    sceneUBO.lightDir     = glm::vec4(scene.light().direction, 0.0f);
    sceneUBO.lightColor   = glm::vec4(scene.light().color,     1.0f);
    sceneUBO.ambientColor = glm::vec4(scene.light().ambient,   1.0f);
    renderer.updateSceneUBO(sceneUBO);

    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = transforms.M_total;
    surfaceUBO.worldMatrix = transforms.M_world;
    for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
    surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
    renderer.updateSurfaceUBO(surfaceUBO);

    // Create the headless render target.
    HeadlessRenderTarget hrt{};
    ASSERT_TRUE(renderer.createHeadlessRT(FB_WIDTH, FB_HEIGHT, hrt));

    // Allocate a CPU-visible readback buffer.
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
        ASSERT_EQ(vmaCreateBuffer(renderer.getAllocator(), &bci, &ai,
                                  &readbackBuf, &readbackAlloc, nullptr), VK_SUCCESS);
    }

    // Record commands: shadow pass + main pass (direct mode) + readback copy.
    VkCommandBufferAllocateInfo cbAI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbAI.commandPool        = renderer.getCommandPool();
    cbAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAI.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    ASSERT_EQ(vkAllocateCommandBuffers(renderer.getDevice(), &cbAI, &cmd), VK_SUCCESS);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    renderer.recordShadowPass(cmd);
    renderer.recordMainPass(cmd, hrt.rt, /*directMode=*/true, uiVtxBuf, UI_VTX_COUNT);

    // After the main pass, resolve image is in COLOR_ATTACHMENT_OPTIMAL.
    // Transition to TRANSFER_SRC_OPTIMAL for the copy.
    vku::imageBarrier(cmd, hrt.rt.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset      = 0;
    copyRegion.bufferRowLength   = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.imageOffset       = {0, 0, 0};
    copyRegion.imageExtent       = {FB_WIDTH, FB_HEIGHT, 1};
    vkCmdCopyImageToBuffer(cmd, hrt.rt.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readbackBuf, 1, &copyRegion);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;
    ASSERT_EQ(vkQueueSubmit(renderer.getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE), VK_SUCCESS);
    vkQueueWaitIdle(renderer.getGraphicsQueue());

    // Map and copy pixels to CPU.
    void* mapped = nullptr;
    vmaMapMemory(renderer.getAllocator(), readbackAlloc, &mapped);
    std::vector<uint8_t> pixels(readbackSize);
    memcpy(pixels.data(), mapped, static_cast<size_t>(readbackSize));
    vmaUnmapMemory(renderer.getAllocator(), readbackAlloc);

    // Cleanup temporary resources before assertions (so TearDown is clean).
    vkFreeCommandBuffers(renderer.getDevice(), renderer.getCommandPool(), 1, &cmd);
    vmaDestroyBuffer(renderer.getAllocator(), readbackBuf, readbackAlloc);
    vmaDestroyBuffer(renderer.getAllocator(), uiVtxBuf,   uiVtxAlloc);
    vkDestroySampler(renderer.getDevice(), dummySampler, nullptr);
    vkDestroyImageView(renderer.getDevice(), dummyView, nullptr);
    vmaDestroyImage(renderer.getAllocator(), dummyImg, dummyAlloc);
    renderer.destroyHeadlessRT(hrt);

    // Project surface corners to screen space.
    glm::vec2 screenCorners[4] = {
        projectToScreen(P00, vp, FB_WIDTH, FB_HEIGHT),
        projectToScreen(P10, vp, FB_WIDTH, FB_HEIGHT),
        projectToScreen(P11, vp, FB_WIDTH, FB_HEIGHT),
        projectToScreen(P01, vp, FB_WIDTH, FB_HEIGHT),
    };

    // Scan every pixel; any magenta pixel must be inside the projected quad.
    int violations = 0;
    for (uint32_t y = 0; y < FB_HEIGHT; ++y) {
        for (uint32_t x = 0; x < FB_WIDTH; ++x) {
            const uint8_t* px = pixels.data() + (y * FB_WIDTH + x) * 4;
            if (isMagenta(px[0], px[1], px[2])) {
                glm::vec2 coord{static_cast<float>(x), static_cast<float>(y)};
                if (!insideConvexQuad(coord, screenCorners, /*margin=*/2.0f)) {
                    ++violations;
                }
            }
        }
    }

    EXPECT_EQ(violations, 0)
        << violations << " magenta pixel(s) found outside the surface quad boundary";
}
