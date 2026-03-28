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

// ---------------------------------------------------------------------------
// Test 1: Direct mode — existing test, now uses shared fixture setup.
// ---------------------------------------------------------------------------

TEST_F(ContainmentTest, DirectMode_MagentaPixels_InsideSurfaceQuad)
{
    glm::vec3 P00{-0.5f,  0.5f, 0.0f};
    glm::vec3 P10{ 0.5f,  0.5f, 0.0f};
    glm::vec3 P01{-0.5f, -0.5f, 0.0f};
    glm::vec3 P11{ 0.5f, -0.5f, 0.0f};

    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    SceneUBO sceneUBO{};
    sceneUBO.view          = view;
    sceneUBO.proj          = proj;
    sceneUBO.lightViewProj = scene.lightViewProj();
    sceneUBO.lightDir      = glm::vec4(scene.light().direction, 0.0f);
    sceneUBO.lightColor    = glm::vec4(scene.light().color,     1.0f);
    sceneUBO.ambientColor  = glm::vec4(scene.light().ambient,   1.0f);
    renderer.updateSceneUBO(sceneUBO);

    auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                               static_cast<float>(Renderer::W_UI),
                                               static_cast<float>(Renderer::H_UI), vp);
    auto clipPlanes = computeClipPlanes(P00, P10, P01);

    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = transforms.M_total;
    surfaceUBO.worldMatrix = transforms.M_world;
    for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
    surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
    renderer.updateSurfaceUBO(surfaceUBO);

    auto pixels = renderAndReadback(/*directMode=*/true);
    assertMagentaContained(pixels, vp, P00, P10, P11, P01);
}

// ---------------------------------------------------------------------------
// Test 2: Traditional mode — composited UI RT must also be contained in quad.
//
// The UI is first rendered to the offscreen RT (recordUIRTPass, UI_TEST_COLOR →
// solid magenta), then composited onto the teal surface quad in recordMainPass.
// Because compositing uses pre-multiplied alpha and the test color has alpha=1,
// the composited output is solid magenta where the quad falls on screen.
// The test verifies those magenta pixels lie within the projected quad boundary.
// ---------------------------------------------------------------------------

TEST_F(ContainmentTest, TraditionalMode_MagentaPixels_InsideSurfaceQuad)
{
    glm::vec3 P00{-0.5f,  0.5f, 0.0f};
    glm::vec3 P10{ 0.5f,  0.5f, 0.0f};
    glm::vec3 P01{-0.5f, -0.5f, 0.0f};
    glm::vec3 P11{ 0.5f, -0.5f, 0.0f};

    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    SceneUBO sceneUBO{};
    sceneUBO.view          = view;
    sceneUBO.proj          = proj;
    sceneUBO.lightViewProj = scene.lightViewProj();
    sceneUBO.lightDir      = glm::vec4(scene.light().direction, 0.0f);
    sceneUBO.lightColor    = glm::vec4(scene.light().color,     1.0f);
    sceneUBO.ambientColor  = glm::vec4(scene.light().ambient,   1.0f);
    renderer.updateSceneUBO(sceneUBO);

    auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                               static_cast<float>(Renderer::W_UI),
                                               static_cast<float>(Renderer::H_UI), vp);
    auto clipPlanes = computeClipPlanes(P00, P10, P01);

    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = transforms.M_total;
    surfaceUBO.worldMatrix = transforms.M_world;
    for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
    surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
    renderer.updateSurfaceUBO(surfaceUBO);

    // Position the surface quad so the compositor draws the UI at the correct location.
    renderer.updateSurfaceQuad(P00, P10, P01, P11);

    glm::mat4 uiOrtho = glm::ortho(0.0f, static_cast<float>(Renderer::W_UI),
                                   static_cast<float>(Renderer::H_UI), 0.0f,
                                   -1.0f, 1.0f);

    auto pixels = renderAndReadback(/*directMode=*/false, uiOrtho);

    // Ensure the composited UI RT actually produced magenta pixels in the
    // readback — a vacuous containment pass would occur if the surface quad
    // were off-screen or misconfigured and no magenta pixels were present.
    int magentaCount = countMagentaPixels(pixels);
    EXPECT_GT(magentaCount, 0)
        << "No magenta pixels found in traditional-mode readback — "
        << "surface quad may be off-screen or the composite pass is not executing";

    assertMagentaContained(pixels, vp, P00, P10, P11, P01);
}

// ---------------------------------------------------------------------------
// Test 3: Extreme angle — camera nearly edge-on to the UI surface.
//
// The camera is placed at (2, 0, 0.15) looking at the origin, so the surface
// (in the XY plane, normal = +Z) is viewed at ~85° from face-on.  The projected
// quad is a very thin sliver.  The clip planes in M_total must still correctly
// prevent UI pixels from bleeding outside that sliver.
// ---------------------------------------------------------------------------

TEST_F(ContainmentTest, ExtremeAngle_DirectMode_MagentaPixels_InsideSurfaceQuad)
{
    glm::vec3 P00{-0.5f,  0.5f, 0.0f};
    glm::vec3 P10{ 0.5f,  0.5f, 0.0f};
    glm::vec3 P01{-0.5f, -0.5f, 0.0f};
    glm::vec3 P11{ 0.5f, -0.5f, 0.0f};

    // Camera ~85° from surface normal (nearly edge-on).
    // Placed along +X with a small Z offset so the surface isn't degenerate on screen.
    glm::mat4 view = glm::lookAt(glm::vec3(2.0f, 0.0f, 0.15f),
                                 glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    SceneUBO sceneUBO{};
    sceneUBO.view          = view;
    sceneUBO.proj          = proj;
    sceneUBO.lightViewProj = scene.lightViewProj();
    sceneUBO.lightDir      = glm::vec4(scene.light().direction, 0.0f);
    sceneUBO.lightColor    = glm::vec4(scene.light().color,     1.0f);
    sceneUBO.ambientColor  = glm::vec4(scene.light().ambient,   1.0f);
    renderer.updateSceneUBO(sceneUBO);

    auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                               static_cast<float>(Renderer::W_UI),
                                               static_cast<float>(Renderer::H_UI), vp);
    auto clipPlanes = computeClipPlanes(P00, P10, P01);

    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = transforms.M_total;
    surfaceUBO.worldMatrix = transforms.M_world;
    for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
    surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
    renderer.updateSurfaceUBO(surfaceUBO);

    // Use a larger margin (4 px) for the edge-on case: the thin projected sliver
    // amplifies MSAA edge-blending effects relative to a face-on view.
    auto pixels = renderAndReadback(/*directMode=*/true);

    // Ensure the test is non-vacuous: at least one magenta pixel must be present.
    // In the extreme angle case, the projected sliver may be very thin, so we need
    // to verify that magenta pixels were actually rendered.
    EXPECT_GT(countMagentaPixels(pixels), 0)
        << "No magenta pixels found in extreme-angle direct-mode readback — "
        << "surface may be off-screen or the direct-mode pass is not executing";

    assertMagentaContained(pixels, vp, P00, P10, P11, P01, /*margin=*/4.0f);
}

// ---------------------------------------------------------------------------
// Test 4: Multi-frame animation — direct mode follows the animated surface.
//
// The Scene animates the UI surface with M_anim(t).  This test renders three
// separate frames (t=0, t=1, t=2), recomputing worldCorners() and the full
// transform chain each time.  For each frame it asserts:
//   (a) at least one magenta pixel is present (surface is on-screen),
//   (b) all magenta pixels lie within the screen-space projection of the
//       updated surface quad (clip planes track the moving surface).
// ---------------------------------------------------------------------------

TEST_F(ContainmentTest, DirectMode_AnimationFrames_MagentaContained)
{
    // Camera positioned inside the room (z < 3) looking toward the back wall
    // where the animated surface oscillates near z=-2.5, y≈1.5.
    // Using z=2.5 keeps the camera inside the room so the front wall (z=3)
    // does not occlude the surface.
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.5f, 2.5f),
                                 glm::vec3(0.0f, 1.5f, -2.5f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    SceneUBO sceneUBO{};
    sceneUBO.view          = view;
    sceneUBO.proj          = proj;
    sceneUBO.lightViewProj = scene.lightViewProj();
    sceneUBO.lightDir      = glm::vec4(scene.light().direction, 0.0f);
    sceneUBO.lightColor    = glm::vec4(scene.light().color,     1.0f);
    sceneUBO.ambientColor  = glm::vec4(scene.light().ambient,   1.0f);
    renderer.updateSceneUBO(sceneUBO);

    for (float t : {0.0f, 1.0f, 2.0f}) {
        SCOPED_TRACE("t=" + std::to_string(t));

        glm::vec3 P00, P10, P01, P11;
        scene.worldCorners(t, P00, P10, P01, P11);

        auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                                   static_cast<float>(Renderer::W_UI),
                                                   static_cast<float>(Renderer::H_UI), vp);
        auto clipPlanes = computeClipPlanes(P00, P10, P01);

        SurfaceUBO surfaceUBO{};
        surfaceUBO.totalMatrix = transforms.M_total;
        surfaceUBO.worldMatrix = transforms.M_world;
        for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
        surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
        renderer.updateSurfaceUBO(surfaceUBO);

        auto pixels = renderAndReadback(/*directMode=*/true);

        // (a) Non-vacuous: at least one magenta pixel must be present.
        EXPECT_GT(countMagentaPixels(pixels), 0)
            << "No magenta pixels at t=" << t
            << " — surface may be off-screen or M_total is degenerate";

        // (b) All magenta pixels must lie within the projected quad boundary.
        assertMagentaContained(pixels, vp, P00, P10, P11, P01);
    }
}

// ---------------------------------------------------------------------------
// Test 5: Back wall not self-shadowed
//
// Position the camera facing the back wall (Z = -D), render one frame, and
// read back the center region of the back wall.  Since the directional light
// has direction (-0.5, -1.0, -0.5) and the back wall normal is (0, 0, 1),
// the dot product N·L = 0 — the wall receives zero diffuse illumination.
// Without a working depth bias, the back wall would be entirely shadowed
// (acne) and appear black.  This test asserts that the mean luminance of
// the back-wall region exceeds ambientColor + 0.1, proving that at least
// some diffuse light contribution reaches the wall (the depth bias prevents
// total self-shadowing).
// ---------------------------------------------------------------------------

TEST_F(ContainmentTest, BackWall_NotSelfShadowed)
{
    // Room dimensions (matching Scene::init())
    constexpr float W = 2.0f;   // half-width
    constexpr float H = 3.0f;   // full height
    constexpr float D = 3.0f;   // half-depth

    // Camera facing the back wall (Z = -D) from the front.
    // Position: in front of the back wall, looking at its center.
    glm::vec3 camPos{0.0f, H * 0.5f, D + 5.0f};   // (0, 1.5, 8)
    glm::vec3 camTarget{-W * 0.5f, H * 0.5f, -D};  // point on back wall
    glm::mat4 view = glm::lookAt(camPos, camTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    SceneUBO sceneUBO{};
    sceneUBO.view          = view;
    sceneUBO.proj          = proj;
    sceneUBO.lightViewProj = scene.lightViewProj();
    sceneUBO.lightDir      = glm::vec4(scene.light().direction, 0.0f);
    sceneUBO.lightColor    = glm::vec4(scene.light().color,     1.0f);
    sceneUBO.ambientColor  = glm::vec4(scene.light().ambient,   1.0f);
    renderer.updateSceneUBO(sceneUBO);

    auto pixels = renderAndReadback(/*directMode=*/true);

    // Define a center strip on the back wall in screen space.
    // The back wall spans most of the screen when viewed from this angle.
    // We sample a horizontal strip in the vertical center of the image.
    const int stripTop    = FB_HEIGHT / 3;
    const int stripBottom = 2 * FB_HEIGHT / 3;
    const int stripLeft   = FB_WIDTH / 4;
    const int stripRight  = 3 * FB_WIDTH / 4;

    // Compute mean luminance of the center strip.
    // Luminance = 0.299*R + 0.587*G + 0.114*B (standard NTSC weights).
    double sumLuminance = 0.0;
    int pixelCount = 0;
    for (int y = stripTop; y < stripBottom; ++y) {
        for (int x = stripLeft; x < stripRight; ++x) {
            const uint8_t* px = pixels.data() + (y * FB_WIDTH + x) * 4;
            float r = px[0] / 255.0f;
            float g = px[1] / 255.0f;
            float b = px[2] / 255.0f;
            float lum = 0.299f * r + 0.587f * g + 0.114f * b;
            sumLuminance += lum;
            ++pixelCount;
        }
    }

    EXPECT_GT(pixelCount, 0) << "No pixels sampled in back-wall region";

    double meanLuminance = sumLuminance / static_cast<double>(pixelCount);

    // The ambient color from scene.h is {0.15f, 0.15f, 0.2f}.
    // Its luminance contribution alone is:
    //   0.299*0.15 + 0.587*0.15 + 0.114*0.2 = 0.1557
    // The test asserts meanLuminance > ambientLuminance + 0.1, i.e.,
    // > 0.2557, proving that some diffuse light reaches the back wall
    // (the depth bias prevents total self-shadowing/acne).
    float ambientLuminance =
        0.299f * scene.light().ambient.r +
        0.587f * scene.light().ambient.g +
        0.114f * scene.light().ambient.b;
    float threshold = ambientLuminance + 0.1f;

    EXPECT_GT(meanLuminance, threshold)
        << "Back wall mean luminance=" << meanLuminance
        << " <= threshold=" << threshold
        << " (ambient luminance + 0.1); "
        << "the back wall appears too dark, indicating excessive self-shadowing "
        << "likely due to insufficient depth bias in the shadow map";
}

// ---------------------------------------------------------------------------
// Test 6: PCF shadow symmetry — centered {-0.5, 0.5} kernel produces no bias.
//
// This test validates that the 2×2 PCF kernel in room.frag produces symmetric
// results when sampling around a lit/shadow transition. The kernel uses offsets
// {-0.5, +0.5} texels which should be centered, producing no directional bias.
//
// We render a view that shows the room with visible lighting variation, then
// scan horizontally across a row that contains a lit/shadow transition. We find
// the midpoint (where luminance is halfway between the lit and shadow values),
// then sample equal distances on either side of this midpoint.
//
// For a centered kernel, the luminance difference from the midpoint should be
// symmetric: (litNear - midLum) ≈ (midLum - shadowNear), with ratio within ±10%.
// ---------------------------------------------------------------------------
TEST_F(ContainmentTest, PCFShadow_Symmetry_CenteredKernel)
{
    // Camera looking at the room from a position that shows visible lighting variation.
    // Position: outside the room, looking at the front wall corner.
    glm::mat4 view = glm::lookAt(glm::vec3(5.0f, 2.0f, 8.0f),
                                 glm::vec3(0.0f, 1.5f, 3.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    SceneUBO sceneUBO{};
    sceneUBO.view          = view;
    sceneUBO.proj          = proj;
    sceneUBO.lightViewProj = scene.lightViewProj();
    sceneUBO.lightDir      = glm::vec4(scene.light().direction, 0.0f);
    sceneUBO.lightColor    = glm::vec4(scene.light().color,     1.0f);
    sceneUBO.ambientColor  = glm::vec4(scene.light().ambient, 1.0f);
    renderer.updateSceneUBO(sceneUBO);

    // SurfaceUBO must be bound even though the room-only pass never reads it.
    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = glm::mat4(1.0f);
    surfaceUBO.worldMatrix = glm::mat4(1.0f);
    surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
    renderer.updateSurfaceUBO(surfaceUBO);

    auto pixels = renderAndReadback(/*directMode=*/true);

    auto lumAt = [&](int x, int y) -> float {
        const uint8_t* px = pixels.data() + (y * FB_WIDTH + x) * 4;
        return 0.299f * px[0] / 255.0f +
               0.587f * px[1] / 255.0f +
               0.114f * px[2] / 255.0f;
    };

    // Scan the centre row horizontally.
    const int row = FB_HEIGHT / 2;

    // Sample luminance at all columns.
    std::vector<float> luminance;
    for (int x = 0; x < FB_WIDTH; ++x) {
        luminance.push_back(lumAt(x, row));
    }

    // Find the minimum and maximum luminance.
    float minLum = *std::min_element(luminance.begin(), luminance.end());
    float maxLum = *std::max_element(luminance.begin(), luminance.end());

    // Verify we have visible lighting variation.
    ASSERT_GT(maxLum - minLum, 0.05f)
        << "No visible lighting variation in centre row (row=" << row << ")\n"
        << "  minLum=" << minLum << "  maxLum=" << maxLum;

    // Target luminance is the midpoint between min and max.
    const float targetLum = (minLum + maxLum) * 0.5f;

    // Find the column closest to the midpoint luminance.
    int edgeCol = FB_WIDTH / 2;
    float bestDiff = 1e9f;
    for (int x = 0; x < (int)FB_WIDTH; ++x) {
        float diff = std::abs(luminance[x] - targetLum);
        if (diff < bestDiff) {
            bestDiff = diff;
            edgeCol = x;
        }
    }

    // Sample k=20 pixels on each side of the edge — well outside the PCF penumbra.
    const int k = 20;
    ASSERT_GE(edgeCol - k, 0) << "Left sample out of bounds at edgeCol=" << edgeCol;
    ASSERT_LT(edgeCol + k, (int)FB_WIDTH) << "Right sample out of bounds at edgeCol=" << edgeCol;

    const float leftNear  = lumAt(edgeCol - k, row);
    const float rightNear = lumAt(edgeCol + k, row);
    const float edgeLum   = lumAt(edgeCol, row);

    // Determine which side is brighter.
    const float brighter  = std::max(leftNear, rightNear);
    const float darker    = std::min(leftNear, rightNear);

    EXPECT_GT(brighter, darker)
        << "Expected brightness variation: brighter=" << brighter
        << " darker=" << darker << " edgeCol=" << edgeCol;

    // Compute excess and deficit from the edge luminance.
    const float excess  = brighter - edgeLum;
    const float deficit = edgeLum - darker;

    ASSERT_GT(excess, 0.0f) << "excess must be positive: brighter=" << brighter << " edgeLum=" << edgeLum;
    ASSERT_GT(deficit, 0.0f) << "deficit must be positive: edgeLum=" << edgeLum << " darker=" << darker;

    // Symmetry assertion: excess ≈ deficit within ±10%.
    // This validates the {-0.5, +0.5} PCF kernel is centered.
    float ratio = excess / deficit;
    EXPECT_NEAR(ratio, 1.0f, 0.1f)
        << "PCF penumbra is asymmetric:\n"
        << "  excess=" << excess << "  deficit=" << deficit
        << "  ratio=" << ratio
        << " (expected 1.0 ± 0.1 for centred {-0.5, +0.5} kernel)\n"
        << "  edgeCol=" << edgeCol << "  edgeLum=" << edgeLum
        << "  brighter=" << brighter << "  darker=" << darker
        << "  targetLum=" << targetLum
        << "  minLum=" << minLum << "  maxLum=" << maxLum;
}

// ---------------------------------------------------------------------------
// Test 7: Non-uniform quad scale — clip planes track the reshaped surface.
//
// Uses Scene::worldCorners with scaleW=2.0, scaleH=0.5 to produce a surface
// that is twice as wide and half as tall as the default.  The clip planes are
// derived from the reshaped world corners.  The test verifies:
//   (a) at least one magenta pixel is present on screen (surface is visible),
//   (b) all magenta pixels lie within the screen-space projection of the
//       reshaped quad — i.e. the clip planes correctly reflect the new bounds.
// ---------------------------------------------------------------------------

TEST_F(ContainmentTest, NonUniformScale_DirectMode_ClipPlanesTrackReshapedSurface)
{
    constexpr float scaleW = 2.0f;
    constexpr float scaleH = 0.5f;

    // Camera positioned inside the room looking toward the back wall where
    // the animated surface sits at t=0 (near z=-2.5, y≈1.5).
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.5f, 2.5f),
                                 glm::vec3(0.0f, 1.5f, -2.5f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    SceneUBO sceneUBO{};
    sceneUBO.view          = view;
    sceneUBO.proj          = proj;
    sceneUBO.lightViewProj = scene.lightViewProj();
    sceneUBO.lightDir      = glm::vec4(scene.light().direction, 0.0f);
    sceneUBO.lightColor    = glm::vec4(scene.light().color,     1.0f);
    sceneUBO.ambientColor  = glm::vec4(scene.light().ambient,   1.0f);
    renderer.updateSceneUBO(sceneUBO);

    // Get world corners with non-uniform scale applied.
    glm::vec3 P00, P10, P01, P11;
    scene.worldCorners(0.0f, P00, P10, P01, P11, scaleW, scaleH);

    // Scale canvas dimensions proportionally so font size is invariant.
    // This causes H_UI * scaleH content to map exactly to the reshaped surface
    // height-wise, while W_UI * scaleW is the logical canvas width.
    auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                               static_cast<float>(Renderer::W_UI) * scaleW,
                                               static_cast<float>(Renderer::H_UI) * scaleH,
                                               vp);
    auto clipPlanes = computeClipPlanes(P00, P10, P01);

    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = transforms.M_total;
    surfaceUBO.worldMatrix = transforms.M_world;
    for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
    surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
    renderer.updateSurfaceUBO(surfaceUBO);

    auto pixels = renderAndReadback(/*directMode=*/true);

    // (a) Non-vacuous: at least one magenta pixel must be visible.
    EXPECT_GT(countMagentaPixels(pixels), 0)
        << "No magenta pixels found with scaleW=" << scaleW << " scaleH=" << scaleH
        << " — reshaped surface may be off-screen or M_total is degenerate";

    // (b) All magenta pixels must lie within the projected reshaped quad.
    assertMagentaContained(pixels, vp, P00, P10, P11, P01);
}

// ---------------------------------------------------------------------------
// Test 8: Font-size invariance across modes — scaled quad, sub-canvas rect.
//
// Render a surface with scaleW=0.5 (half-width) in both direct mode and
// traditional mode.  The UI vertex buffer covers only the LEFT HALF of UI
// space: x in [0, W_UI/2], y in [0, H_UI].  Both modes use the unscaled
// canvas dimensions W_UI × H_UI so:
//
//   Direct mode   : M_us maps x=[0,W_UI/2] → s=[0,0.5] — left half of surface.
//   Traditional   : ortho maps x=[0,W_UI/2] to left half of RT; the full RT
//                   is composited onto the surface quad → left half of surface.
//
// The left-half sub-canvas therefore projects to the same screen region in
// both modes.  The test asserts that the bounding boxes of magenta pixels
// agree within a 3-pixel tolerance, verifying font-size invariance.
// ---------------------------------------------------------------------------

TEST_F(ContainmentTest, FontSizeInvariance_DirectVsTraditional_ScaledQuad)
{
    constexpr float scaleW = 0.5f;
    constexpr float scaleH = 1.0f;

    // Camera inside room looking toward back wall (same camera as Test 7).
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.5f, 2.5f),
                                 glm::vec3(0.0f, 1.5f, -2.5f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    SceneUBO sceneUBO{};
    sceneUBO.view          = view;
    sceneUBO.proj          = proj;
    sceneUBO.lightViewProj = scene.lightViewProj();
    sceneUBO.lightDir      = glm::vec4(scene.light().direction, 0.0f);
    sceneUBO.lightColor    = glm::vec4(scene.light().color,     1.0f);
    sceneUBO.ambientColor  = glm::vec4(scene.light().ambient,   1.0f);
    renderer.updateSceneUBO(sceneUBO);

    // Surface corners with scaleW=0.5 at t=0.
    glm::vec3 P00, P10, P01, P11;
    scene.worldCorners(0.0f, P00, P10, P01, P11, scaleW, scaleH);

    // Both modes use the unscaled canvas (W_UI × H_UI).
    auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                               static_cast<float>(Renderer::W_UI),
                                               static_cast<float>(Renderer::H_UI), vp);
    auto clipPlanes = computeClipPlanes(P00, P10, P01);

    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = transforms.M_total;
    surfaceUBO.worldMatrix = transforms.M_world;
    for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
    surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
    renderer.updateSurfaceUBO(surfaceUBO);

    // Traditional mode needs the surface quad positioned correctly.
    renderer.updateSurfaceQuad(P00, P10, P01, P11);

    // Sub-canvas vertex buffer: a rect covering the LEFT HALF of UI space.
    // x in [0, W_UI/2], y in [0, H_UI] — two triangles, 6 vertices.
    const float halfW = static_cast<float>(Renderer::W_UI) * 0.5f;
    const float H     = static_cast<float>(Renderer::H_UI);
    UIVertex subVerts[UI_VTX_COUNT] = {
        {{0,     0}, {0.0f, 0.0f}}, {{halfW, 0}, {0.5f, 0.0f}}, {{halfW, H}, {0.5f, 1.0f}},
        {{0,     0}, {0.0f, 0.0f}}, {{halfW, H}, {0.5f, 1.0f}}, {{0,     H}, {0.0f, 1.0f}},
    };
    VkBuffer      subVtxBuf   = VK_NULL_HANDLE;
    VmaAllocation subVtxAlloc = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size        = sizeof(subVerts);
        bci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        ASSERT_EQ(vmaCreateBuffer(renderer.getAllocator(), &bci, &ai,
                                  &subVtxBuf, &subVtxAlloc, nullptr), VK_SUCCESS);
        void* mapped = nullptr;
        vmaMapMemory(renderer.getAllocator(), subVtxAlloc, &mapped);
        memcpy(mapped, subVerts, sizeof(subVerts));
        vmaUnmapMemory(renderer.getAllocator(), subVtxAlloc);
    }

    // Temporarily replace the fixture's vertex buffer with the sub-canvas one
    // so renderAndReadback records draws using the left-half rect.
    VkBuffer      savedBuf   = uiVtxBuf;
    VmaAllocation savedAlloc = uiVtxAlloc;
    uiVtxBuf   = subVtxBuf;
    uiVtxAlloc = subVtxAlloc;

    // Ortho matrix for traditional mode: full unscaled canvas.
    glm::mat4 uiOrtho = glm::ortho(0.0f, static_cast<float>(Renderer::W_UI),
                                   static_cast<float>(Renderer::H_UI), 0.0f,
                                   -1.0f, 1.0f);

    auto pixelsDirect = renderAndReadback(/*directMode=*/true);
    auto pixelsTrad   = renderAndReadback(/*directMode=*/false, uiOrtho);

    // Restore original full-canvas vertex buffer; destroy sub-canvas buffer.
    uiVtxBuf   = savedBuf;
    uiVtxAlloc = savedAlloc;
    vmaDestroyBuffer(renderer.getAllocator(), subVtxBuf, subVtxAlloc);

    // Compute bounding boxes of magenta pixels for each mode.
    auto bboxDirect = computeMagentaBBox(pixelsDirect, FB_WIDTH, FB_HEIGHT);
    auto bboxTrad   = computeMagentaBBox(pixelsTrad,   FB_WIDTH, FB_HEIGHT);

    ASSERT_TRUE(bboxDirect.valid)
        << "No magenta pixels in direct-mode render "
           "(scaleW=" << scaleW << " — surface may be off-screen)";
    ASSERT_TRUE(bboxTrad.valid)
        << "No magenta pixels in traditional-mode render "
           "(scaleW=" << scaleW << " — surface quad or composite pass misconfigured)";

    // Both modes must place the left-half sub-canvas at the same screen region.
    constexpr int kTol = 3;
    EXPECT_NEAR(bboxDirect.minX, bboxTrad.minX, kTol)
        << "Left edge mismatch: direct=" << bboxDirect.minX
        << " trad=" << bboxTrad.minX;
    EXPECT_NEAR(bboxDirect.maxX, bboxTrad.maxX, kTol)
        << "Right edge mismatch: direct=" << bboxDirect.maxX
        << " trad=" << bboxTrad.maxX;
    EXPECT_NEAR(bboxDirect.minY, bboxTrad.minY, kTol)
        << "Top edge mismatch: direct=" << bboxDirect.minY
        << " trad=" << bboxTrad.minY;
    EXPECT_NEAR(bboxDirect.maxY, bboxTrad.maxY, kTol)
        << "Bottom edge mismatch: direct=" << bboxDirect.maxY
        << " trad=" << bboxTrad.maxY;
}
