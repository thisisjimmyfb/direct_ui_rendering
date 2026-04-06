#include <gtest/gtest.h>
#include "containment_fixture.h"
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Shadow Acne on Walls Test
//
// Shadow acne manifests as visual artifacts (banding, moire patterns, speckling)
// on surfaces that are in shadow. This test detects these artifacts by:
//
// 1. Rendering a wall surface that is being self-shadowed
// 2. Sampling a region of the shadow to measure variance and detect artifacts
// 3. Checking for banding patterns which indicate shadow acne
//
// The test looks for:
// - Excessive variance in shadow regions (banding/artifacts)
// - Smooth gradients without sharp discontinuities
// ---------------------------------------------------------------------------

class ShadowAcneWallTest : public ContainmentTest {
protected:
    // Helper to compute the standard deviation of luminance in a rectangular region
    float computeLuminanceStdDev(const std::vector<uint8_t>& pixels,
                                  int startX, int startY,
                                  int width, int height)
    {
        if (width <= 0 || height <= 0) return 0.0f;

        // Compute mean luminance
        double sumLum = 0.0;
        int pixelCount = 0;
        std::vector<float> luminances;

        for (int y = startY; y < startY + height; ++y) {
            for (int x = startX; x < startX + width; ++x) {
                if (x < 0 || x >= (int)FB_WIDTH || y < 0 || y >= (int)FB_HEIGHT)
                    continue;

                const uint8_t* px = pixels.data() + (y * FB_WIDTH + x) * 4;
                float r = px[0] / 255.0f;
                float g = px[1] / 255.0f;
                float b = px[2] / 255.0f;
                float lum = 0.299f * r + 0.587f * g + 0.114f * b;
                luminances.push_back(lum);
                sumLum += lum;
                ++pixelCount;
            }
        }

        if (pixelCount == 0) return 0.0f;

        double meanLum = sumLum / pixelCount;

        // Compute standard deviation
        double sumSqDiff = 0.0;
        for (float lum : luminances) {
            double diff = lum - meanLum;
            sumSqDiff += diff * diff;
        }

        double variance = sumSqDiff / pixelCount;
        return static_cast<float>(std::sqrt(variance));
    }

    // Helper to detect banding artifacts by looking at horizontal slices
    // Returns the maximum variance across horizontal scanlines
    float detectBandingArtifacts(const std::vector<uint8_t>& pixels,
                                  int startX, int startY,
                                  int width, int height,
                                  int sliceHeight = 5)
    {
        float maxBandingVariance = 0.0f;

        // Divide the region into horizontal slices and check variance within each
        for (int y = startY; y < startY + height; y += sliceHeight) {
            int sliceH = std::min(sliceHeight, startY + height - y);
            float sliceVar = computeLuminanceStdDev(pixels, startX, y, width, sliceH);
            maxBandingVariance = std::max(maxBandingVariance, sliceVar);
        }

        return maxBandingVariance;
    }
};

// ---------------------------------------------------------------------------
// Test: Shadow acne on back wall self-shadowing area
//
// The back wall (z = -3) faces the +Z direction (normal = (0, 0, 1)).
// The spotlight direction is roughly toward (0, -1.3, -3.5), which means
// the back wall normal is nearly perpendicular to the light direction.
// This creates a self-shadowing scenario where depth bias is critical.
//
// Without proper bias, shadow acne appears as banding or speckling on the wall.
// The test renders the wall and checks that shadow artifacts are minimal.
// ---------------------------------------------------------------------------
TEST_F(ShadowAcneWallTest, BackWall_NoSelfShadowAcne_DirectMode)
{
    // Camera looking directly at the back wall center
    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 1.5f, 2.0f),      // camera position (inside, looking back)
        glm::vec3(0.0f, 1.5f, -3.0f),     // looking at back wall center
        glm::vec3(0.0f, 1.0f, 0.0f));     // up

    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    // Setup scene
    SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
    renderer.updateSceneUBO(sceneUBO);

    // Dummy surface
    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = glm::mat4(1.0f);
    surfaceUBO.worldMatrix = glm::mat4(1.0f);
    surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
    renderer.updateSurfaceUBO(surfaceUBO);

    auto pixels = renderAndReadback(/*directMode=*/true);

    // Define the center region of the back wall in screen space
    // The back wall should fill most of the screen from this view
    const int stripLeft   = FB_WIDTH / 4;
    const int stripRight  = 3 * FB_WIDTH / 4;
    const int stripTop    = FB_HEIGHT / 4;
    const int stripBottom = 3 * FB_HEIGHT / 4;
    const int stripWidth  = stripRight - stripLeft;
    const int stripHeight = stripBottom - stripTop;

    // Measure banding artifacts
    float bandingVariance = detectBandingArtifacts(pixels,
                                                    stripLeft, stripTop,
                                                    stripWidth, stripHeight,
                                                    /*sliceHeight=*/8);

    // Banding artifacts would show as high variance within horizontal slices.
    // With proper bias, variance should be low (shadow should be smooth).
    // Threshold: if variance > 0.15, we likely have banding/acne artifacts.
    // (Typical smooth shadow: variance < 0.10; banding shadow: variance > 0.20)
    EXPECT_LT(bandingVariance, 0.15f)
        << "Back wall shows excessive banding artifacts (shadow acne detected).\n"
        << "  Banding variance=" << bandingVariance
        << " (threshold=0.15)\n"
        << "  This indicates insufficient or incorrect depth bias in shadow sampling.\n"
        << "  Shadow acne typically appears as vertical bands or moire patterns.";
}

// ---------------------------------------------------------------------------
// Test: Shadow acne on back wall with moving UI surface
//
// When the UI surface cube casts a shadow onto the wall, the penumbra region
// (soft shadow edge) can also exhibit acne if the bias is tuned incorrectly.
// This test verifies that even with a moving occluder, the wall shadow is clean.
// ---------------------------------------------------------------------------
TEST_F(ShadowAcneWallTest, BackWall_NoAcneWithShadowCaster_DirectMode)
{
    // Camera looking at the back wall with the UI cube present
    glm::mat4 view = glm::lookAt(
        glm::vec3(0.5f, 1.5f, 1.0f),      // slightly off-center to see shadow edge
        glm::vec3(0.0f, 1.5f, -3.0f),     // looking at back wall center
        glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    // Setup scene with UI cube at t=0 position
    SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
    renderer.updateSceneUBO(sceneUBO);

    // Place UI surface cube
    glm::vec3 P00, P10, P01, P11;
    scene.worldCorners(0.0f, P00, P10, P01, P11);
    renderer.updateSurfaceQuad(P00, P10, P01, P11);
    std::array<std::array<glm::vec3, 4>, 6> cubeCorners;
    scene.worldCubeCorners(0.0f, cubeCorners);
    renderer.updateUIShadowCube(cubeCorners);

    // Dummy surface UBO
    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = glm::mat4(1.0f);
    surfaceUBO.worldMatrix = glm::mat4(1.0f);
    surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
    renderer.updateSurfaceUBO(surfaceUBO);

    auto pixels = renderAndReadback(/*directMode=*/true);

    // Sample a larger region of the back wall to include both lit and shadowed areas
    const int stripLeft   = FB_WIDTH / 6;
    const int stripRight  = 5 * FB_WIDTH / 6;
    const int stripTop    = FB_HEIGHT / 3;
    const int stripBottom = 2 * FB_HEIGHT / 3;
    const int stripWidth  = stripRight - stripLeft;
    const int stripHeight = stripBottom - stripTop;

    // Check for banding artifacts in both lit and shadow regions
    float bandingVariance = detectBandingArtifacts(pixels,
                                                    stripLeft, stripTop,
                                                    stripWidth, stripHeight,
                                                    /*sliceHeight=*/10);

    EXPECT_LT(bandingVariance, 0.18f)
        << "Back wall with shadow caster shows excessive banding artifacts.\n"
        << "  Banding variance=" << bandingVariance
        << " (threshold=0.18)\n"
        << "  This indicates that the depth bias may not properly handle penumbra regions.";
}

// ---------------------------------------------------------------------------
// Test: Vertical surface with self-shadowing (right wall)
//
// The right wall (+X face) is also a vertical surface that can exhibit
// shadow acne when it's perpendicular to the light direction.
// ---------------------------------------------------------------------------
TEST_F(ShadowAcneWallTest, RightWall_NoSelfShadowAcne_DirectMode)
{
    // Camera looking at the right wall (+X direction)
    glm::mat4 view = glm::lookAt(
        glm::vec3(-1.5f, 1.5f, 0.0f),     // camera on left side
        glm::vec3(2.0f, 1.5f, 0.0f),      // looking at right wall
        glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
    renderer.updateSceneUBO(sceneUBO);

    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = glm::mat4(1.0f);
    surfaceUBO.worldMatrix = glm::mat4(1.0f);
    surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
    renderer.updateSurfaceUBO(surfaceUBO);

    auto pixels = renderAndReadback(/*directMode=*/true);

    // Sample the center region of the right wall
    const int stripLeft   = FB_WIDTH / 4;
    const int stripRight  = 3 * FB_WIDTH / 4;
    const int stripTop    = FB_HEIGHT / 4;
    const int stripBottom = 3 * FB_HEIGHT / 4;
    const int stripWidth  = stripRight - stripLeft;
    const int stripHeight = stripBottom - stripTop;

    float bandingVariance = detectBandingArtifacts(pixels,
                                                    stripLeft, stripTop,
                                                    stripWidth, stripHeight,
                                                    /*sliceHeight=*/8);

    EXPECT_LT(bandingVariance, 0.15f)
        << "Right wall shows excessive banding artifacts (shadow acne detected).\n"
        << "  Banding variance=" << bandingVariance
        << " (threshold=0.15)";
}
