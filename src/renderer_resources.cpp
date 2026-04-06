// VMA implementation — must appear in exactly one translation unit.
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "renderer.h"
#include "vk_utils.h"
#include "scene.h"
#include "msaa_config.h"

#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Lifecycle - Cleanup
// ---------------------------------------------------------------------------

void Renderer::cleanup()
{
    if (m_device) vkDeviceWaitIdle(m_device);

    // Pipelines
    if (m_pipeRoom)      { vkDestroyPipeline(m_device, m_pipeRoom,      nullptr); m_pipeRoom      = VK_NULL_HANDLE; }
    if (m_pipeUIDirect)  { vkDestroyPipeline(m_device, m_pipeUIDirect,  nullptr); m_pipeUIDirect  = VK_NULL_HANDLE; }
    if (m_pipeUIRT)      { vkDestroyPipeline(m_device, m_pipeUIRT,      nullptr); m_pipeUIRT      = VK_NULL_HANDLE; }
    if (m_pipeComposite) { vkDestroyPipeline(m_device, m_pipeComposite, nullptr); m_pipeComposite = VK_NULL_HANDLE; }
    if (m_pipeSurface)   { vkDestroyPipeline(m_device, m_pipeSurface,   nullptr); m_pipeSurface   = VK_NULL_HANDLE; }
    if (m_pipeMetrics)   { vkDestroyPipeline(m_device, m_pipeMetrics,   nullptr); m_pipeMetrics   = VK_NULL_HANDLE; }

    // Uniform buffers
    if (m_sceneUBOBuf)   { vmaDestroyBuffer(m_allocator, m_sceneUBOBuf,   m_sceneUBOAlloc);   m_sceneUBOBuf   = VK_NULL_HANDLE; }
    for (int i = 0; i < 6; ++i) {
        if (m_surfaceUBOBufs[i]) {
            vmaDestroyBuffer(m_allocator, m_surfaceUBOBufs[i], m_surfaceUBOAllocs[i]);
            m_surfaceUBOBufs[i] = VK_NULL_HANDLE;
        }
    }

    // Descriptor pool (frees all sets implicitly)
    if (m_descPool)    { vkDestroyDescriptorPool(m_device, m_descPool,   nullptr); m_descPool   = VK_NULL_HANDLE; }
    if (m_setLayout0)  { vkDestroyDescriptorSetLayout(m_device, m_setLayout0, nullptr); m_setLayout0 = VK_NULL_HANDLE; }
    if (m_setLayout1)  { vkDestroyDescriptorSetLayout(m_device, m_setLayout1, nullptr); m_setLayout1 = VK_NULL_HANDLE; }
    if (m_setLayout2)  { vkDestroyDescriptorSetLayout(m_device, m_setLayout2, nullptr); m_setLayout2 = VK_NULL_HANDLE; }
    if (m_pipelineLayout) { vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }

    // Shadow map resources (must be destroyed before allocator)
    if (m_shadowFB)      { vkDestroyFramebuffer(m_device, m_shadowFB,      nullptr); m_shadowFB      = VK_NULL_HANDLE; }
    if (m_shadowSampler) { vkDestroySampler    (m_device, m_shadowSampler, nullptr); m_shadowSampler = VK_NULL_HANDLE; }
    if (m_shadowView)    { vkDestroyImageView  (m_device, m_shadowView,    nullptr); m_shadowView    = VK_NULL_HANDLE; }

    // Shadow pipeline
    if (m_pipeShadow) { vkDestroyPipeline(m_device, m_pipeShadow, nullptr); m_pipeShadow = VK_NULL_HANDLE; }

    // UI RT resources
    if (m_uiRTFB)      { vkDestroyFramebuffer(m_device, m_uiRTFB,      nullptr); m_uiRTFB      = VK_NULL_HANDLE; }
    if (m_uiRTSampler) { vkDestroySampler    (m_device, m_uiRTSampler, nullptr); m_uiRTSampler = VK_NULL_HANDLE; }
    if (m_uiRTView)    { vkDestroyImageView  (m_device, m_uiRTView,    nullptr); m_uiRTView    = VK_NULL_HANDLE; }

    // Swapchain (destroys image views + VkSwapchainKHR)
    destroySwapchain();
    if (m_surface) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    // Sync objects
    if (m_imageAvailable) { vkDestroySemaphore(m_device, m_imageAvailable, nullptr); m_imageAvailable = VK_NULL_HANDLE; }
    for (auto& sem : m_renderFinished) if (sem) vkDestroySemaphore(m_device, sem, nullptr);
    m_renderFinished.clear();
    if (m_inFlightFence)  { vkDestroyFence    (m_device, m_inFlightFence,  nullptr); m_inFlightFence  = VK_NULL_HANDLE; }

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
        // Shadow image is VMA-allocated — destroy before allocator teardown.
        if (m_shadowImage)    { vmaDestroyImage (m_allocator, m_shadowImage,    m_shadowAlloc);    m_shadowImage    = VK_NULL_HANDLE; }
        if (m_uiRTImage)      { vmaDestroyImage (m_allocator, m_uiRTImage,      m_uiRTAlloc);      m_uiRTImage      = VK_NULL_HANDLE; }
        if (m_roomVtxBuf)     { vmaDestroyBuffer(m_allocator, m_roomVtxBuf,     m_roomVtxAlloc);   m_roomVtxBuf     = VK_NULL_HANDLE; }
        if (m_roomIdxBuf)     { vmaDestroyBuffer(m_allocator, m_roomIdxBuf,     m_roomIdxAlloc);   m_roomIdxBuf     = VK_NULL_HANDLE; }
        if (m_surfaceQuadBuf)   { vmaDestroyBuffer(m_allocator, m_surfaceQuadBuf,   m_surfaceQuadAlloc);   m_surfaceQuadBuf   = VK_NULL_HANDLE; }
        if (m_uiShadowVtxBuf)  { vmaDestroyBuffer(m_allocator, m_uiShadowVtxBuf,  m_uiShadowVtxAlloc);  m_uiShadowVtxBuf  = VK_NULL_HANDLE; }
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
    if (!m_surfaceUBOAllocs[0]) return;
    void* mapped{nullptr};
    vmaMapMemory(m_allocator, m_surfaceUBOAllocs[0], &mapped);
    memcpy(mapped, &data, sizeof(data));
    vmaUnmapMemory(m_allocator, m_surfaceUBOAllocs[0]);
}

void Renderer::updateFaceSurfaceUBOs(const std::array<SurfaceUBO, 6>& data)
{
    for (int i = 0; i < 6; ++i) {
        if (!m_surfaceUBOAllocs[i]) continue;
        void* mapped{nullptr};
        vmaMapMemory(m_allocator, m_surfaceUBOAllocs[i], &mapped);
        memcpy(mapped, &data[i], sizeof(SurfaceUBO));
        vmaUnmapMemory(m_allocator, m_surfaceUBOAllocs[i]);
    }
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

    if (!createHostBuffer(sizeof(SceneUBO), m_sceneUBOBuf, m_sceneUBOAlloc)) return false;
    for (int i = 0; i < 6; ++i) {
        if (!createHostBuffer(sizeof(SurfaceUBO), m_surfaceUBOBufs[i], m_surfaceUBOAllocs[i]))
            return false;
    }

    // Bind the UBO buffers into their descriptor sets immediately.
    VkDescriptorBufferInfo sceneInfo{m_sceneUBOBuf, 0, sizeof(SceneUBO)};

    VkWriteDescriptorSet writes[7]{};
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet          = m_set0;
    writes[0].dstBinding      = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo     = &sceneInfo;

    VkDescriptorBufferInfo surfaceInfos[6]{};
    for (int i = 0; i < 6; ++i) {
        surfaceInfos[i] = {m_surfaceUBOBufs[i], 0, sizeof(SurfaceUBO)};
        writes[1 + i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1 + i].dstSet          = m_sets1[i];
        writes[1 + i].dstBinding      = 0;
        writes[1 + i].descriptorCount = 1;
        writes[1 + i].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1 + i].pBufferInfo     = &surfaceInfos[i];
    }

    vkUpdateDescriptorSets(m_device, 7, writes, 0, nullptr);
    return true;
}

// ---------------------------------------------------------------------------
// createShadowResources
// ---------------------------------------------------------------------------

bool Renderer::createShadowResources()
{
    // Shadow depth image: SHADOW_MAP_SIZE x SHADOW_MAP_SIZE, D32, depth attachment + sampled.
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = VK_FORMAT_D32_SFLOAT;
    imageInfo.extent        = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1};
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(m_allocator, &imageInfo, &allocInfo,
                       &m_shadowImage, &m_shadowAlloc, nullptr) != VK_SUCCESS)
        return false;

    // Depth image view.
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image    = m_shadowImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_shadowView) != VK_SUCCESS)
        return false;

    // Framebuffer against the shadow render pass.
    VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbci.renderPass      = m_shadowPass;
    fbci.attachmentCount = 1;
    fbci.pAttachments    = &m_shadowView;
    fbci.width           = SHADOW_MAP_SIZE;
    fbci.height          = SHADOW_MAP_SIZE;
    fbci.layers          = 1;

    if (vkCreateFramebuffer(m_device, &fbci, nullptr, &m_shadowFB) != VK_SUCCESS)
        return false;

    // Comparison sampler for sampler2DShadow — lit outside shadow frustum (border = white).
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter     = VK_FILTER_LINEAR;
    samplerInfo.minFilter     = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor   = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp     = VK_COMPARE_OP_LESS_OR_EQUAL;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_shadowSampler) != VK_SUCCESS)
        return false;

    // Bind shadow map into descriptor set 0, binding 1.
    VkDescriptorImageInfo shadowImgInfo{};
    shadowImgInfo.sampler     = m_shadowSampler;
    shadowImgInfo.imageView   = m_shadowView;
    shadowImgInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet          = m_set0;
    write.dstBinding      = 1;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &shadowImgInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);

    return true;
}

// ---------------------------------------------------------------------------
// destroySwapchain
// ---------------------------------------------------------------------------

void Renderer::destroySwapchain()
{
    // MSAA transient attachments
    if (m_msaaColorView) { vkDestroyImageView(m_device, m_msaaColorView, nullptr); m_msaaColorView = VK_NULL_HANDLE; }
    if (m_msaaDepthView) { vkDestroyImageView(m_device, m_msaaDepthView, nullptr); m_msaaDepthView = VK_NULL_HANDLE; }
    if (m_msaaColorImg && m_allocator) { vmaDestroyImage(m_allocator, m_msaaColorImg, m_msaaColorAlloc); m_msaaColorImg = VK_NULL_HANDLE; }
    if (m_msaaDepthImg && m_allocator) { vmaDestroyImage(m_allocator, m_msaaDepthImg, m_msaaDepthAlloc); m_msaaDepthImg = VK_NULL_HANDLE; }

    // Per-swapchain framebuffers
    for (auto& rt : m_swapRTs) {
        if (rt.framebuffer)        vkDestroyFramebuffer(m_device, rt.framebuffer,        nullptr);
        if (rt.metricsFramebuffer) vkDestroyFramebuffer(m_device, rt.metricsFramebuffer, nullptr);
    }
    m_swapRTs.clear();

    for (auto iv : m_swapImageViews)
        vkDestroyImageView(m_device, iv, nullptr);
    m_swapImageViews.clear();

    if (m_swapchain) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
    m_swapImages.clear();
}

// ---------------------------------------------------------------------------
// createFramebuffers — MSAA transient images + per-swapchain FBs
// ---------------------------------------------------------------------------

bool Renderer::createFramebuffers()
{
    // MSAA color image (4x, same format as swapchain)
    {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.format        = m_colorFormat;
        ci.extent        = {m_swapExtent.width, m_swapExtent.height, 1};
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = msaaSampleCount();
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(m_allocator, &ci, &ai,
                           &m_msaaColorImg, &m_msaaColorAlloc, nullptr) != VK_SUCCESS)
            return false;

        VkImageViewCreateInfo viewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewCI.image    = m_msaaColorImg;
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format   = m_colorFormat;
        viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(m_device, &viewCI, nullptr, &m_msaaColorView) != VK_SUCCESS)
            return false;
    }

    // MSAA depth image (4x, D32)
    {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.format        = VK_FORMAT_D32_SFLOAT;
        ci.extent        = {m_swapExtent.width, m_swapExtent.height, 1};
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = msaaSampleCount();
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(m_allocator, &ci, &ai,
                           &m_msaaDepthImg, &m_msaaDepthAlloc, nullptr) != VK_SUCCESS)
            return false;

        VkImageViewCreateInfo viewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewCI.image    = m_msaaDepthImg;
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format   = VK_FORMAT_D32_SFLOAT;
        viewCI.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(m_device, &viewCI, nullptr, &m_msaaDepthView) != VK_SUCCESS)
            return false;
    }

    // Per-swapchain-image render targets and framebuffers
    m_swapRTs.resize(m_swapImages.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_swapImages.size()); ++i) {
        auto& rt = m_swapRTs[i];
        rt.image       = m_swapImages[i];
        rt.imageView   = m_swapImageViews[i];
        rt.width       = m_swapExtent.width;
        rt.height      = m_swapExtent.height;
        rt.isSwapchain = true;

        // Main pass FB: 3 attachments — MSAA color, MSAA depth, resolve target
        {
            VkImageView attachments[3] = {m_msaaColorView, m_msaaDepthView, m_swapImageViews[i]};
            VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fbci.renderPass      = m_mainPass;
            fbci.attachmentCount = 3;
            fbci.pAttachments    = attachments;
            fbci.width           = m_swapExtent.width;
            fbci.height          = m_swapExtent.height;
            fbci.layers          = 1;
            if (vkCreateFramebuffer(m_device, &fbci, nullptr, &rt.framebuffer) != VK_SUCCESS)
                return false;
        }

        // Metrics pass FB: 1 attachment — the resolved/final swapchain image
        {
            VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fbci.renderPass      = m_metricsPass;
            fbci.attachmentCount = 1;
            fbci.pAttachments    = &m_swapImageViews[i];
            fbci.width           = m_swapExtent.width;
            fbci.height          = m_swapExtent.height;
            fbci.layers          = 1;
            if (vkCreateFramebuffer(m_device, &fbci, nullptr, &rt.metricsFramebuffer) != VK_SUCCESS)
                return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// createSurfaceQuadBuffer — 6 QuadVertex host-visible buffer for composite mode
// ---------------------------------------------------------------------------

bool Renderer::createSurfaceQuadBuffer()
{
    // Composite quad (QuadVertex layout: pos vec3 + uv vec2)
    {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size        = sizeof(QuadVertex) * 36;
        bci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        if (vmaCreateBuffer(m_allocator, &bci, &aci,
                            &m_surfaceQuadBuf, &m_surfaceQuadAlloc, nullptr) != VK_SUCCESS)
            return false;
    }

    // Shadow cube (room Vertex layout: pos vec3 + normal vec3 + uv vec2) — 36 vertices for 6 faces
    {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size        = sizeof(Vertex) * 36;  // 6 faces × 6 vertices = 36
        bci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        if (vmaCreateBuffer(m_allocator, &bci, &aci,
                            &m_uiShadowVtxBuf, &m_uiShadowVtxAlloc, nullptr) != VK_SUCCESS)
            return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// ensureUIRTAllocated — lazily create offscreen RGBA8 UI render target
// ---------------------------------------------------------------------------

bool Renderer::ensureUIRTAllocated()
{
    if (m_uiRTImage != VK_NULL_HANDLE) return true;

    // RGBA8 image: W_UI x H_UI, color attachment + sampled
    {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.format        = VK_FORMAT_R8G8B8A8_UNORM;
        ci.extent        = {W_UI, H_UI, 1};
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(m_allocator, &ci, &ai, &m_uiRTImage, &m_uiRTAlloc, nullptr) != VK_SUCCESS)
            return false;
    }

    // Image view
    {
        VkImageViewCreateInfo viewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewCI.image    = m_uiRTImage;
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format   = VK_FORMAT_R8G8B8A8_UNORM;
        viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(m_device, &viewCI, nullptr, &m_uiRTView) != VK_SUCCESS)
            return false;
    }

    // Framebuffer against the UI RT render pass
    {
        VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass      = m_uiRTPass;
        fbci.attachmentCount = 1;
        fbci.pAttachments    = &m_uiRTView;
        fbci.width           = W_UI;
        fbci.height          = H_UI;
        fbci.layers          = 1;
        if (vkCreateFramebuffer(m_device, &fbci, nullptr, &m_uiRTFB) != VK_SUCCESS)
            return false;
    }

    // Linear sampler for the RT
    {
        VkSamplerCreateInfo samplerCI{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerCI.magFilter    = VK_FILTER_LINEAR;
        samplerCI.minFilter    = VK_FILTER_LINEAR;
        samplerCI.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(m_device, &samplerCI, nullptr, &m_uiRTSampler) != VK_SUCCESS)
            return false;
    }

    // Bind into descriptor set 2, binding 1
    {
        VkDescriptorImageInfo imgInfo{};
        imgInfo.sampler     = m_uiRTSampler;
        imgInfo.imageView   = m_uiRTView;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = m_set2;
        write.dstBinding      = 1;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo      = &imgInfo;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }

    return true;
}

// ---------------------------------------------------------------------------
// uploadSceneGeometry — upload room mesh to device-local GPU buffers
// ---------------------------------------------------------------------------

bool Renderer::uploadSceneGeometry(const Scene& scene)
{
    const auto& mesh = scene.roomMesh();
    if (mesh.vertices.empty() || mesh.indices.empty()) return false;

    m_roomIdxCount = static_cast<uint32_t>(mesh.indices.size());

    // Vertex buffer
    {
        VkDeviceSize size = sizeof(Vertex) * mesh.vertices.size();
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size  = size;
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateBuffer(m_allocator, &bci, &aci, &m_roomVtxBuf, &m_roomVtxAlloc, nullptr) != VK_SUCCESS)
            return false;

        vku::uploadBuffer(m_allocator, m_device, m_cmdPool, m_graphicsQueue,
                          m_roomVtxBuf, mesh.vertices.data(), size);
    }

    // Index buffer
    {
        VkDeviceSize size = sizeof(uint32_t) * mesh.indices.size();
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size  = size;
        bci.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateBuffer(m_allocator, &bci, &aci, &m_roomIdxBuf, &m_roomIdxAlloc, nullptr) != VK_SUCCESS)
            return false;

        vku::uploadBuffer(m_allocator, m_device, m_cmdPool, m_graphicsQueue,
                          m_roomIdxBuf, mesh.indices.data(), size);
    }

    return true;
}

// ---------------------------------------------------------------------------
// bindAtlasDescriptor — write UI glyph atlas into set 2 binding 0
// ---------------------------------------------------------------------------

void Renderer::bindAtlasDescriptor(VkImageView view, VkSampler sampler)
{
    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler     = sampler;
    imgInfo.imageView   = view;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_set2;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &imgInfo;

    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

// ---------------------------------------------------------------------------
// updateSurfaceQuad — write 6 QuadVertices into the host-visible surface buffer
// ---------------------------------------------------------------------------

void Renderer::updateSurfaceQuad(const glm::vec3& P00, const glm::vec3& P10,
                                  const glm::vec3& P01, const glm::vec3& P11)
{
    if (!m_surfaceQuadBuf) return;

    // Two CCW triangles: (P00, P10, P11) and (P00, P11, P01)
    QuadVertex verts[6] = {
        {P00, {0.0f, 0.0f}},
        {P10, {1.0f, 0.0f}},
        {P11, {1.0f, 1.0f}},
        {P00, {0.0f, 0.0f}},
        {P11, {1.0f, 1.0f}},
        {P01, {0.0f, 1.0f}},
    };

    void* mapped = nullptr;
    vmaMapMemory(m_allocator, m_surfaceQuadAlloc, &mapped);
    memcpy(mapped, verts, sizeof(verts));
    vmaUnmapMemory(m_allocator, m_surfaceQuadAlloc);
}

void Renderer::updateCubeSurface(const std::array<std::array<glm::vec3, 4>, 6>& faceCorners)
{
    if (!m_surfaceQuadBuf) return;

    // 6 faces, 2 triangles each (6 vertices per face), 1 faceIndex per vertex
    // Each face's corners: P_00(0,0), P_10(1,0), P_11(1,1), P_01(0,1)
    // Triangles: (0,1,2) and (0,2,3) for CCW winding
    QuadVertex verts[36];
    for (int face = 0; face < 6; ++face) {
        const auto& f = faceCorners[face];
        int base = face * 6;
        verts[base + 0] = {f[0], {0.0f, 0.0f}, face};  // P_00
        verts[base + 1] = {f[1], {1.0f, 0.0f}, face};  // P_10
        verts[base + 2] = {f[3], {1.0f, 1.0f}, face};  // P_11
        verts[base + 3] = {f[0], {0.0f, 0.0f}, face};  // P_00
        verts[base + 4] = {f[3], {1.0f, 1.0f}, face};  // P_11
        verts[base + 5] = {f[2], {0.0f, 1.0f}, face};  // P_01
    }

    void* mapped = nullptr;
    vmaMapMemory(m_allocator, m_surfaceQuadAlloc, &mapped);
    memcpy(mapped, verts, sizeof(verts));
    vmaUnmapMemory(m_allocator, m_surfaceQuadAlloc);
}

// ---------------------------------------------------------------------------
// updateUIShadowCube — write 36 room-Vertex-layout verts for shadow casting (6 faces)
// ---------------------------------------------------------------------------

void Renderer::updateUIShadowCube(const std::array<std::array<glm::vec3, 4>, 6>& faceCorners)
{
    if (!m_uiShadowVtxBuf) return;

    // 6 faces, 2 triangles each (6 vertices per face)
    // Each face's corners: P_00(0,0), P_10(1,0), P_11(1,1), P_01(0,1)
    // Triangles: (0,1,2) and (0,2,3) for CCW winding
    Vertex verts[36];
    for (int face = 0; face < 6; ++face) {
        const auto& f = faceCorners[face];

        // Compute surface normal from edge vectors.
        glm::vec3 eu = f[1] - f[0];
        glm::vec3 ev = f[2] - f[0];
        glm::vec3 n  = glm::normalize(glm::cross(eu, ev));

        int base = face * 6;
        // Two CCW triangles: (P00, P10, P11) and (P00, P11, P01)
        verts[base + 0] = {f[0], n, {0.0f, 0.0f}};  // P_00
        verts[base + 1] = {f[1], n, {1.0f, 0.0f}};  // P_10
        verts[base + 2] = {f[3], n, {1.0f, 1.0f}};  // P_11
        verts[base + 3] = {f[0], n, {0.0f, 0.0f}};  // P_00
        verts[base + 4] = {f[3], n, {1.0f, 1.0f}};  // P_11
        verts[base + 5] = {f[2], n, {0.0f, 1.0f}};  // P_01
    }

    void* mapped = nullptr;
    vmaMapMemory(m_allocator, m_uiShadowVtxAlloc, &mapped);
    memcpy(mapped, verts, sizeof(verts));
    vmaUnmapMemory(m_allocator, m_uiShadowVtxAlloc);
}

// ---------------------------------------------------------------------------
// getSwapchainRT — return the RenderTarget for the given swapchain image index
// ---------------------------------------------------------------------------

RenderTarget& Renderer::getSwapchainRT(uint32_t imageIndex)
{
    return m_swapRTs[imageIndex];
}

// ---------------------------------------------------------------------------
// createHeadlessRT / destroyHeadlessRT / initOffscreenRT — headless test support
// ---------------------------------------------------------------------------

bool Renderer::createHeadlessRT(uint32_t width, uint32_t height, HeadlessRenderTarget& hrt)
{
    VmaAllocationCreateInfo gpuOnly{};
    gpuOnly.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    // 1. Resolve target (1x, COLOR_ATTACHMENT + TRANSFER_SRC)
    {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.format        = m_colorFormat;
        ci.extent        = {width, height, 1};
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vmaCreateImage(m_allocator, &ci, &gpuOnly,
                           &hrt.rt.image, &hrt.resolveAlloc, nullptr) != VK_SUCCESS)
            return false;

        VkImageViewCreateInfo viewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewCI.image            = hrt.rt.image;
        viewCI.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format           = m_colorFormat;
        viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(m_device, &viewCI, nullptr, &hrt.rt.imageView) != VK_SUCCESS)
            return false;
    }

    // 2. MSAA color (4x, TRANSIENT + COLOR_ATTACHMENT)
    {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.format        = m_colorFormat;
        ci.extent        = {width, height, 1};
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = msaaSampleCount();
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vmaCreateImage(m_allocator, &ci, &gpuOnly,
                           &hrt.msaaColor, &hrt.msaaColorAlloc, nullptr) != VK_SUCCESS)
            return false;

        VkImageViewCreateInfo viewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewCI.image            = hrt.msaaColor;
        viewCI.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format           = m_colorFormat;
        viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(m_device, &viewCI, nullptr, &hrt.msaaColorView) != VK_SUCCESS)
            return false;
    }

    // 3. MSAA depth (4x, TRANSIENT + DEPTH_STENCIL)
    {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.format        = VK_FORMAT_D32_SFLOAT;
        ci.extent        = {width, height, 1};
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = msaaSampleCount();
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vmaCreateImage(m_allocator, &ci, &gpuOnly,
                           &hrt.msaaDepth, &hrt.msaaDepthAlloc, nullptr) != VK_SUCCESS)
            return false;

        VkImageViewCreateInfo viewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewCI.image            = hrt.msaaDepth;
        viewCI.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format           = VK_FORMAT_D32_SFLOAT;
        viewCI.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(m_device, &viewCI, nullptr, &hrt.msaaDepthView) != VK_SUCCESS)
            return false;
    }

    // 4. Main pass framebuffer (MSAA color, MSAA depth, resolve)
    {
        VkImageView attachments[3] = {hrt.msaaColorView, hrt.msaaDepthView, hrt.rt.imageView};
        VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass      = m_mainPass;
        fbci.attachmentCount = 3;
        fbci.pAttachments    = attachments;
        fbci.width           = width;
        fbci.height          = height;
        fbci.layers          = 1;
        if (vkCreateFramebuffer(m_device, &fbci, nullptr, &hrt.rt.framebuffer) != VK_SUCCESS)
            return false;
    }

    hrt.rt.width      = width;
    hrt.rt.height     = height;
    hrt.rt.isSwapchain = false;
    return true;
}

void Renderer::destroyHeadlessRT(HeadlessRenderTarget& hrt)
{
    if (hrt.rt.framebuffer) {
        vkDestroyFramebuffer(m_device, hrt.rt.framebuffer, nullptr);
        hrt.rt.framebuffer = VK_NULL_HANDLE;
    }
    if (hrt.msaaDepthView) {
        vkDestroyImageView(m_device, hrt.msaaDepthView, nullptr);
        hrt.msaaDepthView = VK_NULL_HANDLE;
    }
    if (hrt.msaaColorView) {
        vkDestroyImageView(m_device, hrt.msaaColorView, nullptr);
        hrt.msaaColorView = VK_NULL_HANDLE;
    }
    if (hrt.rt.imageView) {
        vkDestroyImageView(m_device, hrt.rt.imageView, nullptr);
        hrt.rt.imageView = VK_NULL_HANDLE;
    }
    if (hrt.msaaDepth) {
        vmaDestroyImage(m_allocator, hrt.msaaDepth, hrt.msaaDepthAlloc);
        hrt.msaaDepth = VK_NULL_HANDLE;
    }
    if (hrt.msaaColor) {
        vmaDestroyImage(m_allocator, hrt.msaaColor, hrt.msaaColorAlloc);
        hrt.msaaColor = VK_NULL_HANDLE;
    }
    if (hrt.rt.image) {
        vmaDestroyImage(m_allocator, hrt.rt.image, hrt.resolveAlloc);
        hrt.rt.image = VK_NULL_HANDLE;
    }
}

bool Renderer::initOffscreenRT()
{
    return ensureUIRTAllocated();
}
