#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>

namespace vku {

// Insert a pipeline barrier that transitions an image from one layout/access to another.
inline void imageBarrier(VkCommandBuffer cmd,
                         VkImage image,
                         VkImageLayout oldLayout, VkImageLayout newLayout,
                         VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                         VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                         VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout           = oldLayout;
    barrier.newLayout           = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange    = {aspectMask, 0, 1, 0, 1};
    barrier.srcAccessMask       = srcAccess;
    barrier.dstAccessMask       = dstAccess;

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);
}

// Allocate and begin a one-shot command buffer from cmdPool.
inline VkCommandBuffer beginOneShot(VkDevice device, VkCommandPool cmdPool)
{
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool        = cmdPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd{VK_NULL_HANDLE};
    vkAllocateCommandBuffers(device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    return cmd;
}

// Submit and wait for a one-shot command buffer, then free it.
inline void endOneShot(VkDevice device, VkCommandPool cmdPool,
                       VkQueue queue, VkCommandBuffer cmd)
{
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
}

// Upload data to a device-local buffer via a transient staging buffer.
inline void uploadBuffer(VmaAllocator allocator,
                         VkDevice device,
                         VkCommandPool cmdPool,
                         VkQueue queue,
                         VkBuffer dst,
                         const void* data,
                         VkDeviceSize size)
{
    // Create staging buffer (CPU visible)
    VkBufferCreateInfo stagingInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    stagingInfo.size        = size;
    stagingInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer      staging{VK_NULL_HANDLE};
    VmaAllocation stagingAlloc{VK_NULL_HANDLE};
    vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocInfo, &staging, &stagingAlloc, nullptr);

    // Copy data into staging
    void* mapped{nullptr};
    vmaMapMemory(allocator, stagingAlloc, &mapped);
    memcpy(mapped, data, size);
    vmaUnmapMemory(allocator, stagingAlloc);

    // Copy staging -> device-local
    VkCommandBuffer cmd = beginOneShot(device, cmdPool);
    VkBufferCopy copy{0, 0, size};
    vkCmdCopyBuffer(cmd, staging, dst, 1, &copy);
    endOneShot(device, cmdPool, queue, cmd);

    vmaDestroyBuffer(allocator, staging, stagingAlloc);
}

} // namespace vku
