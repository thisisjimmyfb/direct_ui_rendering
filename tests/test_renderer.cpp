#include <gtest/gtest.h>
#include "renderer.h"
#include "scene.h"
#include "shader_uniforms.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vk_mem_alloc.h>
#include <vector>

// ---------------------------------------------------------------------------
// RendererInitTest — Test Renderer initialization in headless mode
// ---------------------------------------------------------------------------

class RendererInitTest : public ::testing::Test {
protected:
    Renderer renderer;

    void TearDown() override {
        renderer.cleanup();
    }
};

TEST_F(RendererInitTest, HeadlessInit_CreatesValidDevice) {
    ASSERT_TRUE(renderer.init(/*headless=*/true, nullptr, TEST_SHADER_DIR));
    EXPECT_NE(renderer.getInstance(), VK_NULL_HANDLE);
    EXPECT_NE(renderer.getDevice(), VK_NULL_HANDLE);
    EXPECT_NE(renderer.getCommandPool(), VK_NULL_HANDLE);
    EXPECT_NE(renderer.getGraphicsQueue(), VK_NULL_HANDLE);
}

TEST_F(RendererInitTest, HeadlessInit_CreatesAllocator) {
    ASSERT_TRUE(renderer.init(/*headless=*/true, nullptr, TEST_SHADER_DIR));
    EXPECT_NE(renderer.getAllocator(), VK_NULL_HANDLE);
}

TEST_F(RendererInitTest, HeadlessInit_SetsFlagCorrectly) {
    ASSERT_TRUE(renderer.init(/*headless=*/true, nullptr, TEST_SHADER_DIR));
    EXPECT_TRUE(renderer.isHeadless());
}

TEST_F(RendererInitTest, HeadlessRT_CreateAndDestroy) {
    ASSERT_TRUE(renderer.init(/*headless=*/true, nullptr, TEST_SHADER_DIR));

    HeadlessRenderTarget hrt{};
    ASSERT_TRUE(renderer.createHeadlessRT(640, 480, hrt));

    EXPECT_NE(hrt.rt.image, VK_NULL_HANDLE);
    EXPECT_NE(hrt.rt.imageView, VK_NULL_HANDLE);
    EXPECT_NE(hrt.msaaColor, VK_NULL_HANDLE);
    EXPECT_NE(hrt.msaaDepth, VK_NULL_HANDLE);
    EXPECT_EQ(hrt.rt.width, 640U);
    EXPECT_EQ(hrt.rt.height, 480U);

    renderer.destroyHeadlessRT(hrt);
    // After destroy, handles should be invalid
    EXPECT_EQ(hrt.rt.image, VK_NULL_HANDLE);
    EXPECT_EQ(hrt.msaaColor, VK_NULL_HANDLE);
}

TEST_F(RendererInitTest, OffscreenRTInit) {
    ASSERT_TRUE(renderer.init(/*headless=*/true, nullptr, TEST_SHADER_DIR));
    ASSERT_TRUE(renderer.initOffscreenRT());
    // Test passes if no assertion/exception
}

// ---------------------------------------------------------------------------
// UniformBufferTest — Test uniform buffer updates
// ---------------------------------------------------------------------------

class UniformBufferTest : public ::testing::Test {
protected:
    Renderer renderer;
    Scene    scene;

    void SetUp() override {
        ASSERT_TRUE(renderer.init(/*headless=*/true, nullptr, TEST_SHADER_DIR));
        scene.init();
        ASSERT_TRUE(renderer.uploadSceneGeometry(scene));
    }

    void TearDown() override {
        renderer.cleanup();
    }

    // Helper to verify matrix values match (within floating point tolerance)
    void expectMatrixEqual(const glm::mat4& a, const glm::mat4& b, float tolerance = 1e-5f) {
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                EXPECT_NEAR(a[col][row], b[col][row], tolerance)
                    << "Mismatch at [" << col << "][" << row << "]";
            }
        }
    }
};

TEST_F(UniformBufferTest, UpdateSceneUBO_AcceptsValidData) {
    SceneUBO ubo{};
    ubo.view = glm::mat4(1.0f);
    ubo.proj = glm::perspective(glm::radians(45.0f), 1.33f, 0.1f, 100.0f);
    ubo.lightPos = glm::vec4(1.0f, 2.0f, 3.0f, 1.0f);
    ubo.lightDir = glm::vec4(0.0f, -1.0f, 0.0f, 0.5f);
    ubo.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.7f);
    ubo.ambientColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);

    // Should not crash
    renderer.updateSceneUBO(ubo);
}

TEST_F(UniformBufferTest, UpdateSurfaceUBO_AcceptsValidData) {
    SurfaceUBO ubo{};
    ubo.totalMatrix = glm::mat4(1.0f);
    ubo.worldMatrix = glm::mat4(1.0f);
    ubo.clipPlanes[0] = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    ubo.clipPlanes[1] = glm::vec4(-1.0f, 0.0f, 0.0f, 4.0f);
    ubo.clipPlanes[2] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
    ubo.clipPlanes[3] = glm::vec4(0.0f, -1.0f, 0.0f, 2.0f);
    ubo.depthBias = 0.0001f;

    // Should not crash
    renderer.updateSurfaceUBO(ubo);
}

TEST_F(UniformBufferTest, UpdateFaceSurfaceUBOs_AcceptsValidData) {
    std::array<SurfaceUBO, 6> ubos{};
    for (int i = 0; i < 6; ++i) {
        ubos[i].totalMatrix = glm::mat4(1.0f);
        ubos[i].worldMatrix = glm::mat4(1.0f);
        ubos[i].depthBias = 0.0001f;
    }

    // Should not crash
    renderer.updateFaceSurfaceUBOs(ubos);
}

TEST_F(UniformBufferTest, SceneUBO_WithPerspectiveMatrix) {
    SceneUBO ubo{};
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0), glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.33f, 0.1f, 100.0f);
    proj[1][1] *= -1.0f;  // Vulkan convention

    ubo.view = view;
    ubo.proj = proj;
    ubo.lightPos = glm::vec4(0.0f, 2.8f, 0.5f, 1.0f);
    ubo.lightDir = glm::vec4(0.0f, -1.3f, -3.5f, 0.6428f);  // cos(50°)
    ubo.lightColor = glm::vec4(1.0f, 0.95f, 0.85f, 0.8192f);  // cos(35°)
    ubo.ambientColor = glm::vec4(0.08f, 0.08f, 0.12f, 1.0f);

    renderer.updateSceneUBO(ubo);
}

TEST_F(UniformBufferTest, SurfaceUBO_WithTransforms) {
    // Simulate a surface at world position
    glm::vec3 P00{-2.0f,  2.0f, -5.0f};
    glm::vec3 P10{ 2.0f,  2.0f, -5.0f};
    glm::vec3 P01{-2.0f, -2.0f, -5.0f};

    // Build M_sw (surface to world)
    glm::vec3 e_u = P10 - P00;
    glm::vec3 e_v = P01 - P00;
    glm::vec3 n = glm::normalize(glm::cross(e_u, e_v));

    glm::mat4 M_sw{1.0f};
    M_sw[0] = glm::vec4(e_u, 0.0f);
    M_sw[1] = glm::vec4(e_v, 0.0f);
    M_sw[2] = glm::vec4(n, 0.0f);
    M_sw[3] = glm::vec4(P00, 1.0f);

    // M_us (UI to surface)
    glm::mat4 M_us{1.0f};
    M_us[0][0] = 1.0f / 512.0f;  // 1/W_ui
    M_us[1][1] = 1.0f / 128.0f;  // 1/H_ui

    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.33f, 0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 M_wc = proj * view;

    glm::mat4 M_total = M_wc * M_sw * M_us;
    glm::mat4 M_world = M_sw * M_us;

    SurfaceUBO ubo{};
    ubo.totalMatrix = M_total;
    ubo.worldMatrix = M_world;
    ubo.clipPlanes[0] = glm::vec4(1.0f, 0.0f, 0.0f, 2.0f);
    ubo.clipPlanes[1] = glm::vec4(-1.0f, 0.0f, 0.0f, 2.0f);
    ubo.clipPlanes[2] = glm::vec4(0.0f, 1.0f, 0.0f, 2.0f);
    ubo.clipPlanes[3] = glm::vec4(0.0f, -1.0f, 0.0f, 2.0f);
    ubo.depthBias = 0.0001f;

    renderer.updateSurfaceUBO(ubo);
}

// ---------------------------------------------------------------------------
// SurfaceGeometryTest — Test surface geometry updates
// ---------------------------------------------------------------------------

class SurfaceGeometryTest : public ::testing::Test {
protected:
    Renderer renderer;
    Scene    scene;

    void SetUp() override {
        ASSERT_TRUE(renderer.init(/*headless=*/true, nullptr, TEST_SHADER_DIR));
        scene.init();
        ASSERT_TRUE(renderer.uploadSceneGeometry(scene));
    }

    void TearDown() override {
        renderer.cleanup();
    }
};

TEST_F(SurfaceGeometryTest, UpdateSurfaceQuad_AcceptsValidCorners) {
    glm::vec3 P00{-1.0f,  1.0f, 0.0f};
    glm::vec3 P10{ 1.0f,  1.0f, 0.0f};
    glm::vec3 P01{-1.0f, -1.0f, 0.0f};
    glm::vec3 P11{ 1.0f, -1.0f, 0.0f};

    // Should not crash
    renderer.updateSurfaceQuad(P00, P10, P01, P11);
}

TEST_F(SurfaceGeometryTest, UpdateSurfaceQuad_WithAnimatedTransform) {
    // Simulate animated surface moving in world space
    glm::mat4 anim = glm::rotate(glm::mat4(1.0f), 0.5f, glm::vec3(0, 1, 0));
    anim = glm::translate(anim, glm::vec3(2.0f, 0.0f, 0.0f));

    glm::vec3 P00_local{-1.0f,  1.0f, 0.0f};
    glm::vec3 P10_local{ 1.0f,  1.0f, 0.0f};
    glm::vec3 P01_local{-1.0f, -1.0f, 0.0f};
    glm::vec3 P11_local{ 1.0f, -1.0f, 0.0f};

    glm::vec3 P00 = glm::vec3(anim * glm::vec4(P00_local, 1.0f));
    glm::vec3 P10 = glm::vec3(anim * glm::vec4(P10_local, 1.0f));
    glm::vec3 P01 = glm::vec3(anim * glm::vec4(P01_local, 1.0f));
    glm::vec3 P11 = glm::vec3(anim * glm::vec4(P11_local, 1.0f));

    renderer.updateSurfaceQuad(P00, P10, P01, P11);
}

TEST_F(SurfaceGeometryTest, UpdateCubeSurface_AcceptsValidCorners) {
    std::array<std::array<glm::vec3, 4>, 6> faceCorners{};

    // Initialize all 6 faces with simple test data
    float size = 2.0f;
    for (int f = 0; f < 6; ++f) {
        faceCorners[f][0] = glm::vec3(-size, -size, 0.0f);
        faceCorners[f][1] = glm::vec3(size, -size, 0.0f);
        faceCorners[f][2] = glm::vec3(size, size, 0.0f);
        faceCorners[f][3] = glm::vec3(-size, size, 0.0f);
    }

    // Should not crash
    renderer.updateCubeSurface(faceCorners);
}

TEST_F(SurfaceGeometryTest, UpdateUIShadowCube_AcceptsValidCorners) {
    std::array<std::array<glm::vec3, 4>, 6> faceCorners{};

    float size = 1.0f;
    for (int f = 0; f < 6; ++f) {
        faceCorners[f][0] = glm::vec3(-size, -size, 0.0f);
        faceCorners[f][1] = glm::vec3(size, -size, 0.0f);
        faceCorners[f][2] = glm::vec3(size, size, 0.0f);
        faceCorners[f][3] = glm::vec3(-size, size, 0.0f);
    }

    // Should not crash
    renderer.updateUIShadowCube(faceCorners);
}

TEST_F(SurfaceGeometryTest, UpdateCubeSurface_MultipleFrames) {
    std::array<std::array<glm::vec3, 4>, 6> faceCorners{};

    // Update cube geometry over multiple simulated frames
    for (int frame = 0; frame < 10; ++frame) {
        float rotation = frame * 0.1f;
        glm::mat4 transform = glm::rotate(glm::mat4(1.0f), rotation, glm::vec3(0, 1, 0));

        float size = 1.5f;
        for (int f = 0; f < 6; ++f) {
            glm::vec3 local[4] = {
                {-size, -size, 0.0f},
                {size, -size, 0.0f},
                {size, size, 0.0f},
                {-size, size, 0.0f}
            };
            for (int i = 0; i < 4; ++i) {
                faceCorners[f][i] = glm::vec3(transform * glm::vec4(local[i], 1.0f));
            }
        }

        renderer.updateCubeSurface(faceCorners);
    }
}

// ---------------------------------------------------------------------------
// DescriptorBindingTest — Test descriptor set binding
// ---------------------------------------------------------------------------

class DescriptorBindingTest : public ::testing::Test {
protected:
    Renderer renderer;

    void SetUp() override {
        ASSERT_TRUE(renderer.init(/*headless=*/true, nullptr, TEST_SHADER_DIR));
    }

    void TearDown() override {
        renderer.cleanup();
    }
};

TEST_F(DescriptorBindingTest, BindAtlasDescriptor) {
    // Create a dummy atlas image
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ci.extent        = {512, 512, 1};
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkImage img;
    VmaAllocation alloc;
    ASSERT_EQ(vmaCreateImage(renderer.getAllocator(), &ci, &ai, &img, &alloc, nullptr),
              VK_SUCCESS);

    VkImageViewCreateInfo viewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewCI.image            = img;
    viewCI.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format           = VK_FORMAT_R8G8B8A8_UNORM;
    viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageView view;
    ASSERT_EQ(vkCreateImageView(renderer.getDevice(), &viewCI, nullptr, &view), VK_SUCCESS);

    VkSamplerCreateInfo sampCI{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampCI.magFilter    = VK_FILTER_LINEAR;
    sampCI.minFilter    = VK_FILTER_LINEAR;
    sampCI.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    VkSampler samp;
    ASSERT_EQ(vkCreateSampler(renderer.getDevice(), &sampCI, nullptr, &samp), VK_SUCCESS);

    // Bind the descriptor — should not crash
    renderer.bindAtlasDescriptor(view, samp);

    // Cleanup
    vkDestroySampler(renderer.getDevice(), samp, nullptr);
    vkDestroyImageView(renderer.getDevice(), view, nullptr);
    vmaDestroyImage(renderer.getAllocator(), img, alloc);
}

// ---------------------------------------------------------------------------
// RenderPassTest — Test render pass creation with attachment validation
// ---------------------------------------------------------------------------

class RenderPassTest : public ::testing::Test {
protected:
    Renderer renderer;

    void SetUp() override {
        ASSERT_TRUE(renderer.init(/*headless=*/true, nullptr, TEST_SHADER_DIR));
    }

    void TearDown() override {
        renderer.cleanup();
    }

    // Helper to query render pass properties
    struct RenderPassProperties {
        uint32_t attachmentCount{0};
        std::vector<VkAttachmentDescription> attachments;
    };

    // Note: VkRenderPass is an opaque handle, so we can only test that it's created
    // and not null. Full validation of attachment properties would require
    // exposing the render pass properties or inspecting through indirect testing.
};

TEST_F(RenderPassTest, ShadowPassCreated) {
    VkRenderPass shadowPass = renderer.getShadowPass();
    EXPECT_NE(shadowPass, VK_NULL_HANDLE);
}

TEST_F(RenderPassTest, UIRTPassCreated) {
    VkRenderPass uiRTPass = renderer.getUIRTPass();
    EXPECT_NE(uiRTPass, VK_NULL_HANDLE);
}

TEST_F(RenderPassTest, MainPassCreated) {
    VkRenderPass mainPass = renderer.getMainPass();
    EXPECT_NE(mainPass, VK_NULL_HANDLE);
}

TEST_F(RenderPassTest, MetricsPassCreated) {
    VkRenderPass metricsPass = renderer.getMetricsPass();
    EXPECT_NE(metricsPass, VK_NULL_HANDLE);
}

// Test that all render passes are distinct handles
TEST_F(RenderPassTest, RenderPassesAreDistinct) {
    VkRenderPass shadow  = renderer.getShadowPass();
    VkRenderPass uiRT    = renderer.getUIRTPass();
    VkRenderPass main    = renderer.getMainPass();
    VkRenderPass metrics = renderer.getMetricsPass();

    // Each should be a valid, unique handle
    EXPECT_NE(shadow, VK_NULL_HANDLE);
    EXPECT_NE(uiRT, VK_NULL_HANDLE);
    EXPECT_NE(main, VK_NULL_HANDLE);
    EXPECT_NE(metrics, VK_NULL_HANDLE);

    // They should be distinct
    EXPECT_NE(shadow, uiRT);
    EXPECT_NE(shadow, main);
    EXPECT_NE(shadow, metrics);
    EXPECT_NE(uiRT, main);
    EXPECT_NE(uiRT, metrics);
    EXPECT_NE(main, metrics);
}

// ---------------------------------------------------------------------------
// PipelineTest — Test pipeline creation
// ---------------------------------------------------------------------------

class PipelineTest : public ::testing::Test {
protected:
    Renderer renderer;

    void SetUp() override {
        ASSERT_TRUE(renderer.init(/*headless=*/true, nullptr, TEST_SHADER_DIR));
    }

    void TearDown() override {
        renderer.cleanup();
    }
};

TEST_F(PipelineTest, ShadowPipelineCreated) {
    VkPipeline shadowPipe = renderer.getShadowPipeline();
    EXPECT_NE(shadowPipe, VK_NULL_HANDLE);
}

TEST_F(PipelineTest, RoomPipelineCreated) {
    VkPipeline roomPipe = renderer.getRoomPipeline();
    EXPECT_NE(roomPipe, VK_NULL_HANDLE);
}

TEST_F(PipelineTest, UIDirectPipelineCreated) {
    VkPipeline uidirectPipe = renderer.getUIDirectPipeline();
    EXPECT_NE(uidirectPipe, VK_NULL_HANDLE);
}

TEST_F(PipelineTest, UIRTPipelineCreated) {
    VkPipeline uirtPipe = renderer.getUIRTPipeline();
    EXPECT_NE(uirtPipe, VK_NULL_HANDLE);
}

TEST_F(PipelineTest, CompositePipelineCreated) {
    VkPipeline compositePipe = renderer.getCompositePipeline();
    EXPECT_NE(compositePipe, VK_NULL_HANDLE);
}

TEST_F(PipelineTest, SurfacePipelineCreated) {
    VkPipeline surfacePipe = renderer.getSurfacePipeline();
    EXPECT_NE(surfacePipe, VK_NULL_HANDLE);
}

TEST_F(PipelineTest, MetricsPipelineCreated) {
    VkPipeline metricsPipe = renderer.getMetricsPipeline();
    EXPECT_NE(metricsPipe, VK_NULL_HANDLE);
}

TEST_F(PipelineTest, PipelinesAreDistinct) {
    VkPipeline shadow    = renderer.getShadowPipeline();
    VkPipeline room      = renderer.getRoomPipeline();
    VkPipeline uidirect  = renderer.getUIDirectPipeline();
    VkPipeline uirt      = renderer.getUIRTPipeline();
    VkPipeline composite = renderer.getCompositePipeline();
    VkPipeline surface   = renderer.getSurfacePipeline();
    VkPipeline metrics   = renderer.getMetricsPipeline();

    // All should be valid handles
    EXPECT_NE(shadow, VK_NULL_HANDLE);
    EXPECT_NE(room, VK_NULL_HANDLE);
    EXPECT_NE(uidirect, VK_NULL_HANDLE);
    EXPECT_NE(uirt, VK_NULL_HANDLE);
    EXPECT_NE(composite, VK_NULL_HANDLE);
    EXPECT_NE(surface, VK_NULL_HANDLE);
    EXPECT_NE(metrics, VK_NULL_HANDLE);

    // Verify distinction (spot checks)
    EXPECT_NE(shadow, room);
    EXPECT_NE(uidirect, uirt);
    EXPECT_NE(composite, surface);
}

TEST_F(PipelineTest, PipelineLayoutCreated) {
    VkPipelineLayout layout = renderer.getPipelineLayout();
    EXPECT_NE(layout, VK_NULL_HANDLE);
}

TEST_F(PipelineTest, DescriptorSetLayoutsCreated) {
    VkDescriptorSetLayout set0 = renderer.getSetLayout0();
    VkDescriptorSetLayout set1 = renderer.getSetLayout1();
    VkDescriptorSetLayout set2 = renderer.getSetLayout2();

    EXPECT_NE(set0, VK_NULL_HANDLE);
    EXPECT_NE(set1, VK_NULL_HANDLE);
    EXPECT_NE(set2, VK_NULL_HANDLE);
}

TEST_F(PipelineTest, DescriptorSetLayoutsAreDistinct) {
    VkDescriptorSetLayout set0 = renderer.getSetLayout0();
    VkDescriptorSetLayout set1 = renderer.getSetLayout1();
    VkDescriptorSetLayout set2 = renderer.getSetLayout2();

    EXPECT_NE(set0, set1);
    EXPECT_NE(set0, set2);
    EXPECT_NE(set1, set2);
}
