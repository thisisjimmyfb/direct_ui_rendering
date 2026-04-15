#pragma once

#include <gtest/gtest.h>
#include "renderer.h"
#include "scene.h"
#include "render_helpers.h"
#include "ui_surface.h"
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>

// Base fixture for SDF and headless render tests.
// Provides shared constants, common Vulkan resources, and render/readback helpers.
//
// Subclasses must implement SetUp/TearDown.  At the end of TearDown, after destroying
// any subclass-specific GPU resources, call cleanupBase().
//
// m_P00..m_P11 are the surface corners used by renderAndReadback_traditional.
// The defaults form a unit quad at z=0; override them in the subclass SetUp as needed.
class SDFRenderFixture : public ::testing::Test {
protected:
    static constexpr uint32_t FB_WIDTH  = 640;
    static constexpr uint32_t FB_HEIGHT = 360;
    static constexpr uint32_t ATLAS_DIM = 64;

    Renderer         renderer;
    Scene            scene;
    VkBuffer         uiVtxBuf{VK_NULL_HANDLE};
    VmaAllocation    uiVtxAlloc{};
    HeadlessRenderTarget hrt{};

    glm::vec3 m_P00{-0.5f,  0.5f, 0.0f};
    glm::vec3 m_P10{ 0.5f,  0.5f, 0.0f};
    glm::vec3 m_P01{-0.5f, -0.5f, 0.0f};
    glm::vec3 m_P11{ 0.5f, -0.5f, 0.0f};

    // Render one direct-mode frame and return flat RGBA pixels (FB_WIDTH * FB_HEIGHT * 4).
    std::vector<uint8_t> renderAndReadback(float sdfThreshold) {
        return render_helpers::renderAndReadback(
            renderer, hrt, uiVtxBuf, UI_VTX_COUNT, /*directMode=*/true,
            glm::mat4(1.0f), sdfThreshold);
    }

    // Render one traditional-mode frame (UI RT pass then composite) and return flat RGBA pixels.
    // Calls renderer.updateSurfaceQuad(m_P00..m_P11) before recording.
    std::vector<uint8_t> renderAndReadback_traditional(float sdfThreshold) {
        renderer.updateSurfaceQuad(m_P00, m_P10, m_P01, m_P11);
        const glm::mat4 ortho = glm::ortho(0.0f, static_cast<float>(W_UI),
                                            0.0f, static_cast<float>(H_UI),
                                            -1.0f, 1.0f);
        return render_helpers::renderAndReadback(
            renderer, hrt, uiVtxBuf, UI_VTX_COUNT, /*directMode=*/false,
            ortho, sdfThreshold);
    }

    // Destroy shared GPU resources. Call at the end of TearDown after releasing
    // any subclass-specific GPU resources (atlases, etc.).
    void cleanupBase() {
        renderer.destroyHeadlessRT(hrt);
        render_helpers::destroyBuffer(renderer.getAllocator(), uiVtxBuf, uiVtxAlloc);
        renderer.cleanup();
    }
};
