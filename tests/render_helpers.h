#pragma once

#include <gtest/gtest.h>
#include "renderer.h"
#include "scene.h"
#include "ui_surface.h"
#include "ui_system.h"
#include "vk_utils.h"
#include <vector>
#include <cstdint>
#include <algorithm>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

// =============================================================================
// Render Helpers - Shared test utilities for Vulkan rendering tests
// =============================================================================

namespace render_helpers {

// -----------------------------------------------------------------------------
// Create a synthetic RGBA8 atlas where every pixel = (r, r, r, a)
// Uploads to GPU and populates image/view/sampler out-params.
// -----------------------------------------------------------------------------
inline void createAtlas(VkDevice device, VmaAllocator allocator, VkCommandPool cmdPool,
                        VkQueue graphicsQueue,
                        uint32_t dim, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                        VkImage& imgOut, VmaAllocation& allocOut,
                        VkImageView& viewOut, VkSampler& samplerOut)
{
    std::vector<uint8_t> pixels(dim * dim * 4);
    for (uint32_t i = 0; i < dim * dim; ++i) {
        pixels[i * 4 + 0] = r;
        pixels[i * 4 + 1] = g;
        pixels[i * 4 + 2] = b;
        pixels[i * 4 + 3] = a;
    }

    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ci.extent        = {dim, dim, 1};
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    EXPECT_EQ(vmaCreateImage(allocator, &ci, &ai,
                             &imgOut, &allocOut, nullptr), VK_SUCCESS);

    // Upload via staging buffer
    VkBuffer stagingBuf;
    VmaAllocation stagingAlloc;
    {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size        = pixels.size();
        bci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo sai{};
        sai.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        EXPECT_EQ(vmaCreateBuffer(allocator, &bci, &sai,
                                  &stagingBuf, &stagingAlloc, nullptr), VK_SUCCESS);
        void* mapped = nullptr;
        vmaMapMemory(allocator, stagingAlloc, &mapped);
        memcpy(mapped, pixels.data(), pixels.size());
        vmaUnmapMemory(allocator, stagingAlloc);
    }

    VkCommandBuffer uploadCmd = vku::beginOneShot(device, cmdPool);
    vku::imageBarrier(uploadCmd, imgOut,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent      = {dim, dim, 1};
    vkCmdCopyBufferToImage(uploadCmd, stagingBuf, imgOut,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vku::imageBarrier(uploadCmd, imgOut,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    vku::endOneShot(device, cmdPool, graphicsQueue, uploadCmd);
    vmaDestroyBuffer(allocator, stagingBuf, stagingAlloc);

    VkImageViewCreateInfo viewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewCI.image            = imgOut;
    viewCI.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format           = VK_FORMAT_R8G8B8A8_UNORM;
    viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    EXPECT_EQ(vkCreateImageView(device, &viewCI, nullptr, &viewOut), VK_SUCCESS);

    VkSamplerCreateInfo sampCI{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampCI.magFilter    = VK_FILTER_NEAREST;
    sampCI.minFilter    = VK_FILTER_NEAREST;
    sampCI.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    EXPECT_EQ(vkCreateSampler(device, &sampCI, nullptr, &samplerOut), VK_SUCCESS);
}

// -----------------------------------------------------------------------------
// Destroy an atlas (image, allocation, view, sampler)
// -----------------------------------------------------------------------------
inline void destroyAtlas(VkDevice device, VmaAllocator allocator,
                         VkImage& img, VmaAllocation& alloc,
                         VkImageView& view, VkSampler& sampler)
{
    if (sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }
    if (view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (img != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, img, alloc);
        img = VK_NULL_HANDLE;
    }
}

// -----------------------------------------------------------------------------
// Render one frame and read back pixels as flat RGBA buffer.
// directMode=true  -> shadow + main(direct)
// directMode=false -> shadow + UIRTPass + main(traditional)
// Parameters:
//   renderer      - Renderer instance with headless device ready
//   hrt           - HeadlessRenderTarget for the output
//   uiVtxBuf      - UI vertex buffer (can be VK_NULL_HANDLE for direct mode without UI)
//   uiVtxCount    - Number of UI vertices
//   directMode    - true for direct mode, false for traditional mode
//   uiOrtho       - Orthographic matrix for traditional mode UI RT pass (default: identity)
//   sdfThreshold  - SDF threshold push constant value (default: 0.0)
//   viewProj      - View-projection matrix for clip plane transforms (used in traditional mode)
// -----------------------------------------------------------------------------
inline std::vector<uint8_t> renderAndReadback(
    Renderer& renderer,
    HeadlessRenderTarget& hrt,
    VkBuffer uiVtxBuf,
    uint32_t uiVtxCount,
    bool directMode,
    const glm::mat4& uiOrtho = glm::mat4(1.0f),
    float sdfThreshold = 0.0f,
    const glm::mat4* viewProj = nullptr)
{
    const VkDeviceSize readbackSize =
        static_cast<VkDeviceSize>(hrt.rt.width) * hrt.rt.height * 4;

    VkBuffer readbackBuf = VK_NULL_HANDLE;
    VmaAllocation readbackAlloc = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size        = readbackSize;
        bci.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        EXPECT_EQ(vmaCreateBuffer(renderer.getAllocator(), &bci, &ai,
                                  &readbackBuf, &readbackAlloc, nullptr), VK_SUCCESS);
    }

    VkCommandBufferAllocateInfo cbAI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbAI.commandPool        = renderer.getCommandPool();
    cbAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAI.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    EXPECT_EQ(vkAllocateCommandBuffers(renderer.getDevice(), &cbAI, &cmd), VK_SUCCESS);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    renderer.recordShadowPass(cmd);

    if (!directMode) {
        renderer.recordUIRTPass(cmd, uiVtxBuf, uiVtxCount, uiOrtho, sdfThreshold);
    }

    renderer.recordMainPass(cmd, hrt.rt, directMode, uiVtxBuf, uiVtxCount, sdfThreshold);

    vku::imageBarrier(cmd, hrt.rt.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy copyRegion{};
    copyRegion.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.imageExtent       = {hrt.rt.width, hrt.rt.height, 1};
    vkCmdCopyImageToBuffer(cmd, hrt.rt.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readbackBuf, 1, &copyRegion);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;
    EXPECT_EQ(vkQueueSubmit(renderer.getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE),
              VK_SUCCESS);
    vkQueueWaitIdle(renderer.getGraphicsQueue());

    void* mapped = nullptr;
    vmaMapMemory(renderer.getAllocator(), readbackAlloc, &mapped);
    std::vector<uint8_t> pixels(readbackSize);
    memcpy(pixels.data(), mapped, static_cast<size_t>(readbackSize));
    vmaUnmapMemory(renderer.getAllocator(), readbackAlloc);

    vkFreeCommandBuffers(renderer.getDevice(), renderer.getCommandPool(), 1, &cmd);
    vmaDestroyBuffer(renderer.getAllocator(), readbackBuf, readbackAlloc);

    // Transition resolve image back to COLOR_ATTACHMENT_OPTIMAL for next render
    VkCommandBuffer resetCmd = vku::beginOneShot(renderer.getDevice(), renderer.getCommandPool());
    vku::imageBarrier(resetCmd, hrt.rt.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    vku::endOneShot(renderer.getDevice(), renderer.getCommandPool(),
                    renderer.getGraphicsQueue(), resetCmd);

    return pixels;
}

// -----------------------------------------------------------------------------
// Helper to check if an RGBA pixel is "magenta-like" within tolerance
// -----------------------------------------------------------------------------
inline bool isMagenta(uint8_t r, uint8_t g, uint8_t b, uint8_t threshold = 32)
{
    return r > (255 - threshold) && g < threshold && b > (255 - threshold);
}

// -----------------------------------------------------------------------------
// Count magenta pixels in a readback buffer
// -----------------------------------------------------------------------------
inline int countMagentaPixels(const std::vector<uint8_t>& pixels,
                              uint32_t width, uint32_t height)
{
    int count = 0;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* px = pixels.data() + (y * width + x) * 4;
            if (isMagenta(px[0], px[1], px[2])) {
                ++count;
            }
        }
    }
    return count;
}

// -----------------------------------------------------------------------------
// Helper to compute total absolute difference between two pixel buffers
// Returns 0 if buffers differ only by small floating-point noise (< threshold)
// -----------------------------------------------------------------------------
inline uint64_t computePixelDiff(const std::vector<uint8_t>& a,
                                 const std::vector<uint8_t>& b,
                                 uint64_t threshold = 0)
{
    EXPECT_EQ(a.size(), b.size());
    uint64_t totalDiff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        int diff = static_cast<int>(a[i]) - static_cast<int>(b[i]);
        totalDiff += static_cast<uint64_t>(std::abs(diff));
    }
    return totalDiff;
}

// -----------------------------------------------------------------------------
// Create a dummy 1x1 atlas (no staging upload, just layout transition)
// Used when atlas content doesn't matter (e.g., UI_TEST_COLOR mode)
// -----------------------------------------------------------------------------
inline void createDummyAtlas(VkDevice device, VmaAllocator allocator, VkCommandPool cmdPool,
                             VkQueue graphicsQueue,
                             VkImage& imgOut, VmaAllocation& allocOut,
                             VkImageView& viewOut, VkSampler& samplerOut)
{
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ci.extent        = {1, 1, 1};
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    EXPECT_EQ(vmaCreateImage(allocator, &ci, &ai,
                             &imgOut, &allocOut, nullptr), VK_SUCCESS);

    VkCommandBuffer transCmd = vku::beginOneShot(device, cmdPool);
    vku::imageBarrier(transCmd, imgOut,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        0, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vku::endOneShot(device, cmdPool, graphicsQueue, transCmd);

    VkImageViewCreateInfo viewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewCI.image            = imgOut;
    viewCI.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format           = VK_FORMAT_R8G8B8A8_UNORM;
    viewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    EXPECT_EQ(vkCreateImageView(device, &viewCI, nullptr, &viewOut), VK_SUCCESS);

    VkSamplerCreateInfo sampCI{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampCI.magFilter    = VK_FILTER_NEAREST;
    sampCI.minFilter    = VK_FILTER_NEAREST;
    sampCI.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    EXPECT_EQ(vkCreateSampler(device, &sampCI, nullptr, &samplerOut), VK_SUCCESS);
}

// -----------------------------------------------------------------------------
// Create a full-canvas UI vertex buffer (6 vertices for two triangles)
// -----------------------------------------------------------------------------
inline void createUIVertexBuffer(VmaAllocator allocator,
                                 VkBuffer& bufOut, VmaAllocation& allocOut)
{
    static constexpr uint32_t UI_VTX_COUNT = 6;
    const float W = static_cast<float>(W_UI);
    const float H = static_cast<float>(H_UI);
    UIVertex verts[UI_VTX_COUNT] = {
        {{0, 0}, {0, 0}}, {{W, 0}, {1, 0}}, {{W, H}, {1, 1}},
        {{0, 0}, {0, 0}}, {{W, H}, {1, 1}}, {{0, H}, {0, 1}},
    };

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size        = sizeof(verts);
    bci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    EXPECT_EQ(vmaCreateBuffer(allocator, &bci, &ai,
                              &bufOut, &allocOut, nullptr), VK_SUCCESS);
    void* mapped = nullptr;
    vmaMapMemory(allocator, allocOut, &mapped);
    memcpy(mapped, verts, sizeof(verts));
    vmaUnmapMemory(allocator, allocOut);
}

// -----------------------------------------------------------------------------
// Project a world-space point through viewProj to screen-space pixel coords.
// -----------------------------------------------------------------------------
inline glm::vec2 projectToScreen(glm::vec3 worldPos,
                                 const glm::mat4& viewProj,
                                 uint32_t width, uint32_t height)
{
    glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
    glm::vec3 ndc  = glm::vec3(clip) / clip.w;
    return {
        (ndc.x * 0.5f + 0.5f) * static_cast<float>(width),
        (ndc.y * 0.5f + 0.5f) * static_cast<float>(height)
    };
}

// -----------------------------------------------------------------------------
// Test if a pixel coordinate is inside a convex screen-space quad.
// quad[0..3] are the four screen-space corners in order.
// margin: allow N pixels outside the quad boundary.
// -----------------------------------------------------------------------------
inline bool insideConvexQuad(glm::vec2 p,
                             const glm::vec2 quad[4],
                             float margin = 2.0f)
{
    for (int i = 0; i < 4; ++i) {
        glm::vec2 a    = quad[i];
        glm::vec2 b    = quad[(i + 1) % 4];
        glm::vec2 edge = b - a;
        float len = glm::length(edge);
        if (len < 1e-6f) continue;  // degenerate edge
        // Normalised inward normal (CCW winding in screen space).
        // Dividing by len gives d in units of pixels, so margin is truly in pixels.
        glm::vec2 perp = glm::vec2{-edge.y, edge.x} / len;
        float d = glm::dot(p - a, perp);
        if (d < -margin) return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Axis-aligned bounding box of all magenta pixels in a readback buffer.
// -----------------------------------------------------------------------------
struct MagentaBBox {
    int minX{INT_MAX}, minY{INT_MAX};
    int maxX{INT_MIN}, maxY{INT_MIN};
    bool valid{false};
};

inline MagentaBBox computeMagentaBBox(const std::vector<uint8_t>& pixels,
                                      uint32_t width, uint32_t height)
{
    MagentaBBox bb;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* px = pixels.data() + (y * width + x) * 4;
            if (isMagenta(px[0], px[1], px[2])) {
                bb.valid = true;
                bb.minX  = std::min(bb.minX, (int)x);
                bb.minY  = std::min(bb.minY, (int)y);
                bb.maxX  = std::max(bb.maxX, (int)x);
                bb.maxY  = std::max(bb.maxY, (int)y);
            }
        }
    }
    return bb;
}

// -----------------------------------------------------------------------------
// Assert helper: check that two pixel buffers differ by more than threshold
// -----------------------------------------------------------------------------
inline void expectPixelsDiffer(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b,
                               uint64_t threshold = 0, const char* context = "")
{
    uint64_t diff = computePixelDiff(a, b, 0);
    std::string ctx = context[0] ? std::string(": ") + context : "";
    EXPECT_GT(diff, threshold) << "Pixel buffers should differ by > " << threshold << " total diff" << ctx;
}

// -----------------------------------------------------------------------------
// Destroy a vertex buffer and allocation
// -----------------------------------------------------------------------------
inline void destroyBuffer(VmaAllocator allocator, VkBuffer& buf, VmaAllocation& alloc)
{
    if (buf != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buf, alloc);
        buf = VK_NULL_HANDLE;
        alloc = {};
    }
}

} // namespace render_helpers
