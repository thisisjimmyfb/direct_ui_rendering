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

// Vertex layout for the composite surface quad (world-space pos + UV).
struct QuadVertex {
    glm::vec3 pos;
    glm::vec2 uv;
};

// ---------------------------------------------------------------------------
// RenderTarget — wraps a VkImage+VkImageView for use as a render destination.
// In normal mode this is a swapchain image; in headless mode it is an
// offscreen image allocated by the test.
// ---------------------------------------------------------------------------
struct RenderTarget {
    VkImage       image{VK_NULL_HANDLE};
    VkImageView   imageView{VK_NULL_HANDLE};
    VkFramebuffer framebuffer{VK_NULL_HANDLE};        // main pass FB (MSAA color + depth + resolve)
    VkFramebuffer metricsFramebuffer{VK_NULL_HANDLE}; // metrics overlay pass FB (1x final image)
    uint32_t      width{0};
    uint32_t      height{0};
    bool          isSwapchain{false};
};

// Forward-declare GLFWwindow so callers don't need to include GLFW.
struct GLFWwindow;

// Forward-declare Scene so Renderer methods can reference it without pulling in scene.h.
class Scene;

// ---------------------------------------------------------------------------
// Renderer — Vulkan device, pipelines, render passes, per-frame recording.
// init(headless=true) skips GLFW surface and swapchain setup but initialises
// all pipelines and VMA identically so tests can use the same code paths.
// ---------------------------------------------------------------------------
class Renderer {
public:
    // Lifecycle
    // In non-headless mode, pass the GLFW window so the renderer can create
    // the Vulkan surface and swapchain.
    bool init(bool headless = false, GLFWwindow* window = nullptr);
    void cleanup();

    // UBO updates (call once per frame before recording)
    void updateSceneUBO(const SceneUBO& data);
    void updateSurfaceUBO(const SurfaceUBO& data);

    // Command buffer recording — call in order each frame
    void recordShadowPass(VkCommandBuffer cmd);
    void recordUIRTPass(VkCommandBuffer cmd,
                        VkBuffer uiVtxBuf, uint32_t uiVtxCount,
                        const glm::mat4& ortho);
    void recordMainPass(VkCommandBuffer cmd, RenderTarget& rt, bool directMode,
                        VkBuffer uiVtxBuf, uint32_t uiVtxCount);
    void recordMetricsPass(VkCommandBuffer cmd, RenderTarget& rt,
                           VkBuffer hudVtxBuf, uint32_t hudVtxCount,
                           const glm::mat4& ortho);

    // Frame helpers (non-headless only)
    bool acquireSwapchainImage(uint32_t& imageIndex);
    void presentSwapchainImage(uint32_t imageIndex);

    // Upload room mesh geometry to GPU buffers (call once after Scene::init()).
    bool uploadSceneGeometry(const Scene& scene);
    // Bind the UI glyph atlas into descriptor set 2, binding 0 (call once after UISystem::init()).
    void bindAtlasDescriptor(VkImageView view, VkSampler sampler);
    // Update the surface quad vertex buffer each frame (for composite/traditional mode).
    void updateSurfaceQuad(const glm::vec3& P00, const glm::vec3& P10,
                           const glm::vec3& P01, const glm::vec3& P11);
    // Get the RenderTarget for the given swapchain image index.
    RenderTarget& getSwapchainRT(uint32_t imageIndex);
    // Semaphore that becomes signalled when the swapchain image is available.
    VkSemaphore getImageAvailableSemaphore() const { return m_imageAvailable; }

    // Accessors used by App / tests
    VmaAllocator  getAllocator()   const { return m_allocator; }
    VkDevice      getDevice()     const { return m_device; }
    VkInstance    getInstance()   const { return m_instance; }
    VkCommandPool getCommandPool() const { return m_cmdPool; }
    VkQueue       getGraphicsQueue() const { return m_graphicsQueue; }

    // Swapchain accessors (non-headless only)
    VkExtent2D                      getSwapExtent()      const { return m_swapExtent; }
    uint32_t                        getSwapImageCount()  const { return static_cast<uint32_t>(m_swapImages.size()); }
    const std::vector<VkImageView>& getSwapImageViews()  const { return m_swapImageViews; }
    VkFormat                        getSwapFormat()      const { return m_swapFormat; }
    VkSemaphore                     getRenderFinishedSemaphore() const { return m_renderFinished; }
    VkFence                         getInFlightFence()   const { return m_inFlightFence; }

    // Returns the total bytes currently allocated through VMA (for metrics overlay).
    uint64_t getTotalAllocatedBytes() const;

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
    bool createSwapchain();   // uses m_surface, sets m_swapFormat/m_swapExtent/m_swapImages/Views + sync objects
    bool createRenderPasses();
    bool createDescriptorSetLayouts();
    bool createPipelines();
    bool createDescriptorPool();
    bool allocateDescriptorSets();
    bool createUniformBuffers();
    bool createShadowResources();
    void destroySwapchain();
    bool createFramebuffers();        // MSAA resources + per-swapchain FBs
    bool createSurfaceQuadBuffer();   // allocate host-visible surface quad buffer
    bool ensureUIRTAllocated();       // lazily create offscreen UI RT (traditional mode)

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

    // Color format used by render passes (R8G8B8A8_UNORM in headless; swapchain format otherwise)
    VkFormat m_colorFormat{VK_FORMAT_R8G8B8A8_UNORM};

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

    // Shadow pipeline — depth-only room render from light POV
    VkPipeline m_pipeShadow{VK_NULL_HANDLE};

    // Room mesh GPU buffers (device-local, uploaded once from Scene::roomMesh())
    VkBuffer      m_roomVtxBuf{VK_NULL_HANDLE};
    VmaAllocation m_roomVtxAlloc{VK_NULL_HANDLE};
    VkBuffer      m_roomIdxBuf{VK_NULL_HANDLE};
    VmaAllocation m_roomIdxAlloc{VK_NULL_HANDLE};
    uint32_t      m_roomIdxCount{0};

    // Surface quad buffer (host-visible, 6 QuadVertices, updated per frame for composite mode)
    VkBuffer      m_surfaceQuadBuf{VK_NULL_HANDLE};
    VmaAllocation m_surfaceQuadAlloc{VK_NULL_HANDLE};

    // MSAA transient attachments for main pass (shared across all frames)
    VkImage       m_msaaColorImg{VK_NULL_HANDLE};
    VmaAllocation m_msaaColorAlloc{VK_NULL_HANDLE};
    VkImageView   m_msaaColorView{VK_NULL_HANDLE};
    VkImage       m_msaaDepthImg{VK_NULL_HANDLE};
    VmaAllocation m_msaaDepthAlloc{VK_NULL_HANDLE};
    VkImageView   m_msaaDepthView{VK_NULL_HANDLE};

    // Per-swapchain-image render targets (populated by createFramebuffers())
    std::vector<RenderTarget> m_swapRTs;

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
