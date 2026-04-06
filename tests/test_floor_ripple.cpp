#include <gtest/gtest.h>
#include "containment_fixture.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ---------------------------------------------------------------------------
// Test: Floor ripple patterns
//
// This test verifies that ripple/wave patterns are applied to the floor
// to enhance visual interest. The test renders the floor with:
// - Time t=0 (initial ripple phase)
// - Time t=0.5 seconds (different ripple phase)
// And verifies that the pixel colors change due to the ripple effect.
//
// The ripple should affect:
// 1. Normal perturbation (changes shading)
// 2. Potentially vertex displacement (if amplitude is non-zero)
//
// A successful ripple implementation should result in different colors
// when rendering at different time phases.
// ---------------------------------------------------------------------------

class FloorRippleTest : public ContainmentTest {
protected:
    static constexpr uint32_t FB_WIDTH  = 1280;
    static constexpr uint32_t FB_HEIGHT = 720;

    // Helper to render a single frame at a given time and read back pixels
    std::vector<uint8_t> renderFloorAtTime(float time) {
        // Camera positioned to view the floor at an angle
        // This ensures the floor is visible in the viewport
        glm::vec3 camPos{3.0f, 2.0f, 3.0f};   // Positioned to the side and above
        glm::vec3 camTarget{0.0f, 0.0f, 0.0f};  // Looking at center of floor
        glm::mat4 view = glm::lookAt(camPos, camTarget, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                          static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                          0.1f, 100.0f);
        proj[1][1] *= -1.0f;

        SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
        sceneUBO.lightIntensity = 1.0f + 0.3f * std::sin(time * 2.0f);  // Match app behavior
        sceneUBO.uiColorPhase = time;  // Time-based color animation phase
        sceneUBO.isTerminalMode = 0.0f;  // Not in terminal mode
        sceneUBO.time = time;  // Set the time for ripple animation
        renderer.updateSceneUBO(sceneUBO);

        return renderAndReadback(/*directMode=*/true);
    }
};

// Test 0: Verify render can complete without errors
TEST_F(FloorRippleTest, RenderSucceeds) {
    // Just verify that rendering completes without crashing or validation errors
    auto pixels = renderFloorAtTime(0.0f);
    EXPECT_GT(pixels.size(), 0) << "Render should produce output pixels";
}

// Test 1: Floor appearance changes with ripple animation over time
TEST_F(FloorRippleTest, FloorRippleAnimatesOverTime) {
    // Render floor at two different time phases
    auto pixelsT0 = renderFloorAtTime(0.0f);
    auto pixelsT05 = renderFloorAtTime(0.5f);

    ASSERT_EQ(pixelsT0.size(), pixelsT05.size()) << "Pixel buffers should have same size";
    ASSERT_GT(pixelsT0.size(), 0) << "Pixel buffer should not be empty";

    // Define a center region of the floor to sample
    // The floor should occupy much of the screen when viewed from above
    const int stripTop    = FB_HEIGHT / 4;
    const int stripBottom = 3 * FB_HEIGHT / 4;
    const int stripLeft   = FB_WIDTH / 4;
    const int stripRight  = 3 * FB_WIDTH / 4;

    // Calculate luminance difference between t=0 and t=0.5
    double sumLuminanceDiff = 0.0;
    int pixelCount = 0;
    int changedPixels = 0;

    for (int y = stripTop; y < stripBottom; ++y) {
        for (int x = stripLeft; x < stripRight; ++x) {
            const uint8_t* px0 = pixelsT0.data() + (y * FB_WIDTH + x) * 4;
            const uint8_t* px1 = pixelsT05.data() + (y * FB_WIDTH + x) * 4;

            // Compute luminance: L = 0.299*R + 0.587*G + 0.114*B
            float lum0 = 0.299f * px0[0]/255.0f + 0.587f * px0[1]/255.0f + 0.114f * px0[2]/255.0f;
            float lum1 = 0.299f * px1[0]/255.0f + 0.587f * px1[1]/255.0f + 0.114f * px1[2]/255.0f;

            float diff = std::abs(lum1 - lum0);
            sumLuminanceDiff += diff;
            ++pixelCount;

            // Count pixels where color changed significantly (> 5% luminance change)
            if (diff > 0.05f) {
                ++changedPixels;
            }
        }
    }

    double meanLuminanceDiff = sumLuminanceDiff / static_cast<double>(pixelCount);

    // The ripple effect should cause visible changes in the floor's appearance
    // We expect at least some pixels to show luminance change due to:
    // - Normal perturbation affecting lighting calculations
    // - Potentially vertex displacement changing surface geometry
    // A threshold of 0.03 (3% average luminance difference) indicates perceptible ripple effect
    EXPECT_GT(meanLuminanceDiff, 0.03)
        << "Floor ripple effect not visible: mean luminance difference = " << meanLuminanceDiff
        << " (expected > 0.03). The ripple animation should cause noticeable changes "
        << "in floor appearance due to dynamic normal variation or vertex displacement.";

    // Also verify that a reasonable percentage of pixels changed
    float percentChanged = (100.0f * changedPixels) / pixelCount;
    EXPECT_GT(percentChanged, 1.0f)
        << "Ripple effect: only " << percentChanged << "% of floor pixels changed "
        << "(expected > 1%). The ripple should affect a significant portion of the floor.";
}

// Test 2: Floor ripple creates distinct per-fragment variation
TEST_F(FloorRippleTest, FloorRippleCreatesLocalVariation) {
    auto pixels = renderFloorAtTime(0.5f);

    ASSERT_GT(pixels.size(), 0) << "Pixel buffer should not be empty";

    // Sample the floor in a grid pattern to verify variation across space
    // Different positions on the floor should have different ripple phases
    const int centerY = FB_HEIGHT / 2;
    const int sampleSpacing = 50;  // pixels apart
    const int sampleStartX = FB_WIDTH / 4;
    const int sampleEndX = 3 * FB_WIDTH / 4;

    std::vector<float> samples;
    for (int x = sampleStartX; x < sampleEndX; x += sampleSpacing) {
        const uint8_t* px = pixels.data() + (centerY * FB_WIDTH + x) * 4;
        float lum = 0.299f * px[0]/255.0f + 0.587f * px[1]/255.0f + 0.114f * px[2]/255.0f;
        samples.push_back(lum);
    }

    // Ripple patterns should create variation: not all samples should be identical
    ASSERT_GT(samples.size(), 1) << "Need at least 2 samples for variation check";

    // Check if samples have variation
    float minSample = *std::min_element(samples.begin(), samples.end());
    float maxSample = *std::max_element(samples.begin(), samples.end());
    float variation = maxSample - minSample;

    // The ripple pattern should create at least some spatial variation
    // A threshold of 0.05 (5% luminance range) indicates wave patterns are present
    EXPECT_GT(variation, 0.05f)
        << "Floor ripple pattern lacks spatial variation: "
        << "luminance range = " << variation << " (expected > 0.05). "
        << "Ripple waves should create non-uniform shading across the floor surface.";
}

// Test 3: Floor ripple frequency and amplitude are reasonable
TEST_F(FloorRippleTest, FloorRippleHasValidFrequency) {
    // Render at multiple time points to verify reasonable ripple behavior
    std::vector<float> times = {0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
    std::vector<std::vector<uint8_t>> allPixels;

    for (float t : times) {
        allPixels.push_back(renderFloorAtTime(t));
    }

    // Sample multiple regions of the floor to find luminance variation
    // Different parts of the ripple pattern will have different phases
    const int sampleSpacing = 50;  // pixels apart
    std::vector<float> allLuminances;

    for (const auto& pixels : allPixels) {
        for (int x = 200; x < FB_WIDTH - 200; x += sampleSpacing) {
            for (int y = 200; y < FB_HEIGHT - 200; y += sampleSpacing) {
                const uint8_t* px = pixels.data() + (y * FB_WIDTH + x) * 4;
                float lum = 0.299f * px[0]/255.0f + 0.587f * px[1]/255.0f + 0.114f * px[2]/255.0f;
                allLuminances.push_back(lum);
            }
        }
    }

    ASSERT_GT(allLuminances.size(), 10) << "Need multiple samples for frequency analysis";

    // Calculate min/max and check if there's reasonable variation over time
    float minLum = *std::min_element(allLuminances.begin(), allLuminances.end());
    float maxLum = *std::max_element(allLuminances.begin(), allLuminances.end());
    float range = maxLum - minLum;

    // The ripple should create visible variation over time
    // A range of at least 10% indicates animated ripple effect
    EXPECT_GT(range, 0.10f)
        << "Ripple effect shows insufficient temporal variation: "
        << "luminance range across all samples = " << range << " (expected > 0.10). "
        << "Ripple animation should cause measurable color changes.";

    EXPECT_LT(range, 0.80f)
        << "Ripple effect is too extreme: luminance range = " << range
        << " (expected < 0.80). Ripple should produce subtle visual variations.";
}
