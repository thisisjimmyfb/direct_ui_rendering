// VMA implementation — must appear in exactly one translation unit.
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "renderer.h"
#include "vk_utils.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool Renderer::init(bool headless)
{
    m_headless = headless;
    // Headless mode uses a known format; non-headless will be updated by createSwapchain().
    if (m_headless) m_colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

    if (!createInstance())             return false;
    if (!selectPhysicalDevice())       return false;
    if (!createLogicalDevice())        return false;
    if (!createAllocator())            return false;
    if (!createCommandPool())          return false;
    if (!createRenderPasses())         return false;
    if (!createDescriptorSetLayouts()) return false;  // also creates m_pipelineLayout
    if (!createDescriptorPool())       return false;
    if (!allocateDescriptorSets())     return false;
    if (!createUniformBuffers())       return false;  // writes UBO descriptors into sets
    if (!createPipelines())            return false;  // needs pipeline layout + render passes
    if (!createShadowResources())      return false;

    return true;
}

void Renderer::cleanup()
{
    if (m_device) vkDeviceWaitIdle(m_device);

    // Uniform buffers
    if (m_sceneUBOBuf)   { vmaDestroyBuffer(m_allocator, m_sceneUBOBuf,   m_sceneUBOAlloc);   m_sceneUBOBuf   = VK_NULL_HANDLE; }
    if (m_surfaceUBOBuf) { vmaDestroyBuffer(m_allocator, m_surfaceUBOBuf, m_surfaceUBOAlloc); m_surfaceUBOBuf = VK_NULL_HANDLE; }

    // Descriptor pool (frees all sets implicitly)
    if (m_descPool)    { vkDestroyDescriptorPool(m_device, m_descPool,   nullptr); m_descPool   = VK_NULL_HANDLE; }
    if (m_setLayout0)  { vkDestroyDescriptorSetLayout(m_device, m_setLayout0, nullptr); m_setLayout0 = VK_NULL_HANDLE; }
    if (m_setLayout1)  { vkDestroyDescriptorSetLayout(m_device, m_setLayout1, nullptr); m_setLayout1 = VK_NULL_HANDLE; }
    if (m_setLayout2)  { vkDestroyDescriptorSetLayout(m_device, m_setLayout2, nullptr); m_setLayout2 = VK_NULL_HANDLE; }
    if (m_pipelineLayout) { vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }

    // Render passes
    if (m_shadowPass)  { vkDestroyRenderPass(m_device, m_shadowPass,  nullptr); m_shadowPass  = VK_NULL_HANDLE; }
    if (m_uiRTPass)    { vkDestroyRenderPass(m_device, m_uiRTPass,    nullptr); m_uiRTPass    = VK_NULL_HANDLE; }
    if (m_mainPass)    { vkDestroyRenderPass(m_device, m_mainPass,    nullptr); m_mainPass    = VK_NULL_HANDLE; }
    if (m_metricsPass) { vkDestroyRenderPass(m_device, m_metricsPass, nullptr); m_metricsPass = VK_NULL_HANDLE; }

    if (m_cmdPool) {
        vkDestroyCommandPool(m_device, m_cmdPool, nullptr);
        m_cmdPool = VK_NULL_HANDLE;
    }
    if (m_allocator) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
    if (m_device) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
#ifdef ENABLE_VALIDATION_LAYERS
    if (m_debugMessenger) {
        auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (fn) fn(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
#endif
    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// UBO updates
// ---------------------------------------------------------------------------

void Renderer::updateSceneUBO(const SceneUBO& data)
{
    if (!m_sceneUBOAlloc) return;
    void* mapped{nullptr};
    vmaMapMemory(m_allocator, m_sceneUBOAlloc, &mapped);
    memcpy(mapped, &data, sizeof(data));
    vmaUnmapMemory(m_allocator, m_sceneUBOAlloc);
}

void Renderer::updateSurfaceUBO(const SurfaceUBO& data)
{
    if (!m_surfaceUBOAlloc) return;
    void* mapped{nullptr};
    vmaMapMemory(m_allocator, m_surfaceUBOAlloc, &mapped);
    memcpy(mapped, &data, sizeof(data));
    vmaUnmapMemory(m_allocator, m_surfaceUBOAlloc);
}

// ---------------------------------------------------------------------------
// VMA stats
// ---------------------------------------------------------------------------

uint64_t Renderer::getTotalAllocatedBytes() const
{
    if (!m_allocator) return 0;
    VmaTotalStatistics stats{};
    vmaCalculateStatistics(m_allocator, &stats);
    return static_cast<uint64_t>(stats.total.statistics.allocationBytes);
}

// ---------------------------------------------------------------------------
// Command buffer recording — stubs (TODO: implement each pass)
// ---------------------------------------------------------------------------

void Renderer::recordShadowPass(VkCommandBuffer /*cmd*/)
{
    // TODO: begin shadow render pass, bind pipe_room with shadow variant, draw room + surface quad
}

void Renderer::recordUIRTPass(VkCommandBuffer /*cmd*/)
{
    // TODO: begin UI RT render pass, bind pipe_ui_rt, draw glyph quads with orthographic projection
}

void Renderer::recordMainPass(VkCommandBuffer /*cmd*/, RenderTarget& /*rt*/, bool /*directMode*/)
{
    // TODO: begin main MSAA pass, draw room via pipe_room,
    //       if directMode: draw UI via pipe_ui_direct,
    //       else:          draw surface quad via pipe_composite
}

void Renderer::recordMetricsPass(VkCommandBuffer /*cmd*/, RenderTarget& /*rt*/)
{
    // TODO: begin metrics overlay pass, bind pipe_metrics, draw HUD glyph quads
}

// ---------------------------------------------------------------------------
// Frame helpers
// ---------------------------------------------------------------------------

bool Renderer::acquireSwapchainImage(uint32_t& imageIndex)
{
    if (m_headless) return false;
    // TODO: vkAcquireNextImageKHR
    imageIndex = 0;
    return false;
}

void Renderer::presentSwapchainImage(uint32_t /*imageIndex*/)
{
    if (m_headless) return;
    // TODO: vkQueuePresentKHR
}

// ---------------------------------------------------------------------------
// createInstance — VkInstance with optional validation layers + debug messenger
// ---------------------------------------------------------------------------

bool Renderer::createInstance()
{
    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName   = "direct_ui_rendering";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "direct_ui_rendering";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_3;

    // Collect required instance extensions.
    std::vector<const char*> extensions;
    if (!m_headless) {
        uint32_t glfwCount = 0;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwCount);
        for (uint32_t i = 0; i < glfwCount; ++i)
            extensions.push_back(glfwExts[i]);
    }
#ifdef ENABLE_VALIDATION_LAYERS
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

#ifdef ENABLE_VALIDATION_LAYERS
    // Debug messenger config — reused for pNext (catches instance create/destroy issues)
    // and for creating the persistent messenger after the instance exists.
    VkDebugUtilsMessengerCreateInfoEXT dbgInfo{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    dbgInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dbgInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dbgInfo.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT,
                                 VkDebugUtilsMessageTypeFlagsEXT,
                                 const VkDebugUtilsMessengerCallbackDataEXT* pData,
                                 void*) -> VkBool32 {
        fprintf(stderr, "[VK] %s\n", pData->pMessage);
        return VK_FALSE;
    };
    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
#endif

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo        = &appInfo;
    ci.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
#ifdef ENABLE_VALIDATION_LAYERS
    ci.enabledLayerCount   = 1;
    ci.ppEnabledLayerNames = &validationLayer;
    ci.pNext               = &dbgInfo;
#endif

    if (vkCreateInstance(&ci, nullptr, &m_instance) != VK_SUCCESS)
        return false;

#ifdef ENABLE_VALIDATION_LAYERS
    auto vkCreateDebugUtilsMessengerEXT_ =
        reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
    if (vkCreateDebugUtilsMessengerEXT_)
        vkCreateDebugUtilsMessengerEXT_(m_instance, &dbgInfo, nullptr, &m_debugMessenger);
#endif

    return true;
}

// ---------------------------------------------------------------------------
// selectPhysicalDevice — prefer discrete GPU, fall back to first available
// ---------------------------------------------------------------------------

bool Renderer::selectPhysicalDevice()
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) return false;

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    // Prefer discrete GPU.
    for (auto d : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(d, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            m_physDevice = d;
            return true;
        }
    }

    // Fallback: accept the first device (e.g. integrated GPU or software renderer).
    m_physDevice = devices[0];
    return true;
}

// ---------------------------------------------------------------------------
// createLogicalDevice — graphics queue; swapchain extension when not headless
// ---------------------------------------------------------------------------

bool Renderer::createLogicalDevice()
{
    // Find a queue family with graphics support.
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physDevice, &qfCount, qfProps.data());

    m_graphicsQueueFamily = UINT32_MAX;
    for (uint32_t i = 0; i < qfCount; ++i) {
        if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            m_graphicsQueueFamily = i;
            break;
        }
    }
    if (m_graphicsQueueFamily == UINT32_MAX) return false;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = m_graphicsQueueFamily;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &priority;

    std::vector<const char*> devExts;
    if (!m_headless)
        devExts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    VkPhysicalDeviceFeatures features{};

    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount    = 1;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = static_cast<uint32_t>(devExts.size());
    dci.ppEnabledExtensionNames = devExts.data();
    dci.pEnabledFeatures        = &features;

    if (vkCreateDevice(m_physDevice, &dci, nullptr, &m_device) != VK_SUCCESS)
        return false;

    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    m_presentQueue = m_graphicsQueue;  // same family for present (covers most hardware)
    return true;
}

// ---------------------------------------------------------------------------
// createAllocator — VMA allocator tied to the selected device
// ---------------------------------------------------------------------------

bool Renderer::createAllocator()
{
    VmaAllocatorCreateInfo info{};
    info.physicalDevice   = m_physDevice;
    info.device           = m_device;
    info.instance         = m_instance;
    info.vulkanApiVersion = VK_API_VERSION_1_3;

    return vmaCreateAllocator(&info, &m_allocator) == VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// createCommandPool — resettable pool on the graphics queue family
// ---------------------------------------------------------------------------

bool Renderer::createCommandPool()
{
    VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = m_graphicsQueueFamily;

    return vkCreateCommandPool(m_device, &ci, nullptr, &m_cmdPool) == VK_SUCCESS;
}

// ---------------------------------------------------------------------------
// Remaining private helpers
// ---------------------------------------------------------------------------

bool Renderer::createSwapchain(VkSurfaceKHR /*surface*/,
                               uint32_t     /*width*/,
                               uint32_t     /*height*/)
{
    // TODO: query surface capabilities, create VkSwapchainKHR, retrieve images + views
    return false;
}

// ---------------------------------------------------------------------------
// createRenderPasses — shadow (depth-only) + UI RT (RGBA8) +
//                      main MSAA (4x color+depth+resolve) + metrics overlay
// ---------------------------------------------------------------------------

bool Renderer::createRenderPasses()
{
    // --- 1. Shadow pass: depth-only, D32, stores result for later sampling ---
    {
        VkAttachmentDescription depthAttach{};
        depthAttach.format         = VK_FORMAT_D32_SFLOAT;
        depthAttach.samples        = VK_SAMPLE_COUNT_1_BIT;
        depthAttach.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttach.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttach.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttach.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttach.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pDepthStencilAttachment = &depthRef;

        // Ensure previous shadow-map shader reads complete before writing depth,
        // and depth writes complete before next frame's shader reads.
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass      = 0;
        deps[0].srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstStageMask    = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        deps[1].srcSubpass      = 0;
        deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask    = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpci.attachmentCount = 1;
        rpci.pAttachments    = &depthAttach;
        rpci.subpassCount    = 1;
        rpci.pSubpasses      = &subpass;
        rpci.dependencyCount = 2;
        rpci.pDependencies   = deps;

        if (vkCreateRenderPass(m_device, &rpci, nullptr, &m_shadowPass) != VK_SUCCESS)
            return false;
    }

    // --- 2. UI RT pass: RGBA8 color-only, 1x MSAA, transitions to SHADER_READ ---
    {
        VkAttachmentDescription colorAttach{};
        colorAttach.format         = VK_FORMAT_R8G8B8A8_UNORM;
        colorAttach.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttach.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttach.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttach.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttach.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttach.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorRef;

        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass      = 0;
        deps[0].srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        deps[1].srcSubpass      = 0;
        deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpci.attachmentCount = 1;
        rpci.pAttachments    = &colorAttach;
        rpci.subpassCount    = 1;
        rpci.pSubpasses      = &subpass;
        rpci.dependencyCount = 2;
        rpci.pDependencies   = deps;

        if (vkCreateRenderPass(m_device, &rpci, nullptr, &m_uiRTPass) != VK_SUCCESS)
            return false;
    }

    // --- 3. Main MSAA scene pass: 4x color + depth, resolve-to-output ---
    // Attachment layout: 0 = MSAA color (transient), 1 = MSAA depth (transient),
    //                    2 = resolve target (RenderTarget / swapchain image)
    {
        VkAttachmentDescription attaches[3]{};

        attaches[0].format         = m_colorFormat;
        attaches[0].samples        = VK_SAMPLE_COUNT_4_BIT;
        attaches[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attaches[0].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE; // transient
        attaches[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attaches[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attaches[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        attaches[0].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        attaches[1].format         = VK_FORMAT_D32_SFLOAT;
        attaches[1].samples        = VK_SAMPLE_COUNT_4_BIT;
        attaches[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attaches[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE; // transient
        attaches[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attaches[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attaches[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        attaches[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        attaches[2].format         = m_colorFormat;
        attaches[2].samples        = VK_SAMPLE_COUNT_1_BIT;
        attaches[2].loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attaches[2].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        attaches[2].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attaches[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attaches[2].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        attaches[2].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkAttachmentReference resolveRef{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 1;
        subpass.pColorAttachments       = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;
        subpass.pResolveAttachments     = &resolveRef;

        VkSubpassDependency dep{};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass    = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpci.attachmentCount = 3;
        rpci.pAttachments    = attaches;
        rpci.subpassCount    = 1;
        rpci.pSubpasses      = &subpass;
        rpci.dependencyCount = 1;
        rpci.pDependencies   = &dep;

        if (vkCreateRenderPass(m_device, &rpci, nullptr, &m_mainPass) != VK_SUCCESS)
            return false;
    }

    // --- 4. Metrics overlay pass: load existing output, no depth, no MSAA ---
    {
        VkAttachmentDescription colorAttach{};
        colorAttach.format         = m_colorFormat;
        colorAttach.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttach.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;   // preserve main pass output
        colorAttach.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttach.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttach.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttach.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorRef;

        VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpci.attachmentCount = 1;
        rpci.pAttachments    = &colorAttach;
        rpci.subpassCount    = 1;
        rpci.pSubpasses      = &subpass;

        if (vkCreateRenderPass(m_device, &rpci, nullptr, &m_metricsPass) != VK_SUCCESS)
            return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// createDescriptorSetLayouts — sets 0 / 1 / 2 per spec §6.3,
//                              plus the shared pipeline layout
// ---------------------------------------------------------------------------

bool Renderer::createDescriptorSetLayouts()
{
    // Set 0: SceneUBO (binding 0, VS+FS) + shadow map sampler (binding 1, FS)
    {
        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ci.bindingCount = 2;
        ci.pBindings    = bindings;
        if (vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &m_setLayout0) != VK_SUCCESS)
            return false;
    }

    // Set 1: SurfaceUBO (binding 0, VS+FS)
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding         = 0;
        binding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ci.bindingCount = 1;
        ci.pBindings    = &binding;
        if (vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &m_setLayout1) != VK_SUCCESS)
            return false;
    }

    // Set 2: UI atlas sampler (binding 0, FS) + offscreen RT sampler (binding 1, FS)
    {
        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ci.bindingCount = 2;
        ci.pBindings    = bindings;
        if (vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &m_setLayout2) != VK_SUCCESS)
            return false;
    }

    // Pipeline layout: 3 descriptor sets + one push constant (mat4 ortho, VS only)
    {
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset     = 0;
        pushRange.size       = 64; // sizeof(mat4)

        VkDescriptorSetLayout dsLayouts[3] = {m_setLayout0, m_setLayout1, m_setLayout2};

        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plci.setLayoutCount         = 3;
        plci.pSetLayouts            = dsLayouts;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges    = &pushRange;

        if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_pipelineLayout) != VK_SUCCESS)
            return false;
    }

    return true;
}

bool Renderer::createPipelines()
{
    // TODO: load SPIR-V, create all 5 pipelines using m_pipelineLayout
    return false;
}

// ---------------------------------------------------------------------------
// createDescriptorPool + allocateDescriptorSets
// ---------------------------------------------------------------------------

bool Renderer::createDescriptorPool()
{
    // 2 UBOs: SceneUBO (set 0, binding 0) + SurfaceUBO (set 1, binding 0)
    // 3 combined image samplers: shadow map + atlas + offscreen RT
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 2;
    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 3;

    VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    ci.maxSets       = 3;
    ci.poolSizeCount = 2;
    ci.pPoolSizes    = poolSizes;

    return vkCreateDescriptorPool(m_device, &ci, nullptr, &m_descPool) == VK_SUCCESS;
}

bool Renderer::allocateDescriptorSets()
{
    VkDescriptorSetLayout layouts[3] = {m_setLayout0, m_setLayout1, m_setLayout2};
    VkDescriptorSet       sets[3]    = {};

    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool     = m_descPool;
    ai.descriptorSetCount = 3;
    ai.pSetLayouts        = layouts;

    if (vkAllocateDescriptorSets(m_device, &ai, sets) != VK_SUCCESS)
        return false;

    m_set0 = sets[0];
    m_set1 = sets[1];
    m_set2 = sets[2];
    return true;
}

// ---------------------------------------------------------------------------
// createUniformBuffers — host-visible SceneUBO + SurfaceUBO, writes to sets
// ---------------------------------------------------------------------------

bool Renderer::createUniformBuffers()
{
    auto createHostBuffer = [&](VkDeviceSize size, VkBuffer& buf, VmaAllocation& alloc) -> bool {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size        = size;
        bci.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        return vmaCreateBuffer(m_allocator, &bci, &aci, &buf, &alloc, nullptr) == VK_SUCCESS;
    };

    if (!createHostBuffer(sizeof(SceneUBO),   m_sceneUBOBuf,   m_sceneUBOAlloc))   return false;
    if (!createHostBuffer(sizeof(SurfaceUBO), m_surfaceUBOBuf, m_surfaceUBOAlloc)) return false;

    // Bind the UBO buffers into their descriptor sets immediately.
    VkDescriptorBufferInfo sceneInfo{m_sceneUBOBuf,   0, sizeof(SceneUBO)};
    VkDescriptorBufferInfo surfaceInfo{m_surfaceUBOBuf, 0, sizeof(SurfaceUBO)};

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet          = m_set0;
    writes[0].dstBinding      = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo     = &sceneInfo;

    writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet          = m_set1;
    writes[1].dstBinding      = 0;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].pBufferInfo     = &surfaceInfo;

    vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);
    return true;
}

bool Renderer::createShadowResources()
{
    // TODO: create D32 shadow image (SHADOW_MAP_SIZE x SHADOW_MAP_SIZE),
    //       image view, framebuffer, sampler2DShadow
    return false;
}

void Renderer::destroySwapchain()
{
    // TODO: destroy swapchain image views, then swapchain
}
