#include <gtest/gtest.h>
#include "renderer.h"
#include "scene.h"
#include "ui_surface.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Project a world-space point through viewProj to screen-space pixel coords.
static glm::vec2 projectToScreen(glm::vec3 worldPos,
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

// Test if a pixel coordinate is inside a convex screen-space quad.
// quad[0..3] are the four screen-space corners in order (e.g. TL, TR, BR, BL).
// margin: allow N pixels outside the quad boundary.
static bool insideConvexQuad(glm::vec2 p,
                             const glm::vec2 quad[4],
                             float margin = 2.0f)
{
    for (int i = 0; i < 4; ++i) {
        glm::vec2 a = quad[i];
        glm::vec2 b = quad[(i + 1) % 4];
        glm::vec2 edge = b - a;
        glm::vec2 perp = {-edge.y, edge.x};  // inward normal (CCW winding)
        float d = glm::dot(p - a, perp);
        if (d < -margin) return false;
    }
    return true;
}

// Check if a pixel (r,g,b) is "magenta-like" within a tolerance.
static bool isMagenta(uint8_t r, uint8_t g, uint8_t b, uint8_t threshold = 32)
{
    return r > (255 - threshold) && g < threshold && b > (255 - threshold);
}

// ---------------------------------------------------------------------------
// UI Containment Test
// ---------------------------------------------------------------------------

class ContainmentTest : public ::testing::Test {
protected:
    static constexpr uint32_t FB_WIDTH  = 1280;
    static constexpr uint32_t FB_HEIGHT = 720;

    Renderer renderer;
    Scene    scene;

    void SetUp() override {
        ASSERT_TRUE(renderer.init(/*headless=*/true))
            << "Headless renderer init failed";
        scene.init();
    }

    void TearDown() override {
        renderer.cleanup();
    }
};

TEST_F(ContainmentTest, DirectMode_MagentaPixels_InsideSurfaceQuad)
{
    // Static surface — no animation, fixed position for deterministic test.
    glm::vec3 P00{-0.5f,  0.5f, 0.0f};
    glm::vec3 P10{ 0.5f,  0.5f, 0.0f};
    glm::vec3 P01{-0.5f, -0.5f, 0.0f};
    glm::vec3 P11{ 0.5f, -0.5f, 0.0f};

    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    glm::mat4 vp = proj * view;

    // TODO: allocate offscreen RenderTarget, record + submit one direct-mode frame,
    //       readback RGBA pixels into `pixels`.

    // Placeholder: skip test until renderer is implemented.
    std::vector<uint8_t> pixels; // FB_WIDTH * FB_HEIGHT * 4 bytes when implemented
    if (pixels.empty()) {
        GTEST_SKIP() << "Renderer not yet implemented — skipping pixel readback test";
    }

    // Project surface corners to screen space.
    glm::vec2 screenCorners[4] = {
        projectToScreen(P00, vp, FB_WIDTH, FB_HEIGHT),
        projectToScreen(P10, vp, FB_WIDTH, FB_HEIGHT),
        projectToScreen(P11, vp, FB_WIDTH, FB_HEIGHT),
        projectToScreen(P01, vp, FB_WIDTH, FB_HEIGHT),
    };

    // Scan every pixel; any magenta pixel must be inside the projected quad.
    int violations = 0;
    for (uint32_t y = 0; y < FB_HEIGHT; ++y) {
        for (uint32_t x = 0; x < FB_WIDTH; ++x) {
            const uint8_t* px = pixels.data() + (y * FB_WIDTH + x) * 4;
            if (isMagenta(px[0], px[1], px[2])) {
                glm::vec2 coord{static_cast<float>(x), static_cast<float>(y)};
                if (!insideConvexQuad(coord, screenCorners, /*margin=*/2.0f)) {
                    ++violations;
                }
            }
        }
    }

    EXPECT_EQ(violations, 0)
        << violations << " magenta pixel(s) found outside the surface quad boundary";
}
