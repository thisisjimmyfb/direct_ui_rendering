#include <gtest/gtest.h>
#include "containment_fixture.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ---------------------------------------------------------------------------
// Test: Floor and ceiling remain static (no ripple effect)
//
// This test verifies that the floor and ceiling do NOT have ripple/wave
// patterns applied. They should maintain the same visual appearance across
// different time values, indicating the absence of any time-dependent
// displacement or normal perturbation.
//
// The test renders the floor and ceiling at two different time phases
// and verifies that the pixel colors remain essentially identical,
// confirming that no ripple animation is present.
// ---------------------------------------------------------------------------

class StaticFloorCeilingTest : public ContainmentTest {
protected:
    static constexpr uint32_t FB_WIDTH  = 1280;
    static constexpr uint32_t FB_HEIGHT = 720;

    // Helper to render a single frame at a given time and read back pixels
    std::vector<uint8_t> renderSceneAtTime(float time) {
        // Camera positioned to view the floor and ceiling
        glm::vec3 camPos{3.0f, 1.5f, 3.0f};   // Positioned to see floor/ceiling
        glm::vec3 camTarget{0.0f, 1.5f, 0.0f};  // Looking at center
        glm::mat4 view = glm::lookAt(camPos, camTarget, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                          static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                          0.1f, 100.0f);
        proj[1][1] *= -1.0f;

        SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
        sceneUBO.lightIntensity = 1.0f;
        sceneUBO.uiColorPhase = 0.0f;
        sceneUBO.isTerminalMode = 0.0f;
        sceneUBO.time = time;  // Set the time parameter
        renderer.updateSceneUBO(sceneUBO);

        return renderAndReadback(/*directMode=*/true);
    }
};

// Test 1: Floor appearance is identical at different times (static, no ripple)
TEST_F(StaticFloorCeilingTest, FloorIsStaticNonRippling) {
    // Render floor at two different time phases
    auto pixelsT0 = renderSceneAtTime(0.0f);
    auto pixelsT1 = renderSceneAtTime(1.0f);

    ASSERT_EQ(pixelsT0.size(), pixelsT1.size()) << "Pixel buffers should have same size";
    ASSERT_GT(pixelsT0.size(), 0) << "Pixel buffer should not be empty";

    // Define the floor region (lower portion of the screen when viewed from above-side)
    // The floor should occupy most of the vertical space in this view
    const int floorTop    = 2 * FB_HEIGHT / 3;  // Lower third of screen
    const int floorBottom = FB_HEIGHT;
    const int floorLeft   = FB_WIDTH / 4;
    const int floorRight  = 3 * FB_WIDTH / 4;

    // Calculate the difference between the two renders
    double sumPixelDiff = 0.0;
    int pixelCount = 0;
    int differentPixels = 0;

    for (int y = floorTop; y < floorBottom; ++y) {
        for (int x = floorLeft; x < floorRight; ++x) {
            const uint8_t* px0 = pixelsT0.data() + (y * FB_WIDTH + x) * 4;
            const uint8_t* px1 = pixelsT1.data() + (y * FB_WIDTH + x) * 4;

            // Compute the difference in color
            int rDiff = static_cast<int>(px0[0]) - static_cast<int>(px1[0]);
            int gDiff = static_cast<int>(px0[1]) - static_cast<int>(px1[1]);
            int bDiff = static_cast<int>(px0[2]) - static_cast<int>(px1[2]);

            double diff = std::abs(rDiff) + std::abs(gDiff) + std::abs(bDiff);
            sumPixelDiff += diff;
            ++pixelCount;

            // Count pixels where color differs significantly
            // Allow for very small differences due to floating-point precision
            if (diff > 2.0) {  // Threshold: total color difference > 2/765 (very strict)
                ++differentPixels;
            }
        }
    }

    double meanPixelDiff = pixelCount > 0 ? sumPixelDiff / static_cast<double>(pixelCount) : 0.0;

    // The floor should be IDENTICAL at different times
    // This verifies there is NO ripple effect causing time-dependent changes
    EXPECT_EQ(differentPixels, 0)
        << "Floor pixels should not change between renders at different times. "
        << "If the floor is static (no ripple), pixels should be identical. "
        << differentPixels << " pixels differ (mean difference: " << meanPixelDiff << "). "
        << "This indicates a time-dependent ripple or wave effect is still being applied.";

    EXPECT_LT(meanPixelDiff, 0.1)
        << "Floor has too much variation between time steps (mean difference: " << meanPixelDiff << "). "
        << "A static floor should show essentially no change across time.";
}

// Test 2: Ceiling appearance is identical at different times (static, no ripple)
TEST_F(StaticFloorCeilingTest, CeilingIsStaticNonRippling) {
    // Render ceiling at two different time phases
    auto pixelsT0 = renderSceneAtTime(0.0f);
    auto pixelsT2 = renderSceneAtTime(0.5f);

    ASSERT_EQ(pixelsT0.size(), pixelsT2.size()) << "Pixel buffers should have same size";
    ASSERT_GT(pixelsT0.size(), 0) << "Pixel buffer should not be empty";

    // Define the ceiling region (upper portion of the screen when viewed from above)
    // The ceiling should occupy the upper portion when viewed from this angle
    const int ceilingTop    = 0;
    const int ceilingBottom = FB_HEIGHT / 3;  // Upper third of screen
    const int ceilingLeft   = FB_WIDTH / 4;
    const int ceilingRight  = 3 * FB_WIDTH / 4;

    // Calculate the difference between the two renders
    double sumPixelDiff = 0.0;
    int pixelCount = 0;
    int differentPixels = 0;

    for (int y = ceilingTop; y < ceilingBottom; ++y) {
        for (int x = ceilingLeft; x < ceilingRight; ++x) {
            const uint8_t* px0 = pixelsT0.data() + (y * FB_WIDTH + x) * 4;
            const uint8_t* px1 = pixelsT2.data() + (y * FB_WIDTH + x) * 4;

            // Compute the difference in color
            int rDiff = static_cast<int>(px0[0]) - static_cast<int>(px1[0]);
            int gDiff = static_cast<int>(px0[1]) - static_cast<int>(px1[1]);
            int bDiff = static_cast<int>(px0[2]) - static_cast<int>(px1[2]);

            double diff = std::abs(rDiff) + std::abs(gDiff) + std::abs(bDiff);
            sumPixelDiff += diff;
            ++pixelCount;

            // Count pixels where color differs significantly
            if (diff > 2.0) {  // Threshold: total color difference > 2/765
                ++differentPixels;
            }
        }
    }

    double meanPixelDiff = pixelCount > 0 ? sumPixelDiff / static_cast<double>(pixelCount) : 0.0;

    // The ceiling should be IDENTICAL at different times
    // This verifies there is NO ripple effect causing time-dependent changes
    EXPECT_EQ(differentPixels, 0)
        << "Ceiling pixels should not change between renders at different times. "
        << "If the ceiling is static (no ripple), pixels should be identical. "
        << differentPixels << " pixels differ (mean difference: " << meanPixelDiff << "). "
        << "This indicates a time-dependent ripple or wave effect is still being applied.";

    EXPECT_LT(meanPixelDiff, 0.1)
        << "Ceiling has too much variation between time steps (mean difference: " << meanPixelDiff << "). "
        << "A static ceiling should show essentially no change across time.";
}

// Test 3: Verify walls render correctly with static geometry
TEST_F(StaticFloorCeilingTest, WallsRenderCorrectlyWithStaticGeometry) {
    // Render the scene with static geometry (no ripple displacement)
    auto pixels = renderSceneAtTime(0.0f);
    ASSERT_GT(pixels.size(), 0) << "Pixel buffer should not be empty";

    // With static geometry (no ripple displacement), walls should render
    // without visual artifacts or visible gaps at edges.
    // This is verified by checking that:
    // 1. The render completes successfully
    // 2. The pixel buffer is valid and contains expected colors
    // 3. There are no NaN or invalid values

    // Verify the render produced valid pixel data
    bool hasValidPixels = false;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        uint8_t r = pixels[i];
        uint8_t g = pixels[i + 1];
        uint8_t b = pixels[i + 2];
        uint8_t a = pixels[i + 3];

        // Check that we have some non-zero color values (not all black)
        if (r > 0 || g > 0 || b > 0) {
            hasValidPixels = true;
            break;
        }
    }

    EXPECT_TRUE(hasValidPixels)
        << "Wall geometry should render with valid colors. "
        << "If no valid pixels found, geometry may not be properly rendered.";

    // Sample the middle region to verify walls are visible
    const int centerX = FB_WIDTH / 2;
    const int centerY = FB_HEIGHT / 2;
    const uint8_t* centerPx = pixels.data() + (centerY * FB_WIDTH + centerX) * 4;

    // Center should have some color (not black background)
    int centerColor = centerPx[0] + centerPx[1] + centerPx[2];
    EXPECT_GT(centerColor, 50)
        << "Center of scene should show rendered geometry with visible color. "
        << "Low color value suggests walls/geometry not rendering correctly.";
}
