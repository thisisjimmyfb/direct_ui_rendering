#include "renderer.h"
#include "vk_utils.h"
#include "scene.h"

#include <cstdio>

// ---------------------------------------------------------------------------
// Command buffer recording
// ---------------------------------------------------------------------------

void Renderer::recordShadowPass(VkCommandBuffer cmd)
{
    VkClearValue clearDepth{};
    clearDepth.depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpBI{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpBI.renderPass      = m_shadowPass;
    rpBI.framebuffer     = m_shadowFB;
    rpBI.renderArea      = {{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}};
    rpBI.clearValueCount = 1;
    rpBI.pClearValues    = &clearDepth;

    vkCmdBeginRenderPass(cmd, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

    if (m_pipeShadow != VK_NULL_HANDLE && m_roomIdxCount > 0 &&
        m_roomVtxBuf != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeShadow);

        VkViewport vp{0.0f, 0.0f, (float)SHADOW_MAP_SIZE, (float)SHADOW_MAP_SIZE, 0.0f, 1.0f};
        VkRect2D   sc{{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                0, 1, &m_set0, 0, nullptr);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_roomVtxBuf, &offset);
        vkCmdBindIndexBuffer(cmd, m_roomIdxBuf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, m_roomIdxCount, 1, 0, 0, 0);

        // Draw the floating UI surface quad so it casts a shadow onto room geometry.
        if (m_uiShadowVtxBuf != VK_NULL_HANDLE) {
            VkDeviceSize quadOffset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &m_uiShadowVtxBuf, &quadOffset);
            vkCmdDraw(cmd, 6, 1, 0, 0);
        }
    }

    vkCmdEndRenderPass(cmd);
}

void Renderer::recordUIRTPass(VkCommandBuffer cmd,
                              VkBuffer uiVtxBuf, uint32_t uiVtxCount,
                              const glm::mat4& ortho, float sdfThreshold)
{
    if (!ensureUIRTAllocated()) return;

    VkClearValue clearColor{};
    clearColor.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

    VkRenderPassBeginInfo rpBI{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpBI.renderPass      = m_uiRTPass;
    rpBI.framebuffer     = m_uiRTFB;
    rpBI.renderArea      = {{0, 0}, {W_UI, H_UI}};
    rpBI.clearValueCount = 1;
    rpBI.pClearValues    = &clearColor;

    vkCmdBeginRenderPass(cmd, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

    if (m_pipeUIRT != VK_NULL_HANDLE && uiVtxCount > 0 && uiVtxBuf != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeUIRT);

        VkViewport vp{0.0f, 0.0f, (float)W_UI, (float)H_UI, 0.0f, 1.0f};
        VkRect2D   sc{{0, 0}, {W_UI, H_UI}};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                2, 1, &m_set2, 0, nullptr);
        struct { glm::mat4 orthoMatrix; float sdfThreshold; float _pad[3]; } pc{ortho, sdfThreshold, {}};
        vkCmdPushConstants(cmd, m_pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 80, &pc);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &uiVtxBuf, &offset);
        vkCmdDraw(cmd, uiVtxCount, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void Renderer::recordMainPass(VkCommandBuffer cmd, RenderTarget& rt, bool directMode,
                              VkBuffer uiVtxBuf, uint32_t uiVtxCount, float sdfThreshold)
{
    VkClearValue clearValues[3]{};
    clearValues[0].color        = {{0.1f, 0.1f, 0.15f, 1.0f}};  // MSAA color clear
    clearValues[1].depthStencil = {1.0f, 0};                     // depth clear
    // clearValues[2]: resolve target — DONT_CARE (written by resolve)

    VkRenderPassBeginInfo rpBI{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpBI.renderPass      = m_mainPass;
    rpBI.framebuffer     = rt.framebuffer;
    rpBI.renderArea      = {{0, 0}, {rt.width, rt.height}};
    rpBI.clearValueCount = 3;
    rpBI.pClearValues    = clearValues;

    vkCmdBeginRenderPass(cmd, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{0.0f, 0.0f, (float)rt.width, (float)rt.height, 0.0f, 1.0f};
    VkRect2D   sc{{0, 0}, {rt.width, rt.height}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    // Room geometry
    if (m_pipeRoom != VK_NULL_HANDLE && m_roomIdxCount > 0 && m_roomVtxBuf != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeRoom);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                0, 1, &m_set0, 0, nullptr);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_roomVtxBuf, &offset);
        vkCmdBindIndexBuffer(cmd, m_roomIdxBuf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, m_roomIdxCount, 1, 0, 0, 0);
    }

    if (directMode) {
        // Direct mode: draw opaque teal quad first, then UI geometry on top.
        if (m_pipeSurface != VK_NULL_HANDLE && m_surfaceQuadBuf != VK_NULL_HANDLE) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeSurface);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                    0, 1, &m_set0, 0, nullptr);
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &m_surfaceQuadBuf, &offset);
            vkCmdDraw(cmd, 6, 1, 0, 0);
        }
        // UI geometry rendered directly into world space using M_total (clip-space offset).
        if (m_pipeUIDirect != VK_NULL_HANDLE && uiVtxCount > 0 && uiVtxBuf != VK_NULL_HANDLE) {
            vkCmdPushConstants(cmd, m_pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               64, sizeof(float), &sdfThreshold);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeUIDirect);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                    1, 1, &m_set1, 0, nullptr);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                    2, 1, &m_set2, 0, nullptr);
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &uiVtxBuf, &offset);
            vkCmdDraw(cmd, uiVtxCount, 1, 0, 0);
        }
    } else {
        // Traditional mode: composite the offscreen UI RT onto the teal surface quad.
        if (m_pipeComposite != VK_NULL_HANDLE && m_surfaceQuadBuf != VK_NULL_HANDLE) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeComposite);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                    0, 1, &m_set0, 0, nullptr);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                    2, 1, &m_set2, 0, nullptr);
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &m_surfaceQuadBuf, &offset);
            vkCmdDraw(cmd, 6, 1, 0, 0);
        }
    }

    vkCmdEndRenderPass(cmd);
}

void Renderer::recordMetricsPass(VkCommandBuffer cmd, RenderTarget& rt,
                                 VkBuffer hudVtxBuf, uint32_t hudVtxCount,
                                 const glm::mat4& ortho, float sdfThreshold)
{
    VkRenderPassBeginInfo rpBI{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpBI.renderPass      = m_metricsPass;
    rpBI.framebuffer     = rt.metricsFramebuffer;
    rpBI.renderArea      = {{0, 0}, {rt.width, rt.height}};
    rpBI.clearValueCount = 0;  // LOAD_OP_LOAD — preserve main pass output

    vkCmdBeginRenderPass(cmd, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

    if (m_pipeMetrics != VK_NULL_HANDLE && hudVtxCount > 0 && hudVtxBuf != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeMetrics);

        VkViewport vp{0.0f, 0.0f, (float)rt.width, (float)rt.height, 0.0f, 1.0f};
        VkRect2D   sc{{0, 0}, {rt.width, rt.height}};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                2, 1, &m_set2, 0, nullptr);
        struct { glm::mat4 orthoMatrix; float sdfThreshold; float _pad[3]; } pc{ortho, sdfThreshold, {}};
        vkCmdPushConstants(cmd, m_pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 80, &pc);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &hudVtxBuf, &offset);
        vkCmdDraw(cmd, hudVtxCount, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

// ---------------------------------------------------------------------------
// Frame helpers
// ---------------------------------------------------------------------------

bool Renderer::acquireSwapchainImage(uint32_t& imageIndex)
{
    if (m_headless || !m_swapchain) return false;

    // Wait for the previous frame to finish before reusing command buffers / UBOs.
    vkWaitForFences(m_device, 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences  (m_device, 1, &m_inFlightFence);

    VkResult result = vkAcquireNextImageKHR(
        m_device, m_swapchain, UINT64_MAX,
        m_imageAvailable, VK_NULL_HANDLE, &imageIndex);

    return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
}

void Renderer::presentSwapchainImage(uint32_t imageIndex)
{
    if (m_headless || !m_swapchain) return;

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &m_renderFinished[imageIndex];
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &m_swapchain;
    presentInfo.pImageIndices      = &imageIndex;

    vkQueuePresentKHR(m_presentQueue, &presentInfo);
}
