#include <vk_mem_alloc.h>

#include "renderer.h"
#include "vk_utils.h"
#include "scene.h"
#include "ui_system.h"

#ifndef __ANDROID__
#include <GLFW/glfw3.h>
#else
#include <vulkan/vulkan_android.h>
#endif

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

// ---------------------------------------------------------------------------
// Lifecycle - Initialization
// ---------------------------------------------------------------------------

bool Renderer::init(bool headless, const NativeWindowHandle& window, const char* shaderDir)
{
    m_headless   = headless;
    if (shaderDir) {
        m_shaderDir = shaderDir;
    } else {
#ifdef __ANDROID__
        // On Android, tests push shaders to the device and advertise the path
        // via SHADER_DIR_OVERRIDE so the filesystem loader can find them.
        const char* envDir = getenv("SHADER_DIR_OVERRIDE");
        m_shaderDir = envDir ? envDir : SHADER_DIR;
#else
        m_shaderDir = SHADER_DIR;
#endif
    }
    // Headless mode uses a known format; non-headless format is set by createSwapchain().
    if (m_headless) m_colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

    if (!createInstance())       return false;

    // Create the Vulkan surface from the native window before device selection so
    // that present-queue compatibility can be checked if needed.
    if (!m_headless && !window.isNull()) {
#ifdef __ANDROID__
        VkAndroidSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
        sci.window = window.androidWindow();
        if (vkCreateAndroidSurfaceKHR(m_instance, &sci, nullptr, &m_surface) != VK_SUCCESS)
            return false;
#else
        if (glfwCreateWindowSurface(m_instance, window.glfwWindow(), nullptr, &m_surface) != VK_SUCCESS)
            return false;
#endif
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
#ifdef __ANDROID__
        extensions.push_back("VK_KHR_surface");
        extensions.push_back("VK_KHR_android_surface");
#else
        uint32_t glfwCount = 0;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwCount);
        for (uint32_t i = 0; i < glfwCount; ++i)
            extensions.push_back(glfwExts[i]);
#endif
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
            m_maxAnisotropy = props.limits.maxSamplerAnisotropy;
            return true;
        }
    }

    // Fallback: accept the first device (e.g. integrated GPU or software renderer).
    m_physDevice = devices[0];
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physDevice, &props);
    m_maxAnisotropy = props.limits.maxSamplerAnisotropy;
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

    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(m_physDevice, &supportedFeatures);

    VkPhysicalDeviceFeatures features{};
    features.shaderClipDistance = VK_TRUE;
    if (supportedFeatures.samplerAnisotropy) {
        features.samplerAnisotropy = VK_TRUE;
        m_anisotropyEnabled = true;
    }

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

#ifdef __ANDROID__
    // Android: use dynamic function loading to avoid static references to
    // Vulkan 1.1/1.3 symbols that may not be available on older API levels.
    VmaVulkanFunctions vmaFuncs{};
    vmaFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaFuncs.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;
    info.pVulkanFunctions = &vmaFuncs;
    info.vulkanApiVersion = VK_API_VERSION_1_0;
#else
    info.vulkanApiVersion = VK_API_VERSION_1_3;
#endif

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
// createDescriptorSetLayouts — sets 0 / 1 / 2 per spec,
//                              plus the shared pipeline layout
// ---------------------------------------------------------------------------

bool Renderer::createDescriptorSetLayouts()
{
    // Set 0: SceneUBO (binding 0, VS+FS) + shadow map sampler (binding 1, FS)
    {
        VkDescriptorSetLayoutBinding bindings[2]{};

        bindings[0].binding       = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding       = 1;
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
        binding.binding       = 0;
        binding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ci.bindingCount = 1;
        ci.pBindings    = &binding;

        if (vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &m_setLayout1) != VK_SUCCESS)
            return false;
    }

    // Set 2: UI atlas (binding 0, FS) + offscreen RT (binding 1, FS)
    {
        VkDescriptorSetLayoutBinding bindings[2]{};

        bindings[0].binding       = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding       = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ci.bindingCount = 2;
        ci.pBindings    = bindings;

        if (vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &m_setLayout2) != VK_SUCCESS)
            return false;
    }

    // Pipeline layout: set0 + set1 + set2 (UI atlas) + push constants
    {
        VkDescriptorSetLayout layouts[] = { m_setLayout0, m_setLayout1, m_setLayout2 };
        // Push constant: orthoMatrix (mat4 = 64 bytes) + sdfThreshold (float) + padding (12 bytes) = 80 bytes
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset     = 0;
        pcRange.size       = 80;  // 64 (mat4) + 16 (float + pad)

        VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        ci.setLayoutCount       = 3;
        ci.pSetLayouts          = layouts;
        ci.pushConstantRangeCount = 1;
        ci.pPushConstantRanges    = &pcRange;

        if (vkCreatePipelineLayout(m_device, &ci, nullptr, &m_pipelineLayout) != VK_SUCCESS)
            return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// createDescriptorPool + allocateDescriptorSets
// ---------------------------------------------------------------------------

bool Renderer::createDescriptorPool()
{
    // 7 UBOs: SceneUBO (set 0) + 6x SurfaceUBO (one per cube face, set 1)
    // 3 combined image samplers: shadow map + atlas + offscreen RT
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 7;
    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 3;

    VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    ci.maxSets       = 8;  // set0 + 6*set1 + set2
    ci.poolSizeCount = 2;
    ci.pPoolSizes    = poolSizes;

    return vkCreateDescriptorPool(m_device, &ci, nullptr, &m_descPool) == VK_SUCCESS;
}

bool Renderer::allocateDescriptorSets()
{
    // Allocate set0 and set2 together with the first set1
    VkDescriptorSetLayout layouts8[8] = {
        m_setLayout0,
        m_setLayout1, m_setLayout1, m_setLayout1,
        m_setLayout1, m_setLayout1, m_setLayout1,
        m_setLayout2
    };
    VkDescriptorSet sets8[8] = {};

    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool     = m_descPool;
    ai.descriptorSetCount = 8;
    ai.pSetLayouts        = layouts8;

    if (vkAllocateDescriptorSets(m_device, &ai, sets8) != VK_SUCCESS)
        return false;

    m_set0 = sets8[0];
    for (int i = 0; i < 6; ++i) m_sets1[i] = sets8[1 + i];
    m_set2 = sets8[7];
    return true;
}

