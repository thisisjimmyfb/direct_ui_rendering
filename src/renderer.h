#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

// ---------------------------------------------------------------------------
// GPU-side uniform buffer structs (std140 layout)
// ---------------------------------------------------------------------------
struct SceneUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 lightViewProj;
    glm::vec4 lightDir;
    glm::vec4 lightColor;
    glm::vec4 ambientColor;
};

struct SurfaceUBO {
    glm::mat4 totalMatrix;   // M_wc * M_sw * M_us
    glm::mat4 worldMatrix;   // M_sw * M_us (for clip distance computation)
    glm::vec4 clipPlanes[4];
    float     depthBias;
    float     _pad[3];
};

// ---------------------------------------------------------------------------
// RenderTarget — wraps a VkImage+VkImageView for use as a render destination.
// In normal mode this is a swapchain image; in headless mode it is an
// offscreen image allocated by the test.
// ---------------------------------------------------------------------------
struct RenderTarget {
    VkImage       image{VK_NULL_HANDLE};
    VkImageView   imageView{VK_NULL_HANDLE};
    VkFramebuffer framebuffer{VK_NULL_HANDLE};  // created by Renderer against the right pass
    uint32_t      width{0};
    uint32_t      height{0};
    bool          isSwapchain{false};
};

// ---------------------------------------------------------------------------
// Renderer — Vulkan device, pipelines, render passes, per-frame recording.
// init(headless=true) skips GLFW surface and swapchain setup but initialises
// all pipelines and VMA identically so tests can use the same code paths.
// ---------------------------------------------------------------------------
class Renderer {
public:
    // Lifecycle
    bool init(bool headless = false);
    void cleanup();

    // UBO updates (call once per frame before recording)
    void updateSceneUBO(const SceneUBO& data);
    void updateSurfaceUBO(const SurfaceUBO& data);

    // Command buffer recording — call in order each frame
    void recordShadowPass(VkCommandBuffer cmd);
    void recordUIRTPass(VkCommandBuffer cmd);                             // traditional only
    void recordMainPass(VkCommandBuffer cmd, RenderTarget& rt, bool directMode);
    void recordMetricsPass(VkCommandBuffer cmd, RenderTarget& rt);

    // Frame helpers (non-headless only)
    bool acquireSwapchainImage(uint32_t& imageIndex);
    void presentSwapchainImage(uint32_t imageIndex);

    // Accessors used by App / tests
    VmaAllocator  getAllocator()   const { return m_allocator; }
    VkDevice      getDevice()     const { return m_device; }
    VkInstance    getInstance()   const { return m_instance; }
    VkCommandPool getCommandPool() const { return m_cmdPool; }
    VkQueue       getGraphicsQueue() const { return m_graphicsQueue; }

    bool isHeadless() const { return m_headless; }

    // Canvas / rendering constants
    static constexpr uint32_t W_UI             = 512;
    static constexpr uint32_t H_UI             = 128;
    static constexpr uint32_t SHADOW_MAP_SIZE  = 1024;
    static constexpr float    DEPTH_BIAS_DEFAULT = 0.0001f;

private:
    bool createInstance();
    bool selectPhysicalDevice();
    bool createLogicalDevice();
    bool createAllocator();
    bool createCommandPool();
    bool createSwapchain(VkSurfaceKHR surface, uint32_t width, uint32_t height);
    bool createRenderPasses();
    bool createDescriptorSetLayouts();
    bool createPipelines();
    bool createDescriptorPool();
    bool allocateDescriptorSets();
    bool createUniformBuffers();
    bool createShadowResources();
    void destroySwapchain();

    // Instance / device
    VkInstance               m_instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT m_debugMessenger{VK_NULL_HANDLE};
    VkPhysicalDevice         m_physDevice{VK_NULL_HANDLE};
    VkDevice                 m_device{VK_NULL_HANDLE};
    VkQueue                  m_graphicsQueue{VK_NULL_HANDLE};
    VkQueue                  m_presentQueue{VK_NULL_HANDLE};
    uint32_t                 m_graphicsQueueFamily{0};

    // Memory
    VmaAllocator m_allocator{VK_NULL_HANDLE};

    // Surface / swapchain (non-headless only)
    VkSurfaceKHR             m_surface{VK_NULL_HANDLE};
    VkSwapchainKHR           m_swapchain{VK_NULL_HANDLE};
    std::vector<VkImage>     m_swapImages;
    std::vector<VkImageView> m_swapImageViews;
    VkFormat                 m_swapFormat{VK_FORMAT_UNDEFINED};
    VkExtent2D               m_swapExtent{};

    // Command pool / buffers
    VkCommandPool m_cmdPool{VK_NULL_HANDLE};

    // Render passes
    VkRenderPass m_shadowPass{VK_NULL_HANDLE};
    VkRenderPass m_uiRTPass{VK_NULL_HANDLE};
    VkRenderPass m_mainPass{VK_NULL_HANDLE};
    VkRenderPass m_metricsPass{VK_NULL_HANDLE};

    // Descriptor set layouts
    VkDescriptorSetLayout m_setLayout0{VK_NULL_HANDLE};  // SceneUBO + shadowMap
    VkDescriptorSetLayout m_setLayout1{VK_NULL_HANDLE};  // SurfaceUBO
    VkDescriptorSetLayout m_setLayout2{VK_NULL_HANDLE};  // atlas + offscreen RT

    // Pipelines
    VkPipeline       m_pipeRoom{VK_NULL_HANDLE};
    VkPipeline       m_pipeUIDirect{VK_NULL_HANDLE};
    VkPipeline       m_pipeUIRT{VK_NULL_HANDLE};
    VkPipeline       m_pipeComposite{VK_NULL_HANDLE};
    VkPipeline       m_pipeMetrics{VK_NULL_HANDLE};
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};

    // Descriptor pool / sets
    VkDescriptorPool m_descPool{VK_NULL_HANDLE};
    VkDescriptorSet  m_set0{VK_NULL_HANDLE};
    VkDescriptorSet  m_set1{VK_NULL_HANDLE};
    VkDescriptorSet  m_set2{VK_NULL_HANDLE};

    // Uniform buffers
    VkBuffer      m_sceneUBOBuf{VK_NULL_HANDLE};
    VmaAllocation m_sceneUBOAlloc{VK_NULL_HANDLE};
    VkBuffer      m_surfaceUBOBuf{VK_NULL_HANDLE};
    VmaAllocation m_surfaceUBOAlloc{VK_NULL_HANDLE};

    // Shadow map resources
    VkImage       m_shadowImage{VK_NULL_HANDLE};
    VmaAllocation m_shadowAlloc{VK_NULL_HANDLE};
    VkImageView   m_shadowView{VK_NULL_HANDLE};
    VkFramebuffer m_shadowFB{VK_NULL_HANDLE};
    VkSampler     m_shadowSampler{VK_NULL_HANDLE};

    // Offscreen UI RT (traditional mode, allocated lazily)
    VkImage       m_uiRTImage{VK_NULL_HANDLE};
    VmaAllocation m_uiRTAlloc{VK_NULL_HANDLE};
    VkImageView   m_uiRTView{VK_NULL_HANDLE};
    VkFramebuffer m_uiRTFB{VK_NULL_HANDLE};
    VkSampler     m_uiRTSampler{VK_NULL_HANDLE};

    // Semaphores / sync (non-headless only)
    VkSemaphore m_imageAvailable{VK_NULL_HANDLE};
    VkSemaphore m_renderFinished{VK_NULL_HANDLE};
    VkFence     m_inFlightFence{VK_NULL_HANDLE};

    bool m_headless{false};
};
