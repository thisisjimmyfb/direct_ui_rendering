#include "ui_system.h"
#include "vk_utils.h"

// stb_image implementation — included once here.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <vector>
#include <cstring>

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

bool UISystem::init(VmaAllocator allocator,
                    VkDevice device,
                    VkCommandPool cmdPool,
                    VkQueue queue,
                    const char* atlasPath)
{
    m_allocator = allocator;
    m_device    = device;

    // Build glyph UV table.
    // Atlas layout: 16 glyphs per row, row-major order starting at ASCII 32 (space).
    constexpr float cellF  = static_cast<float>(GLYPH_CELL);
    constexpr float atlasF = static_cast<float>(ATLAS_SIZE);
    constexpr int glyphsPerRow = ATLAS_SIZE / GLYPH_CELL;  // 16

    for (int i = 0; i < 95; ++i) {
        int col = i % glyphsPerRow;
        int row = i / glyphsPerRow;
        m_glyphTable[i] = {
            (col     * cellF) / atlasF,
            (row     * cellF) / atlasF,
            ((col+1) * cellF) / atlasF,
            ((row+1) * cellF) / atlasF,
        };
    }

    // Load atlas PNG.
    int w, h, ch;
    stbi_uc* pixels = stbi_load(atlasPath, &w, &h, &ch, STBI_rgb_alpha);
    if (!pixels) {
        // Atlas not found — create a 1x1 placeholder so the app can still link.
        static stbi_uc dummy[4] = {255, 255, 255, 255};
        pixels = dummy;
        w = h = 1;
    }
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;

    // Create device-local image.
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent        = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vmaCreateImage(allocator, &imageInfo, &allocInfo, &m_atlasImage, &m_atlasAlloc, nullptr);

    // Upload via staging buffer.
    // (Create staging buffer, copy pixels, issue copy command)
    VkBufferCreateInfo stagingBufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    stagingBufInfo.size  = imageSize;
    stagingBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer stagingBuf{VK_NULL_HANDLE};
    VmaAllocation stagingAlloc{VK_NULL_HANDLE};
    vmaCreateBuffer(allocator, &stagingBufInfo, &stagingAllocInfo,
                    &stagingBuf, &stagingAlloc, nullptr);

    void* mapped{nullptr};
    vmaMapMemory(allocator, stagingAlloc, &mapped);
    memcpy(mapped, pixels, imageSize);
    vmaUnmapMemory(allocator, stagingAlloc);

    if (pixels != reinterpret_cast<stbi_uc*>(nullptr) + 0) {
        // Only free if stbi allocated it (not our dummy).
        // (Simple check: if w*h > 1 stbi allocated it)
        if (w * h > 1) stbi_image_free(pixels);
    }

    VkCommandBuffer cmd = vku::beginOneShot(device, cmdPool);

    vku::imageBarrier(cmd, m_atlasImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent      = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    vkCmdCopyBufferToImage(cmd, stagingBuf, m_atlasImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vku::imageBarrier(cmd, m_atlasImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    vku::endOneShot(device, cmdPool, queue, cmd);
    vmaDestroyBuffer(allocator, stagingBuf, stagingAlloc);

    // Image view
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image    = m_atlasImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(device, &viewInfo, nullptr, &m_atlasView);

    // Sampler (linear, clamp to edge)
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter  = VK_FILTER_LINEAR;
    samplerInfo.minFilter  = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device, &samplerInfo, nullptr, &m_atlasSampler);

    // Tessellate "Hello World" and upload to device-local vertex buffer.
    std::vector<UIVertex> helloVerts;
    tessellateString("Hello World", 8.0f, 8.0f, helloVerts);
    m_helloVertCount = static_cast<uint32_t>(helloVerts.size());

    VkDeviceSize vtxSize = m_helloVertCount * sizeof(UIVertex);

    VkBufferCreateInfo vtxBufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    vtxBufInfo.size  = vtxSize;
    vtxBufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo vtxAllocInfo{};
    vtxAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vmaCreateBuffer(allocator, &vtxBufInfo, &vtxAllocInfo,
                    &m_helloVtxBuf, &m_helloVtxAlloc, nullptr);

    vku::uploadBuffer(allocator, device, cmdPool, queue,
                      m_helloVtxBuf, helloVerts.data(), vtxSize);

    return true;
}

void UISystem::cleanup()
{
    if (m_device) {
        if (m_atlasSampler) { vkDestroySampler(m_device, m_atlasSampler, nullptr); }
        if (m_atlasView)    { vkDestroyImageView(m_device, m_atlasView, nullptr); }
    }
    if (m_allocator) {
        if (m_atlasImage)   { vmaDestroyImage(m_allocator, m_atlasImage, m_atlasAlloc); }
        if (m_helloVtxBuf)  { vmaDestroyBuffer(m_allocator, m_helloVtxBuf, m_helloVtxAlloc); }
    }
}

// ---------------------------------------------------------------------------
// Glyph tessellation
// ---------------------------------------------------------------------------

GlyphRect UISystem::uvForChar(char c) const
{
    int idx = static_cast<unsigned char>(c) - 32;
    if (idx < 0 || idx >= 95) idx = 0;   // fallback to space
    return m_glyphTable[idx];
}

uint32_t UISystem::tessellateString(std::string_view text, float x, float y,
                                    std::vector<UIVertex>& outVerts) const
{
    uint32_t count = 0;
    float cx = x;
    const float cellF = static_cast<float>(GLYPH_CELL);

    for (char c : text) {
        GlyphRect uv = uvForChar(c);

        float x0 = cx,        y0 = y;
        float x1 = cx + cellF, y1 = y + cellF;

        // Two triangles per glyph (6 vertices, no index buffer).
        outVerts.push_back({{x0, y0}, {uv.u0, uv.v0}});
        outVerts.push_back({{x1, y0}, {uv.u1, uv.v0}});
        outVerts.push_back({{x1, y1}, {uv.u1, uv.v1}});
        outVerts.push_back({{x0, y0}, {uv.u0, uv.v0}});
        outVerts.push_back({{x1, y1}, {uv.u1, uv.v1}});
        outVerts.push_back({{x0, y1}, {uv.u0, uv.v1}});

        cx += cellF;
        count += 6;
    }

    return count;
}
