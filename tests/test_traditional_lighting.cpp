#include <gtest/gtest.h>
#include "renderer.h"
#include "scene.h"
#include "ui_surface.h"
#include "ui_system.h"
#include "vk_utils.h"
#include "render_helpers.h"

#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>
#include <cmath>
#include <array>

#include "scene_ubo_helper.h"

// ---------------------------------------------------------------------------
// TraditionalLightingTest — validates that composite.frag (traditional mode)
// applies the same world lighting model as surface.frag (direct mode).
//
// This test uses PRODUCTION shaders (no UI_TEST_COLOR) so composite.frag is
// exercised with real lighting, not the bypass path.  It is compiled as part
// of tests_sdf which links against production shaders.
//
// Bug: composite.frag did not apply NdotL or spotlight-cone attenuation.
// Faces facing away from the light appeared identically bright to faces
// facing toward it, because shadow*lightColor was added unconditionally.
//
// After the fix both modes share the same world lighting model:
//   ambient + shadow * NdotL * spotFactor * lightColor
//
// Setup:
//   Spotlight at (0, 2.8, 0.5) pointing toward (0, -1.3, -3.5).
//   A horizontal quad at y=1.0, z=-2.0 is inside the spotlight cone.
//
//   +Y normal (facing the light): NdotL > 0 → significantly lit
//   -Y normal (facing away):      NdotL = 0 → ambient only
// ---------------------------------------------------------------------------

static constexpr uint32_t TRAD_FB_WIDTH  = 640;
static constexpr uint32_t TRAD_FB_HEIGHT = 360;

class TraditionalLightingTest : public ::testing::Test {
protected:
    Renderer renderer;
    Scene    scene;

    VkImage       atlasImg{VK_NULL_HANDLE};
    VmaAllocation atlasAlloc{};
    VkImageView   atlasView{VK_NULL_HANDLE};
    VkSampler     atlasSampler{VK_NULL_HANDLE};

    VkBuffer      uiVtxBuf{VK_NULL_HANDLE};
    VmaAllocation uiVtxAlloc{};
    static constexpr uint32_t UI_VTX_COUNT = 6;

    HeadlessRenderTarget hrt{};

    void SetUp() override {
        // Production shaders: renderer.init with no shader dir override.
        ASSERT_TRUE(renderer.init(/*headless=*/true))
            << "Headless renderer init failed";
        scene.init();
        ASSERT_TRUE(renderer.uploadSceneGeometry(scene));

        // Fully-transparent 1x1 atlas using shared helper.
        render_helpers::createDummyAtlas(renderer.getDevice(), renderer.getAllocator(),
                                         renderer.getCommandPool(), renderer.getGraphicsQueue(),
                                         atlasImg, atlasAlloc, atlasView, atlasSampler);
        renderer.bindAtlasDescriptor(atlasView, atlasSampler);

        ASSERT_TRUE(renderer.initOffscreenRT());

        // Full-canvas UI quad using shared helper.
        render_helpers::createUIVertexBuffer(renderer.getAllocator(), uiVtxBuf, uiVtxAlloc);

        ASSERT_TRUE(renderer.createHeadlessRT(TRAD_FB_WIDTH, TRAD_FB_HEIGHT, hrt));
    }

    void TearDown() override {
        if (renderer.getDevice() != VK_NULL_HANDLE)
            vkDeviceWaitIdle(renderer.getDevice());
        renderer.destroyHeadlessRT(hrt);
        render_helpers::destroyAtlas(renderer.getDevice(), renderer.getAllocator(),
                                     atlasImg, atlasAlloc, atlasView, atlasSampler);
        render_helpers::destroyBuffer(renderer.getAllocator(), uiVtxBuf, uiVtxAlloc);
        renderer.cleanup();
    }

    // Render one frame in TRADITIONAL mode and return a flat RGBA pixel buffer.
    // uiOrtho = identity (default) → UI verts render off-screen → RT stays transparent
    // → composite.frag outputs teal * lit, letting us measure surface brightness.
    std::vector<uint8_t> renderTraditional(const glm::mat4& uiOrtho = glm::mat4(1.0f))
    {
        const VkDeviceSize readbackSize =
            static_cast<VkDeviceSize>(TRAD_FB_WIDTH) * TRAD_FB_HEIGHT * 4;

        VkBuffer      readbackBuf   = VK_NULL_HANDLE;
        VmaAllocation readbackAlloc = VK_NULL_HANDLE;
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
        renderer.recordUIRTPass(cmd, uiVtxBuf, UI_VTX_COUNT, uiOrtho);
        renderer.recordMainPass(cmd, hrt.rt, /*directMode=*/false, uiVtxBuf, UI_VTX_COUNT);

        vku::imageBarrier(cmd, hrt.rt.image,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkBufferImageCopy copyRegion{};
        copyRegion.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.imageExtent       = {TRAD_FB_WIDTH, TRAD_FB_HEIGHT, 1};
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

    // Build 6 identical horizontal face-corner arrays.
    // normalSign = +1 → +Y outward normal; -1 → -Y outward normal.
    // Quad spans x=[-1,1], z=[-1.5,-2.5], placed at height y.
    static std::array<std::array<glm::vec3, 4>, 6>
    makeHorizontalFaces(float y, float normalSign)
    {
        std::array<std::array<glm::vec3, 4>, 6> faces;
        for (int i = 0; i < 6; ++i) {
            if (normalSign > 0.0f) {
                // +Y normal: eu = +X, ev = -Z  → cross(eu,ev) = +Y
                faces[i][0] = {-1.0f, y, -1.5f};
                faces[i][1] = { 1.0f, y, -1.5f};
                faces[i][2] = {-1.0f, y, -2.5f};
                faces[i][3] = { 1.0f, y, -2.5f};
            } else {
                // -Y normal: swap P_10/P_01 to flip winding → cross(eu,ev) = -Y
                faces[i][0] = {-1.0f, y, -1.5f};
                faces[i][1] = {-1.0f, y, -2.5f};
                faces[i][2] = { 1.0f, y, -1.5f};
                faces[i][3] = { 1.0f, y, -2.5f};
            }
        }
        return faces;
    }

    // Render the cube surface in traditional mode; return mean RGB brightness
    // of the centre pixel.  The RT is transparent so the output is teal * lit.
    uint8_t renderFaces(
        const std::array<std::array<glm::vec3, 4>, 6>& faceCorners,
        const glm::mat4& view, const glm::mat4& proj)
    {
        SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
        renderer.updateSceneUBO(sceneUBO);

        renderer.updateCubeSurface(faceCorners);

        // Shadow cube at high altitude so it doesn't cast shadows on the test quad.
        std::array<std::array<glm::vec3, 4>, 6> shadowCorners;
        for (auto& f : shadowCorners)
            f[0] = f[1] = f[2] = f[3] = glm::vec3(0.0f, 10.0f, 0.0f);
        renderer.updateUIShadowCube(shadowCorners);

        // SurfaceUBO for per-face direct-mode data (must be valid even in trad mode).
        glm::vec3 P00{-0.5f,  0.5f, -50.0f};
        glm::vec3 P10{ 0.5f,  0.5f, -50.0f};
        glm::vec3 P01{-0.5f, -0.5f, -50.0f};
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

        auto pixels = renderTraditional();

        const uint8_t* px = pixels.data() +
            (static_cast<size_t>(TRAD_FB_HEIGHT / 2) * TRAD_FB_WIDTH +
             TRAD_FB_WIDTH / 2) * 4;
        return static_cast<uint8_t>(
            (static_cast<uint32_t>(px[0]) + px[1] + px[2]) / 3);
    }

    // Render the cube surface in direct mode (no UI); return mean RGB brightness
    // of the centre pixel.  The output is teal * lit from surface.frag.
    uint8_t renderFacesDirect(
        const std::array<std::array<glm::vec3, 4>, 6>& faceCorners,
        const glm::mat4& view, const glm::mat4& proj)
    {
        SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
        renderer.updateSceneUBO(sceneUBO);

        renderer.updateCubeSurface(faceCorners);

        std::array<std::array<glm::vec3, 4>, 6> shadowCorners;
        for (auto& f : shadowCorners)
            f[0] = f[1] = f[2] = f[3] = glm::vec3(0.0f, 10.0f, 0.0f);
        renderer.updateUIShadowCube(shadowCorners);

        // SurfaceUBO placed off-screen so the UI draw (0 verts) is irrelevant.
        glm::vec3 P00{-0.5f,  0.5f, -50.0f};
        glm::vec3 P10{ 0.5f,  0.5f, -50.0f};
        glm::vec3 P01{-0.5f, -0.5f, -50.0f};
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

        const VkDeviceSize readbackSize =
            static_cast<VkDeviceSize>(TRAD_FB_WIDTH) * TRAD_FB_HEIGHT * 4;

        VkBuffer      readbackBuf   = VK_NULL_HANDLE;
        VmaAllocation readbackAlloc = VK_NULL_HANDLE;
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
        // directMode=true, no UI vertices — surface.frag renders teal*lit only.
        renderer.recordMainPass(cmd, hrt.rt, /*directMode=*/true,
                                VK_NULL_HANDLE, 0, 0.0f);

        vku::imageBarrier(cmd, hrt.rt.image,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkBufferImageCopy copyRegion{};
        copyRegion.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.imageExtent       = {TRAD_FB_WIDTH, TRAD_FB_HEIGHT, 1};
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

        const uint8_t* px = pixels.data() +
            (static_cast<size_t>(TRAD_FB_HEIGHT / 2) * TRAD_FB_WIDTH +
             TRAD_FB_WIDTH / 2) * 4;
        return static_cast<uint8_t>(
            (static_cast<uint32_t>(px[0]) + px[1] + px[2]) / 3);
    }

    // Camera: above and slightly in front of the test surface, looking down at (0,1,-2).
    static glm::mat4 makeTopView() {
        return glm::lookAt(glm::vec3(0.0f, 2.5f, -1.0f),
                           glm::vec3(0.0f, 1.0f, -2.0f),
                           glm::vec3(1.0f, 0.0f,  0.0f));
    }

    static glm::mat4 makeProj() {
        glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                          static_cast<float>(TRAD_FB_WIDTH) / TRAD_FB_HEIGHT,
                                          0.1f, 100.0f);
        proj[1][1] *= -1.0f;
        return proj;
    }
};

// ---------------------------------------------------------------------------
// Test 1: Traditional mode — upward-facing surface receives diffuse light
//
// A horizontal +Y surface inside the spotlight cone should be noticeably
// brighter than ambient-only.
//
// FAILS without fix: composite.frag ignores NdotL — shadow factor alone adds
// the full light colour regardless of face orientation, but the magnitude may
// still exceed ambient.  Paired with Tests 2 and 3 to be conclusive.
// ---------------------------------------------------------------------------
TEST_F(TraditionalLightingTest, UpwardFacingSurface_ReceivesDiffuseLight_Traditional)
{
    auto view  = makeTopView();
    auto proj  = makeProj();

    uint8_t brightness = renderFaces(makeHorizontalFaces(1.0f, +1.0f), view, proj);

    // Teal (0, 0.5, 0.5) × ambient (0.08, 0.08, 0.12): avg ≈ 8/255.
    // With NdotL ≈ 0.584 and spotlight inside cone, expect > 30/255.
    EXPECT_GT(brightness, 30)
        << "Traditional mode: upward-facing surface should receive diffuse light "
           "from the spotlight. Expected > 30/255, got "
        << static_cast<int>(brightness);
}

// ---------------------------------------------------------------------------
// Test 2: Traditional mode — downward-facing surface receives only ambient
//
// A horizontal -Y surface has NdotL = 0 with the spotlight above; it should
// show only ambient lighting.
//
// FAILS without fix: composite.frag adds shadow * lightColor unconditionally.
// For a -Y face inside the light frustum, shadow ≈ 1.0, so lit = ambient +
// lightColor → brightness >> ambient.  With the fix (NdotL applied), NdotL =
// 0 clamps the diffuse term and brightness stays near ambient.
// ---------------------------------------------------------------------------
TEST_F(TraditionalLightingTest, DownwardFacingSurface_OnlyAmbient_Traditional)
{
    auto view  = makeTopView();
    auto proj  = makeProj();

    uint8_t brightness = renderFaces(makeHorizontalFaces(1.0f, -1.0f), view, proj);

    // With NdotL = 0, only ambient light reaches the surface.
    // Teal × ambient ≈ avg(0, 0.04, 0.06) ≈ 8/255.
    // Allow up to 30/255 for shadow-map noise and PCF blending.
    EXPECT_LT(brightness, 30)
        << "Traditional mode: downward-facing surface should receive only ambient "
           "lighting (NdotL = 0). Expected < 30/255, got "
        << static_cast<int>(brightness)
        << ". Indicates composite.frag does not apply NdotL.";
}

// ---------------------------------------------------------------------------
// Test 3: Traditional mode — upward face significantly brighter than downward
//
// Primary regression guard.  Without NdotL both faces are equally (brightly)
// lit and the difference is near zero; the test fails.
// ---------------------------------------------------------------------------
TEST_F(TraditionalLightingTest, NdotL_TopFaceBrighterThanBottomFace_Traditional)
{
    auto view = makeTopView();
    auto proj = makeProj();

    uint8_t topBrightness = renderFaces(makeHorizontalFaces(1.0f, +1.0f), view, proj);
    uint8_t botBrightness = renderFaces(makeHorizontalFaces(1.0f, -1.0f), view, proj);

    EXPECT_GT(topBrightness, botBrightness + 20)
        << "Traditional mode: upward face (" << static_cast<int>(topBrightness)
        << ") should be significantly brighter than downward face ("
        << static_cast<int>(botBrightness)
        << "). Difference must exceed 20/255. "
           "Equal brightness indicates NdotL is not applied in composite.frag.";
}

// ---------------------------------------------------------------------------
// Test 4: Direct-vs-traditional lighting parity
//
// The core claim: both rendering modes produce identical lighting for the same
// surface with the same surfaceNormal/vertex normal.
//
// surface.frag (direct):    outColor = vec4(teal * lit, 1.0)
// composite.frag (trad, no UI): base = teal*(1-0)+0*0 = teal → vec4(teal*lit, 1.0)
//
// Center-pixel brightness must be within 5/255 between the two modes.
// A large difference would indicate a lighting model mismatch between shaders.
// ---------------------------------------------------------------------------
TEST_F(TraditionalLightingTest, DirectVsTraditional_LightingParity_SameSurface)
{
    auto view  = makeTopView();
    auto proj  = makeProj();
    auto faces = makeHorizontalFaces(1.0f, +1.0f);

    uint8_t brightnessTrad   = renderFaces(faces, view, proj);
    uint8_t brightnessDirect = renderFacesDirect(faces, view, proj);

    EXPECT_NEAR(static_cast<int>(brightnessDirect), static_cast<int>(brightnessTrad), 5)
        << "Direct mode brightness (" << static_cast<int>(brightnessDirect)
        << ") differs from traditional mode (" << static_cast<int>(brightnessTrad)
        << ") by more than 5/255. Both modes should produce identical lighting "
           "for the same surface normal and spotlight configuration.";
}
