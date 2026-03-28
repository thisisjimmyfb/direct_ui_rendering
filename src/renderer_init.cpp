#include <vk_mem_alloc.h>

#include "renderer.h"
#include "vk_utils.h"
#include "scene.h"
#include "ui_system.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

// ---------------------------------------------------------------------------
// Lifecycle - Initialization
// ---------------------------------------------------------------------------

bool Renderer::init(bool headless, GLFWwindow* window, const char* shaderDir)
{
    m_headless   = headless;
    m_shaderDir  = shaderDir ? shaderDir : SHADER_DIR;
    // Headless mode uses a known format; non-headless format is set by createSwapchain().
    if (m_headless) m_colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

    if (!createInstance())       return false;

    // Create the Vulkan surface from the GLFW window before device selection so
    // that present-queue compatibility can be checked if needed.
    if (!m_headless && window) {
        if (glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface) != VK_SUCCESS)
            return false;
    }

    if (!selectPhysicalDevice())       return false;
    if (!createLogicalDevice())        return false;
    if (!createAllocator())            return false;
    if (!createCommandPool())          return false;

    // Build the swapchain (sets m_colorFormat / m_swapFormat / m_swapExtent /
    // m_swapImages / m_swapImageViews and creates per-frame sync objects).
    if (!m_headless && m_surface) {
        if (!createSwapchain()) return false;
    }

    if (!createRenderPasses())         return false;  // uses m_colorFormat
    if (!createDescriptorSetLayouts()) return false;  // also creates m_pipelineLayout
    if (!createDescriptorPool())       return false;
    if (!allocateDescriptorSets())     return false;
    if (!createUniformBuffers())       return false;  // writes UBO descriptors into sets
    if (!createPipelines())            return false;  // needs pipeline layout + render passes
    if (!createShadowResources())      return false;
    if (!createSurfaceQuadBuffer())    return false;
    if (!m_headless) {
        if (!createFramebuffers())     return false;  // needs render passes (m_mainPass, m_metricsPass)
    }

    return true;
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
    features.shaderClipDistance = VK_TRUE;

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
// createSwapchain — surface capabilities, format/extent selection, image views, sync objects
// ---------------------------------------------------------------------------

bool Renderer::createSwapchain()
{
    // --- Surface capabilities ---
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physDevice, m_surface, &caps);

    // --- Choose surface format (prefer BGRA8_SRGB / SRGB_NONLINEAR) ---
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physDevice, m_surface, &formatCount, formats.data());

    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = f;
            break;
        }
    }
    m_swapFormat  = chosenFormat.format;
    m_colorFormat = chosenFormat.format;

    // --- Choose present mode (prefer MAILBOX for low-latency triple buffering) ---
    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physDevice, m_surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physDevice, m_surface, &modeCount, presentModes.data());

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // guaranteed available
    for (auto mode : presentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) { presentMode = mode; break; }
    }

    // --- Choose swap extent ---
    if (caps.currentExtent.width != UINT32_MAX) {
        m_swapExtent = caps.currentExtent;
    } else {
        // Surface lets us pick freely; clamp a sensible default.
        m_swapExtent.width  = std::max(caps.minImageExtent.width,
                              std::min(caps.maxImageExtent.width,  1280u));
        m_swapExtent.height = std::max(caps.minImageExtent.height,
                              std::min(caps.maxImageExtent.height, 720u));
    }

    // --- Image count: one more than minimum, up to the hardware maximum ---
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    // --- Create VkSwapchainKHR ---
    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface          = m_surface;
    sci.minImageCount    = imageCount;
    sci.imageFormat      = chosenFormat.format;
    sci.imageColorSpace  = chosenFormat.colorSpace;
    sci.imageExtent      = m_swapExtent;
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // graphics == present queue family
    sci.preTransform     = caps.currentTransform;
    sci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode      = presentMode;
    sci.clipped          = VK_TRUE;

    if (vkCreateSwapchainKHR(m_device, &sci, nullptr, &m_swapchain) != VK_SUCCESS)
        return false;

    // --- Retrieve swapchain images ---
    uint32_t swapImgCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapImgCount, nullptr);
    m_swapImages.resize(swapImgCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapImgCount, m_swapImages.data());

    // --- Create one image view per swapchain image ---
    m_swapImageViews.resize(swapImgCount);
    for (uint32_t i = 0; i < swapImgCount; ++i) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image    = m_swapImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = m_swapFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_swapImageViews[i]) != VK_SUCCESS)
            return false;
    }

    // --- Per-frame sync objects ---
    VkSemaphoreCreateInfo semCI{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceCI{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT; // start signalled so first frame doesn't block

    if (vkCreateSemaphore(m_device, &semCI,   nullptr, &m_imageAvailable) != VK_SUCCESS) return false;
    m_renderFinished.resize(swapImgCount);
    for (uint32_t i = 0; i < swapImgCount; ++i)
        if (vkCreateSemaphore(m_device, &semCI, nullptr, &m_renderFinished[i]) != VK_SUCCESS) return false;
    if (vkCreateFence    (m_device, &fenceCI, nullptr, &m_inFlightFence)  != VK_SUCCESS) return false;

    return true;
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
        deps[1].dependencyFlags = 0; // global dependency: shadow reads are cross-region

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
        colorAttach.finalLayout    = m_headless ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                                : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorRef;

        VkSubpassDependency metricsDep{};
        metricsDep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        metricsDep.dstSubpass    = 0;
        metricsDep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        metricsDep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        metricsDep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        metricsDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpci.attachmentCount = 1;
        rpci.pAttachments    = &colorAttach;
        rpci.subpassCount    = 1;
        rpci.pSubpasses      = &subpass;
        rpci.dependencyCount = 1;
        rpci.pDependencies   = &metricsDep;

        if (vkCreateRenderPass(m_device, &rpci, nullptr, &m_metricsPass) != VK_SUCCESS)
            return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// createDescriptorSetLayouts — sets 0 / 1 / 2 per spec,
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
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset     = 0;
        pushRange.size       = 80; // sizeof(mat4)=64 + sizeof(float)=4 + 12 pad

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

// ---------------------------------------------------------------------------
// createPipelines — load shaders and create all 6 pipelines
// ---------------------------------------------------------------------------

bool Renderer::createPipelines()
{
    // Helper: read a .spv file and create a VkShaderModule.
    auto loadShaderModule = [&](const char* relName) -> VkShaderModule {
        std::string path = m_shaderDir + relName;
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) {
            fprintf(stderr, "Renderer: cannot open shader: %s\n", path.c_str());
            return VK_NULL_HANDLE;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<char> buf(sz);
        fread(buf.data(), 1, static_cast<size_t>(sz), f);
        fclose(f);

        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = static_cast<size_t>(sz);
        ci.pCode    = reinterpret_cast<const uint32_t*>(buf.data());
        VkShaderModule mod{VK_NULL_HANDLE};
        vkCreateShaderModule(m_device, &ci, nullptr, &mod);
        return mod;
    };

    VkShaderModule vsRoom        = loadShaderModule("room.vert.spv");
    VkShaderModule fsRoom        = loadShaderModule("room.frag.spv");
    VkShaderModule vsUIDirect    = loadShaderModule("ui_direct.vert.spv");
    VkShaderModule fsUIDirect    = loadShaderModule("ui_direct.frag.spv");
    VkShaderModule vsUIOrtho     = loadShaderModule("ui_ortho.vert.spv");
    VkShaderModule fsUI          = loadShaderModule("ui.frag.spv");
    VkShaderModule fsComposite   = loadShaderModule("composite.frag.spv");
    VkShaderModule fsSurface     = loadShaderModule("surface.frag.spv");
    VkShaderModule vsQuad        = loadShaderModule("quad.vert.spv");
    VkShaderModule vsShadow      = loadShaderModule("shadow.vert.spv");

    auto destroyModules = [&]() {
        auto d = [&](VkShaderModule m) { if (m) vkDestroyShaderModule(m_device, m, nullptr); };
        d(vsRoom); d(fsRoom); d(vsUIDirect); d(fsUIDirect); d(vsUIOrtho);
        d(fsUI); d(fsComposite); d(fsSurface); d(vsQuad); d(vsShadow);
    };

    if (!vsRoom || !fsRoom || !vsUIDirect || !fsUIDirect || !vsUIOrtho || !fsUI || !fsComposite || !fsSurface || !vsQuad || !vsShadow) {
        destroyModules();
        return false;
    }

    // Common state shared across all pipelines.
    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates    = dynStates;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Pre-multiplied alpha blend state (used by all UI pipelines).
    VkPipelineColorBlendAttachmentState premulBlend{};
    premulBlend.blendEnable         = VK_TRUE;
    premulBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    premulBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    premulBlend.colorBlendOp        = VK_BLEND_OP_ADD;
    premulBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    premulBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    premulBlend.alphaBlendOp        = VK_BLEND_OP_ADD;
    premulBlend.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo premulBlendState{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    premulBlendState.attachmentCount = 1;
    premulBlendState.pAttachments    = &premulBlend;

    // Opaque blend (room geometry).
    VkPipelineColorBlendAttachmentState opaqueBlend{};
    opaqueBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo opaqueBlendState{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    opaqueBlendState.attachmentCount = 1;
    opaqueBlendState.pAttachments    = &opaqueBlend;

    // Vertex input: room geometry (Vertex: pos:vec3, normal:vec3, uv:vec2 = 32 bytes).
    VkVertexInputBindingDescription roomBinding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription roomAttrs[3]{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Vertex, uv)},
    };
    VkPipelineVertexInputStateCreateInfo roomVertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    roomVertexInput.vertexBindingDescriptionCount   = 1;
    roomVertexInput.pVertexBindingDescriptions      = &roomBinding;
    roomVertexInput.vertexAttributeDescriptionCount = 3;
    roomVertexInput.pVertexAttributeDescriptions    = roomAttrs;

    // Vertex input: UI (UIVertex: pos:vec2, uv:vec2 = 16 bytes).
    VkVertexInputBindingDescription uiBinding{0, sizeof(UIVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription uiAttrs[2]{
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, pos)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, uv)},
    };
    VkPipelineVertexInputStateCreateInfo uiVertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    uiVertexInput.vertexBindingDescriptionCount   = 1;
    uiVertexInput.pVertexBindingDescriptions      = &uiBinding;
    uiVertexInput.vertexAttributeDescriptionCount = 2;
    uiVertexInput.pVertexAttributeDescriptions    = uiAttrs;

    // Vertex input: composite quad (pos:vec3, uv:vec2 = 20 bytes).
    VkVertexInputBindingDescription quadBinding{0, sizeof(QuadVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription quadAttrs[2]{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(QuadVertex, pos)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(QuadVertex, uv)},
    };
    VkPipelineVertexInputStateCreateInfo quadVertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    quadVertexInput.vertexBindingDescriptionCount   = 1;
    quadVertexInput.pVertexBindingDescriptions      = &quadBinding;
    quadVertexInput.vertexAttributeDescriptionCount = 2;
    quadVertexInput.pVertexAttributeDescriptions    = quadAttrs;

    // --- 0. pipe_shadow: depth-only, renders room geometry using lightViewProj ---
    {
        VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stage.module = vsShadow;
        stage.pName  = "main";

        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode             = VK_POLYGON_MODE_FILL;
        raster.cullMode                = VK_CULL_MODE_FRONT_BIT;  // front-face culling for shadow maps
        raster.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth               = 1.0f;
        raster.depthBiasEnable         = VK_TRUE;
        raster.depthBiasConstantFactor = 1.25f;
        raster.depthBiasSlopeFactor    = 1.75f;

        VkPipelineMultisampleStateCreateInfo msaa{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable  = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        // Depth-only pass: no color attachments
        VkPipelineColorBlendStateCreateInfo colorBlend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlend.attachmentCount = 0;

        VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pci.stageCount          = 1;
        pci.pStages             = &stage;
        pci.pVertexInputState   = &roomVertexInput;
        pci.pInputAssemblyState = &inputAssembly;
        pci.pViewportState      = &viewportState;
        pci.pRasterizationState = &raster;
        pci.pMultisampleState   = &msaa;
        pci.pDepthStencilState  = &depth;
        pci.pColorBlendState    = &colorBlend;
        pci.pDynamicState       = &dynState;
        pci.layout              = m_pipelineLayout;
        pci.renderPass          = m_shadowPass;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeShadow)
                != VK_SUCCESS) {
            destroyModules();
            return false;
        }
    }

    // --- 1. pipe_room: Blinn-Phong room geometry, depth test+write, 4x MSAA ---
    {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_VERTEX_BIT,   vsRoom, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_FRAGMENT_BIT, fsRoom, "main", nullptr};

        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_BACK_BIT;
        raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable  = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pci.stageCount          = 2;
        pci.pStages             = stages;
        pci.pVertexInputState   = &roomVertexInput;
        pci.pInputAssemblyState = &inputAssembly;
        pci.pViewportState      = &viewportState;
        pci.pRasterizationState = &raster;
        pci.pMultisampleState   = &msaa;
        pci.pDepthStencilState  = &depth;
        pci.pColorBlendState    = &opaqueBlendState;
        pci.pDynamicState       = &dynState;
        pci.layout              = m_pipelineLayout;
        pci.renderPass          = m_mainPass;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeRoom)
                != VK_SUCCESS) {
            destroyModules();
            return false;
        }
    }

    // --- 2. pipe_ui_direct: UI in world space, clip distances, pre-multiplied alpha, 4x MSAA ---
    {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_VERTEX_BIT,   vsUIDirect,  "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_FRAGMENT_BIT, fsUIDirect,  "main", nullptr};

        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_NONE;
        raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable  = VK_TRUE;
        depth.depthWriteEnable = VK_FALSE;
        depth.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pci.stageCount          = 2;
        pci.pStages             = stages;
        pci.pVertexInputState   = &uiVertexInput;
        pci.pInputAssemblyState = &inputAssembly;
        pci.pViewportState      = &viewportState;
        pci.pRasterizationState = &raster;
        pci.pMultisampleState   = &msaa;
        pci.pDepthStencilState  = &depth;
        pci.pColorBlendState    = &premulBlendState;
        pci.pDynamicState       = &dynState;
        pci.layout              = m_pipelineLayout;
        pci.renderPass          = m_mainPass;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeUIDirect)
                != VK_SUCCESS) {
            destroyModules();
            return false;
        }
    }

    // --- 3. pipe_ui_rt: orthographic UI into offscreen RT, 1x MSAA, alpha blend ---
    {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_VERTEX_BIT,   vsUIOrtho, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_FRAGMENT_BIT, fsUI,      "main", nullptr};

        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_NONE;
        raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

        VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pci.stageCount          = 2;
        pci.pStages             = stages;
        pci.pVertexInputState   = &uiVertexInput;
        pci.pInputAssemblyState = &inputAssembly;
        pci.pViewportState      = &viewportState;
        pci.pRasterizationState = &raster;
        pci.pMultisampleState   = &msaa;
        pci.pDepthStencilState  = &depth;
        pci.pColorBlendState    = &premulBlendState;
        pci.pDynamicState       = &dynState;
        pci.layout              = m_pipelineLayout;
        pci.renderPass          = m_uiRTPass;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeUIRT)
                != VK_SUCCESS) {
            destroyModules();
            return false;
        }
    }

    // --- 4. pipe_composite: surface quad; teal base + UI RT blended on top, 4x MSAA ---
    {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_VERTEX_BIT,   vsQuad,      "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_FRAGMENT_BIT, fsComposite, "main", nullptr};

        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_NONE;
        raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable  = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;   // opaque — write depth
        depth.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pci.stageCount          = 2;
        pci.pStages             = stages;
        pci.pVertexInputState   = &quadVertexInput;
        pci.pInputAssemblyState = &inputAssembly;
        pci.pViewportState      = &viewportState;
        pci.pRasterizationState = &raster;
        pci.pMultisampleState   = &msaa;
        pci.pDepthStencilState  = &depth;
        pci.pColorBlendState    = &opaqueBlendState;  // shader outputs alpha=1
        pci.pDynamicState       = &dynState;
        pci.layout              = m_pipelineLayout;
        pci.renderPass          = m_mainPass;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeComposite)
                != VK_SUCCESS) {
            destroyModules();
            return false;
        }
    }

    // --- 4b. pipe_surface: opaque teal quad for direct mode (UI renders on top) ---
    {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_VERTEX_BIT,   vsQuad,    "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_FRAGMENT_BIT, fsSurface, "main", nullptr};

        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_NONE;
        raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable  = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pci.stageCount          = 2;
        pci.pStages             = stages;
        pci.pVertexInputState   = &quadVertexInput;
        pci.pInputAssemblyState = &inputAssembly;
        pci.pViewportState      = &viewportState;
        pci.pRasterizationState = &raster;
        pci.pMultisampleState   = &msaa;
        pci.pDepthStencilState  = &depth;
        pci.pColorBlendState    = &opaqueBlendState;
        pci.pDynamicState       = &dynState;
        pci.layout              = m_pipelineLayout;
        pci.renderPass          = m_mainPass;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeSurface)
                != VK_SUCCESS) {
            destroyModules();
            return false;
        }
    }

    // --- 5. pipe_metrics: orthographic HUD overlay, 1x MSAA, alpha blend ---
    {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_VERTEX_BIT,   vsUIOrtho, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_FRAGMENT_BIT, fsUI,      "main", nullptr};

        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_NONE;
        raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

        VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pci.stageCount          = 2;
        pci.pStages             = stages;
        pci.pVertexInputState   = &uiVertexInput;
        pci.pInputAssemblyState = &inputAssembly;
        pci.pViewportState      = &viewportState;
        pci.pRasterizationState = &raster;
        pci.pMultisampleState   = &msaa;
        pci.pDepthStencilState  = &depth;
        pci.pColorBlendState    = &premulBlendState;
        pci.pDynamicState       = &dynState;
        pci.layout              = m_pipelineLayout;
        pci.renderPass          = m_metricsPass;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeMetrics)
                != VK_SUCCESS) {
            destroyModules();
            return false;
        }
    }

    destroyModules();
    return true;
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
