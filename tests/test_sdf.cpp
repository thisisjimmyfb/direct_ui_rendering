#include <gtest/gtest.h>
#include "sdf_render_fixture.h"
#include "renderer.h"
#include "scene.h"
#include "ui_surface.h"
#include "ui_system.h"
#include "vk_utils.h"
#include "scene_ubo_helper.h"
#include "render_helpers.h"

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

class SDFThresholdTest : public SDFRenderFixture {
protected:
    VkImage       atlasImg{VK_NULL_HANDLE};
    VmaAllocation atlasAlloc{};
    VkImageView   atlasView{VK_NULL_HANDLE};
    VkSampler     atlasSampler{VK_NULL_HANDLE};

    void SetUp() override {
        ASSERT_TRUE(renderer.init(/*headless=*/true))
            << "Headless renderer init failed";
        scene.init();
        ASSERT_TRUE(renderer.uploadSceneGeometry(scene));

        render_helpers::createAtlas(renderer.getDevice(), renderer.getAllocator(),
                                    renderer.getCommandPool(), renderer.getGraphicsQueue(),
                                    ATLAS_DIM, 51, 51, 51, 200,
                                    atlasImg, atlasAlloc, atlasView, atlasSampler);
        renderer.bindAtlasDescriptor(atlasView, atlasSampler);

        ASSERT_TRUE(renderer.initOffscreenRT());

        render_helpers::createUIVertexBuffer(renderer.getAllocator(), uiVtxBuf, uiVtxAlloc);

        ASSERT_TRUE(renderer.createHeadlessRT(FB_WIDTH, FB_HEIGHT, hrt));

        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                          static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                          0.1f, 100.0f);
        proj[1][1] *= -1.0f;

        SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
        sceneUBO.lightIntensity = 1.0f;
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
        glm::vec3 eu = m_P10 - m_P00;
        glm::vec3 ev = m_P01 - m_P00;
        surfaceUBO.surfaceNormal = glm::vec4(glm::normalize(glm::cross(eu, ev)), 0.0f);
        renderer.updateSurfaceUBO(surfaceUBO);
    }

    void TearDown() override {
        if (renderer.getDevice() != VK_NULL_HANDLE)
            vkDeviceWaitIdle(renderer.getDevice());
        render_helpers::destroyAtlas(renderer.getDevice(), renderer.getAllocator(),
                                     atlasImg, atlasAlloc, atlasView, atlasSampler);
        cleanupBase();
    }
};

// ---------------------------------------------------------------------------
// Test: sdfThreshold=0.0 and sdfThreshold=0.5 produce different renders
// ---------------------------------------------------------------------------

TEST_F(SDFThresholdTest, BitmapVsSDF_ProduceDifferentPixelOutput)
{
    auto pixelsBitmap = renderAndReadback(0.0f);
    auto pixelsSDF    = renderAndReadback(0.5f);

    ASSERT_EQ(pixelsBitmap.size(), pixelsSDF.size());

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
// ---------------------------------------------------------------------------

class SDFOnEdgeTest : public SDFRenderFixture {
protected:
    VkImage       atlasOnEdgeImg{VK_NULL_HANDLE};
    VmaAllocation atlasOnEdgeAlloc{};
    VkImageView   atlasOnEdgeView{VK_NULL_HANDLE};
    VkSampler     atlasOnEdgeSampler{VK_NULL_HANDLE};

    VkImage       atlasZeroImg{VK_NULL_HANDLE};
    VmaAllocation atlasZeroAlloc{};
    VkImageView   atlasZeroView{VK_NULL_HANDLE};
    VkSampler     atlasZeroSampler{VK_NULL_HANDLE};

    VkImage       atlasAboveImg{VK_NULL_HANDLE};
    VmaAllocation atlasAboveAlloc{};
    VkImageView   atlasAboveView{VK_NULL_HANDLE};
    VkSampler     atlasAboveSampler{VK_NULL_HANDLE};

    VkImage       atlasBelowImg{VK_NULL_HANDLE};
    VmaAllocation atlasBelowAlloc{};
    VkImageView   atlasBelowView{VK_NULL_HANDLE};
    VkSampler     atlasBelowSampler{VK_NULL_HANDLE};

    void SetUp() override {
        ASSERT_TRUE(renderer.init(/*headless=*/true))
            << "Headless renderer init failed";
        scene.init();
        ASSERT_TRUE(renderer.uploadSceneGeometry(scene));

        render_helpers::createAtlas(renderer.getDevice(), renderer.getAllocator(),
                                    renderer.getCommandPool(), renderer.getGraphicsQueue(),
                                    ATLAS_DIM, 128, 128, 128, 255,
                                    atlasOnEdgeImg, atlasOnEdgeAlloc,
                                    atlasOnEdgeView, atlasOnEdgeSampler);
        render_helpers::createAtlas(renderer.getDevice(), renderer.getAllocator(),
                                    renderer.getCommandPool(), renderer.getGraphicsQueue(),
                                    ATLAS_DIM, 0, 0, 0, 0,
                                    atlasZeroImg, atlasZeroAlloc,
                                    atlasZeroView, atlasZeroSampler);
        render_helpers::createAtlas(renderer.getDevice(), renderer.getAllocator(),
                                    renderer.getCommandPool(), renderer.getGraphicsQueue(),
                                    ATLAS_DIM, 220, 220, 220, 255,
                                    atlasAboveImg, atlasAboveAlloc,
                                    atlasAboveView, atlasAboveSampler);
        render_helpers::createAtlas(renderer.getDevice(), renderer.getAllocator(),
                                    renderer.getCommandPool(), renderer.getGraphicsQueue(),
                                    ATLAS_DIM, 25, 25, 25, 200,
                                    atlasBelowImg, atlasBelowAlloc,
                                    atlasBelowView, atlasBelowSampler);

        renderer.bindAtlasDescriptor(atlasOnEdgeView, atlasOnEdgeSampler);
        ASSERT_TRUE(renderer.initOffscreenRT());

        render_helpers::createUIVertexBuffer(renderer.getAllocator(), uiVtxBuf, uiVtxAlloc);

        ASSERT_TRUE(renderer.createHeadlessRT(FB_WIDTH, FB_HEIGHT, hrt));

        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                          static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                          0.1f, 100.0f);
        proj[1][1] *= -1.0f;

        SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
        sceneUBO.lightIntensity = 1.0f;
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
        glm::vec3 eu = m_P10 - m_P00;
        glm::vec3 ev = m_P01 - m_P00;
        surfaceUBO.surfaceNormal = glm::vec4(glm::normalize(glm::cross(eu, ev)), 0.0f);
        renderer.updateSurfaceUBO(surfaceUBO);
    }

    void TearDown() override {
        if (renderer.getDevice() != VK_NULL_HANDLE)
            vkDeviceWaitIdle(renderer.getDevice());
        render_helpers::destroyAtlas(renderer.getDevice(), renderer.getAllocator(),
                                     atlasOnEdgeImg,  atlasOnEdgeAlloc,  atlasOnEdgeView,  atlasOnEdgeSampler);
        render_helpers::destroyAtlas(renderer.getDevice(), renderer.getAllocator(),
                                     atlasZeroImg,    atlasZeroAlloc,    atlasZeroView,    atlasZeroSampler);
        render_helpers::destroyAtlas(renderer.getDevice(), renderer.getAllocator(),
                                     atlasAboveImg,   atlasAboveAlloc,   atlasAboveView,   atlasAboveSampler);
        render_helpers::destroyAtlas(renderer.getDevice(), renderer.getAllocator(),
                                     atlasBelowImg,   atlasBelowAlloc,   atlasBelowView,   atlasBelowSampler);
        cleanupBase();
    }
};

// ---------------------------------------------------------------------------
// Shared helpers for the coverage tests below
// ---------------------------------------------------------------------------

static float smoothstepRef(float edge0, float edge1, float x)
{
    float t = std::max(0.0f, std::min(1.0f, (x - edge0) / (edge1 - edge0)));
    return t * t * (3.0f - 2.0f * t);
}

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
                                  std::cos(scene.light().innerConeAngle));
    ubo.ambientColor  = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    ubo.lightIntensity = 1.0f;
    ubo.uiColorPhase = 0.0f;
    ubo.isTerminalMode = 0.0f;
    return ubo;
}

// ---------------------------------------------------------------------------
// Tests: SDFOnEdgeTest
// ---------------------------------------------------------------------------

TEST_F(SDFOnEdgeTest, OnEdge_SdfThreshold05_ProducesNonZeroAlphaPixels)
{
    renderer.bindAtlasDescriptor(atlasOnEdgeView, atlasOnEdgeSampler);
    auto pixelsOnEdge = renderAndReadback(0.5f);

    renderer.bindAtlasDescriptor(atlasZeroView, atlasZeroSampler);
    auto pixelsZero = renderAndReadback(0.5f);

    ASSERT_EQ(pixelsOnEdge.size(), pixelsZero.size());

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

TEST_F(SDFOnEdgeTest, AboveThreshold_SdfThreshold05_SmoothstepSaturates)
{
    renderer.bindAtlasDescriptor(atlasAboveView, atlasAboveSampler);
    auto pixelsAbove = renderAndReadback(0.5f);

    renderer.bindAtlasDescriptor(atlasZeroView, atlasZeroSampler);
    auto pixelsZero = renderAndReadback(0.5f);

    ASSERT_EQ(pixelsAbove.size(), pixelsZero.size());

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

TEST_F(SDFOnEdgeTest, TraditionalMode_OnEdge_SdfThreshold05_DiffersFromZeroAtlas)
{
    renderer.bindAtlasDescriptor(atlasOnEdgeView, atlasOnEdgeSampler);
    auto pixelsOnEdge = renderAndReadback_traditional(0.5f);

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
// Test: below-threshold pixels are fully transparent
// ---------------------------------------------------------------------------

TEST_F(SDFOnEdgeTest, BelowThreshold_PixelsAreFullyTransparent)
{
    renderer.bindAtlasDescriptor(atlasBelowView, atlasBelowSampler);
    auto pixelsBelow = renderAndReadback(0.5f);

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
           "contribution fully transparent";
}

// ---------------------------------------------------------------------------
// Test: shadow-SDF interaction in direct mode — alpha is SDF-derived
// ---------------------------------------------------------------------------

TEST_F(SDFOnEdgeTest, ShadowSDF_AlphaIsSDFDerived_InDirectMode)
{
    renderer.updateSceneUBO(makeAmbientOnlyUBO(scene, FB_WIDTH, FB_HEIGHT));

    renderer.updateSurfaceQuad(m_P00, m_P10, m_P01, m_P11);

    renderer.bindAtlasDescriptor(atlasOnEdgeView, atlasOnEdgeSampler);
    auto pixelsOnEdge = renderAndReadback(0.5f);

    renderer.bindAtlasDescriptor(atlasZeroView, atlasZeroSampler);
    auto pixelsZero = renderAndReadback(0.5f);

    const uint32_t cx = FB_WIDTH / 2;
    const uint32_t cy = FB_HEIGHT / 2;
    const size_t   ci = (static_cast<size_t>(cy) * FB_WIDTH + cx) * 4;

    int diffR = static_cast<int>(pixelsOnEdge[ci + 0])
              - static_cast<int>(pixelsZero[ci + 0]);

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
// SDFHelloWorldTest — renders a synthetic "H" glyph atlas in SDF mode
// ---------------------------------------------------------------------------

class SDFHelloWorldTest : public SDFRenderFixture {
protected:
    VkImage       helloAtlasImg{VK_NULL_HANDLE};
    VmaAllocation helloAtlasAlloc{};
    VkImageView   helloAtlasView{VK_NULL_HANDLE};
    VkSampler     helloAtlasSampler{VK_NULL_HANDLE};

    void SetUp() override {
        ASSERT_TRUE(renderer.init(/*headless=*/true))
            << "Headless renderer init failed";
        scene.init();
        ASSERT_TRUE(renderer.uploadSceneGeometry(scene));

        createHelloWorldAtlas(helloAtlasImg, helloAtlasAlloc,
                              helloAtlasView, helloAtlasSampler);

        render_helpers::createUIVertexBuffer(renderer.getAllocator(), uiVtxBuf, uiVtxAlloc);

        renderer.bindAtlasDescriptor(helloAtlasView, helloAtlasSampler);
        ASSERT_TRUE(renderer.initOffscreenRT());

        ASSERT_TRUE(renderer.createHeadlessRT(FB_WIDTH, FB_HEIGHT, hrt));

        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                          static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                          0.1f, 100.0f);
        proj[1][1] *= -1.0f;

        SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
        sceneUBO.lightIntensity = 1.0f;
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
        glm::vec3 eu = m_P10 - m_P00;
        glm::vec3 ev = m_P01 - m_P00;
        surfaceUBO.surfaceNormal = glm::vec4(glm::normalize(glm::cross(eu, ev)), 0.0f);
        renderer.updateSurfaceUBO(surfaceUBO);
    }

    void TearDown() override {
        if (renderer.getDevice() != VK_NULL_HANDLE)
            vkDeviceWaitIdle(renderer.getDevice());
        render_helpers::destroyAtlas(renderer.getDevice(), renderer.getAllocator(),
                                     helloAtlasImg, helloAtlasAlloc,
                                     helloAtlasView, helloAtlasSampler);
        cleanupBase();
    }

    void createHelloWorldAtlas(VkImage& imgOut, VmaAllocation& allocOut,
                               VkImageView& viewOut, VkSampler& samplerOut)
    {
        std::vector<uint8_t> pixels(ATLAS_SIZE * ATLAS_SIZE * 4, 0);

        int cx = ATLAS_SIZE / 2;
        int cy = ATLAS_SIZE / 2;
        int hw = 64;
        int ht = 32;

        for (int y = -ht; y <= ht; ++y) {
            for (int x = -hw; x <= hw; ++x) {
                bool isH = (std::abs(x) <= 8) || (std::abs(y) <= 4);
                if (isH) {
                    int px = cx + x;
                    int py = cy + y;
                    if (px >= 0 && px < ATLAS_SIZE && py >= 0 && py < ATLAS_SIZE) {
                        size_t i = (py * ATLAS_SIZE + px) * 4;
                        pixels[i + 0] = 128;
                        pixels[i + 1] = 128;
                        pixels[i + 2] = 128;
                        pixels[i + 3] = 255;
                    }
                }
            }
        }

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
};

TEST_F(SDFHelloWorldTest, HelloWorld_SdfMode_VisibleText)
{
    auto pixelsSDF = renderAndReadback(0.5f);

    uint32_t visiblePixelCount = 0;
    for (uint32_t y = 0; y < FB_HEIGHT; ++y) {
        for (uint32_t x = 0; x < FB_WIDTH; ++x) {
            const size_t i = (y * FB_WIDTH + x) * 4;
            if (pixelsSDF[i + 3] > 25)
                visiblePixelCount++;
        }
    }

    EXPECT_GT(visiblePixelCount, 0u)
        << "SDF mode render of Hello World should produce visible (alpha > 0) pixels; "
           "the glyph atlas may not be loaded correctly or SDF sampling may be broken";

    std::cout << "Visible text pixels: " << visiblePixelCount << std::endl;
}

// ---------------------------------------------------------------------------
// DirectVsTraditionalLightingParityTest — validates identical lighting in both modes
//
// With pure ambient lighting (lightColor=0, ambientColor=1), both modes should output
// (1,1,1,1) at the center pixel.
// ---------------------------------------------------------------------------

class DirectVsTraditionalLightingParityTest : public SDFRenderFixture {
protected:
    VkImage       atlasImg{VK_NULL_HANDLE};
    VmaAllocation atlasAlloc{};
    VkImageView   atlasView{VK_NULL_HANDLE};
    VkSampler     atlasSampler{VK_NULL_HANDLE};

    glm::mat4 m_view{};
    glm::mat4 m_proj{};

    void SetUp() override {
        // Non-default corners: horizontal surface at y=1, z in [-1.5, -2.5].
        m_P00 = glm::vec3(-1.0f, 1.0f, -1.5f);
        m_P10 = glm::vec3( 1.0f, 1.0f, -1.5f);
        m_P01 = glm::vec3(-1.0f, 1.0f, -2.5f);
        m_P11 = glm::vec3( 1.0f, 1.0f, -2.5f);

        ASSERT_TRUE(renderer.init(/*headless=*/true))
            << "Headless renderer init failed";
        scene.init();
        ASSERT_TRUE(renderer.uploadSceneGeometry(scene));

        m_view = glm::lookAt(glm::vec3(0.0f, 2.5f, -1.0f),
                             glm::vec3(0.0f, 1.0f, -2.0f),
                             glm::vec3(1.0f, 0.0f,  0.0f));
        m_proj = glm::perspective(glm::radians(60.0f),
                                  static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                  0.1f, 100.0f);
        m_proj[1][1] *= -1.0f;

        // White atlas: all pixels = (255,255,255,255) — fully opaque white.
        render_helpers::createAtlas(renderer.getDevice(), renderer.getAllocator(),
                                    renderer.getCommandPool(), renderer.getGraphicsQueue(),
                                    ATLAS_DIM, 255, 255, 255, 255,
                                    atlasImg, atlasAlloc, atlasView, atlasSampler);
        renderer.bindAtlasDescriptor(atlasView, atlasSampler);

        ASSERT_TRUE(renderer.initOffscreenRT());
        ASSERT_TRUE(renderer.createHeadlessRT(FB_WIDTH, FB_HEIGHT, hrt));

        // Pure ambient lighting: lightColor=0, ambientColor=1 → lit=(1,1,1).
        SceneUBO sceneUBO{};
        sceneUBO.view          = m_view;
        sceneUBO.proj          = m_proj;
        sceneUBO.lightViewProj = scene.lightViewProj(0.0f);
        sceneUBO.lightPos      = glm::vec4(scene.light().position, 1.0f);
        sceneUBO.lightDir      = glm::vec4(scene.light().direction,
                                          std::cos(scene.light().outerConeAngle));
        sceneUBO.lightColor    = glm::vec4(0.0f, 0.0f, 0.0f,
                                           std::cos(scene.light().innerConeAngle));
        sceneUBO.ambientColor  = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        sceneUBO.lightIntensity = 1.0f;
        renderer.updateSceneUBO(sceneUBO);

        auto transforms = computeSurfaceTransforms(m_P00, m_P10, m_P01,
                                                   2.0f, 1.0f,
                                                   m_proj * m_view);
        auto clipPlanes = computeClipPlanes(m_P00, m_P10, m_P01);

        SurfaceUBO surfaceUBO{};
        surfaceUBO.totalMatrix = transforms.M_total;
        surfaceUBO.worldMatrix = transforms.M_world;
        for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
        surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
        glm::vec3 e_u = m_P10 - m_P00;
        glm::vec3 e_v = m_P01 - m_P00;
        surfaceUBO.surfaceNormal = glm::vec4(normalize(cross(e_u, e_v)), 0.0f);
        renderer.updateSurfaceUBO(surfaceUBO);

        render_helpers::createUIVertexBuffer(renderer.getAllocator(), uiVtxBuf, uiVtxAlloc);
    }

    void TearDown() override {
        if (renderer.getDevice() != VK_NULL_HANDLE)
            vkDeviceWaitIdle(renderer.getDevice());
        render_helpers::destroyAtlas(renderer.getDevice(), renderer.getAllocator(),
                                     atlasImg, atlasAlloc, atlasView, atlasSampler);
        cleanupBase();
    }
};

TEST_F(DirectVsTraditionalLightingParityTest, CenterPixelBrightness_PureAmbient_WithinTolerance)
{
    auto pixelsDirect = renderAndReadback(0.0f);
    auto pixelsTrad   = renderAndReadback_traditional(0.0f);

    const uint32_t cx = FB_WIDTH / 2;
    const uint32_t cy = FB_HEIGHT / 2;
    const size_t   ci = (static_cast<size_t>(cy) * FB_WIDTH + cx) * 4;

    float rDirect = static_cast<float>(pixelsDirect[ci + 0]) / 255.0f;
    float gDirect = static_cast<float>(pixelsDirect[ci + 1]) / 255.0f;
    float bDirect = static_cast<float>(pixelsDirect[ci + 2]) / 255.0f;

    float rTrad = static_cast<float>(pixelsTrad[ci + 0]) / 255.0f;
    float gTrad = static_cast<float>(pixelsTrad[ci + 1]) / 255.0f;
    float bTrad = static_cast<float>(pixelsTrad[ci + 2]) / 255.0f;

    float brightnessDirect = (rDirect + gDirect + bDirect) / 3.0f;
    float brightnessTrad   = (rTrad   + gTrad   + bTrad)   / 3.0f;

    const float tolerance = 0.01f;

    EXPECT_NEAR(brightnessDirect, 1.0f, tolerance)
        << "Direct mode center pixel brightness should be ~1.0 (white) with pure ambient lighting";
    EXPECT_NEAR(brightnessTrad, 1.0f, tolerance)
        << "Traditional mode center pixel brightness should be ~1.0 (white) with pure ambient lighting";

    EXPECT_NEAR(brightnessDirect, brightnessTrad, tolerance)
        << "Direct mode brightness (" << brightnessDirect << ") and traditional mode brightness ("
        << brightnessTrad << ") should be within " << tolerance
        << " tolerance. This validates that both modes produce identical lighting for the same "
           "surfaceNormal/vertex normal.";

    std::cout << "Direct mode brightness: " << brightnessDirect << std::endl;
    std::cout << "Traditional mode brightness: " << brightnessTrad << std::endl;
    std::cout << "Difference: " << std::abs(brightnessDirect - brightnessTrad) << std::endl;
}
