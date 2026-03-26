// VMA implementation — must appear in exactly one translation unit.
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "renderer.h"
#include "vk_utils.h"

#include <cstring>
#include <stdexcept>
#include <vector>

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool Renderer::init(bool headless)
{
    m_headless = headless;

    if (!createInstance())         return false;
    if (!selectPhysicalDevice())   return false;
    if (!createLogicalDevice())    return false;
    if (!createAllocator())        return false;
    if (!createCommandPool())      return false;
    if (!createRenderPasses())     return false;
    if (!createDescriptorSetLayouts()) return false;
    if (!createPipelines())        return false;
    if (!createDescriptorPool())   return false;
    if (!allocateDescriptorSets()) return false;
    if (!createUniformBuffers())   return false;
    if (!createShadowResources())  return false;

    return true;
}

void Renderer::cleanup()
{
    if (m_device) vkDeviceWaitIdle(m_device);

    // TODO: destroy all resources in reverse creation order

    if (m_allocator)  { vmaDestroyAllocator(m_allocator);  m_allocator = VK_NULL_HANDLE; }
    if (m_device)     { vkDestroyDevice(m_device, nullptr); m_device = VK_NULL_HANDLE; }
    if (m_instance)   { vkDestroyInstance(m_instance, nullptr); m_instance = VK_NULL_HANDLE; }
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
// Private helpers (stubs)
// ---------------------------------------------------------------------------

bool Renderer::createInstance()
{
    // TODO: fill VkApplicationInfo, VkInstanceCreateInfo, enable validation in Debug
    return false;
}

bool Renderer::selectPhysicalDevice()
{
    // TODO: enumerate physical devices, pick first discrete GPU
    return false;
}

bool Renderer::createLogicalDevice()
{
    // TODO: create VkDevice with graphics queue
    return false;
}

bool Renderer::createAllocator()
{
    // TODO: fill VmaAllocatorCreateInfo and call vmaCreateAllocator
    return false;
}

bool Renderer::createCommandPool()
{
    // TODO: vkCreateCommandPool on graphics queue family
    return false;
}

bool Renderer::createSwapchain(VkSurfaceKHR /*surface*/,
                               uint32_t     /*width*/,
                               uint32_t     /*height*/)
{
    // TODO: query surface capabilities, create VkSwapchainKHR, retrieve images + views
    return false;
}

bool Renderer::createRenderPasses()
{
    // TODO: create m_shadowPass, m_uiRTPass, m_mainPass (MSAA), m_metricsPass
    return false;
}

bool Renderer::createDescriptorSetLayouts()
{
    // TODO: create m_setLayout0/1/2 per spec §6.3
    return false;
}

bool Renderer::createPipelines()
{
    // TODO: load SPIR-V, create pipeline layout, create all 5 pipelines
    return false;
}

bool Renderer::createDescriptorPool()
{
    // TODO: vkCreateDescriptorPool with enough UBO/sampler descriptors
    return false;
}

bool Renderer::allocateDescriptorSets()
{
    // TODO: vkAllocateDescriptorSets for sets 0/1/2, write UBOs and samplers
    return false;
}

bool Renderer::createUniformBuffers()
{
    // TODO: allocate host-visible VkBuffer for SceneUBO and SurfaceUBO via VMA
    return false;
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
