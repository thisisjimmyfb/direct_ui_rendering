#include <gtest/gtest.h>
#include "containment_fixture.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Test: Wall normal mapping and parallax effects
//
// This test verifies that room walls have subtle normal mapping and
// potential parallax mapping effects that enhance visual detail:
//
// 1. Wall lighting varies due to normal map perturbation (not perfectly flat)
// 2. Different wall materials with different roughness show distinct shading
// 3. Parallax mapping creates view-dependent surface detail on walls
// 4. Normal mapping effects are consistent across different viewing angles
//
// The test renders walls from different angles and verifies that:
// - Normal variation is visible (non-uniform shading)
// - Material properties affect the appearance
// - Different roughness values produce measurably different results
// ---------------------------------------------------------------------------

class WallNormalMappingTest : public ContainmentTest {
protected:
    // Helper to render a specific wall from a given camera position
    std::vector<uint8_t> renderWallFromCamera(
        const glm::vec3& camPos,
        const glm::vec3& camTarget)
    {
        glm::mat4 view = glm::lookAt(camPos, camTarget, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                          static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                          0.1f, 100.0f);
        proj[1][1] *= -1.0f;

        SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
        sceneUBO.lightIntensity = 1.0f;
        sceneUBO.uiColorPhase = 0.0f;
        sceneUBO.isTerminalMode = 0.0f;
        sceneUBO.time = 0.0f;
        renderer.updateSceneUBO(sceneUBO);

        return renderAndReadback(/*directMode=*/true);
    }

    // Sample pixels from a rectangular region of the framebuffer
    std::vector<float> sampleLuminanceRegion(
        const std::vector<uint8_t>& pixels,
        int x_start, int y_start, int width, int height)
    {
        std::vector<float> luminances;
        for (int y = y_start; y < y_start + height; ++y) {
            for (int x = x_start; x < x_start + width; ++x) {
                if (x < 0 || x >= static_cast<int>(FB_WIDTH) ||
                    y < 0 || y >= static_cast<int>(FB_HEIGHT)) {
                    continue;
                }
                const uint8_t* px = pixels.data() + (y * FB_WIDTH + x) * 4;
                float lum = 0.299f * px[0]/255.0f + 0.587f * px[1]/255.0f + 0.114f * px[2]/255.0f;
                luminances.push_back(lum);
            }
        }
        return luminances;
    }

    // Calculate spatial variation in a region (standard deviation of luminance)
    float calculateSpatialVariation(const std::vector<float>& luminances) {
        if (luminances.empty()) return 0.0f;

        float mean = 0.0f;
        for (float lum : luminances) {
            mean += lum;
        }
        mean /= luminances.size();

        float variance = 0.0f;
        for (float lum : luminances) {
            variance += (lum - mean) * (lum - mean);
        }
        variance /= luminances.size();

        return std::sqrt(variance);
    }
};

// Test 1: Wall renders without errors
TEST_F(WallNormalMappingTest, WallRenderSucceeds) {
    // Camera looking at the back wall
    glm::vec3 camPos{0.0f, 1.5f, 2.5f};
    glm::vec3 camTarget{0.0f, 1.5f, -2.0f};

    auto pixels = renderWallFromCamera(camPos, camTarget);
    EXPECT_GT(pixels.size(), 0) << "Render should produce output pixels";
}

// Test 2: Wall surfaces show normal variation (not perfectly flat lighting)
TEST_F(WallNormalMappingTest, WallNormalMapping_CreatesVariation) {
    // Camera looking directly at the back wall
    glm::vec3 camPos{0.0f, 1.5f, 2.5f};
    glm::vec3 camTarget{0.0f, 1.5f, -2.0f};

    auto pixels = renderWallFromCamera(camPos, camTarget);
    ASSERT_GT(pixels.size(), 0) << "Pixel buffer should not be empty";

    // Sample a large region of the wall surface
    // The wall should occupy the center of the screen
    std::vector<float> wallLuminances = sampleLuminanceRegion(
        pixels,
        FB_WIDTH / 4,      // x_start
        FB_HEIGHT / 4,     // y_start
        FB_WIDTH / 2,      // width
        FB_HEIGHT / 2      // height
    );

    ASSERT_GT(wallLuminances.size(), 10) << "Should sample many pixels from wall";

    // Calculate spatial variation in wall lighting
    float variation = calculateSpatialVariation(wallLuminances);

    // Normal mapping should create measurable spatial variation in lighting
    // A standard deviation > 0.02 indicates non-uniform shading from normal maps
    EXPECT_GT(variation, 0.02f)
        << "Wall normal mapping creates insufficient variation: "
        << "spatial variation = " << variation << " (expected > 0.02). "
        << "Normal mapping should create visible surface detail and non-uniform shading.";
}

// Test 3: Different wall materials show different appearances
TEST_F(WallNormalMappingTest, DifferentWallMaterials_ProduceDifferentShading) {
    // Render back wall (smooth dielectric, roughness 0.6)
    glm::vec3 backWallCam{0.0f, 1.5f, 2.5f};
    glm::vec3 backWallTarget{0.0f, 1.5f, -2.0f};
    auto backWallPixels = renderWallFromCamera(backWallCam, backWallTarget);

    // Render front wall (textured dielectric, roughness 0.85)
    glm::vec3 frontWallCam{0.0f, 1.5f, -2.5f};
    glm::vec3 frontWallTarget{0.0f, 1.5f, 2.0f};
    auto frontWallPixels = renderWallFromCamera(frontWallCam, frontWallTarget);

    ASSERT_EQ(backWallPixels.size(), frontWallPixels.size());

    // Sample the same screen region from both renders
    auto backWallLum = sampleLuminanceRegion(backWallPixels,
                                              FB_WIDTH / 4, FB_HEIGHT / 4,
                                              FB_WIDTH / 2, FB_HEIGHT / 2);
    auto frontWallLum = sampleLuminanceRegion(frontWallPixels,
                                               FB_WIDTH / 4, FB_HEIGHT / 4,
                                               FB_WIDTH / 2, FB_HEIGHT / 2);

    ASSERT_GT(backWallLum.size(), 10);
    ASSERT_GT(frontWallLum.size(), 10);

    float backWallVar = calculateSpatialVariation(backWallLum);
    float frontWallVar = calculateSpatialVariation(frontWallLum);

    // Different roughness materials should show different normal mapping characteristics
    // Front wall is rougher (0.85) so should show more pronounced normal variation
    // Back wall is smoother (0.6) so should show less variation
    EXPECT_NE(backWallVar, frontWallVar)
        << "Different wall materials should produce different shading patterns. "
        << "Back wall variation: " << backWallVar
        << ", Front wall variation: " << frontWallVar;
}

// Test 4: Normal mapping is view-independent (not based on camera angle alone)
TEST_F(WallNormalMappingTest, WallNormalMapping_ConsistentAcrossViewAngles) {
    // View the back wall from two different camera angles
    // Both should show normal mapping variation in the wall surface

    // Camera angle 1: Direct view
    glm::vec3 cam1{0.0f, 1.5f, 2.5f};
    glm::vec3 target1{0.0f, 1.5f, -2.0f};
    auto pixels1 = renderWallFromCamera(cam1, target1);

    // Camera angle 2: Angled view
    glm::vec3 cam2{1.5f, 1.5f, 2.5f};
    glm::vec3 target2{0.0f, 1.5f, -2.0f};
    auto pixels2 = renderWallFromCamera(cam2, target2);

    auto lum1 = sampleLuminanceRegion(pixels1, FB_WIDTH / 4, FB_HEIGHT / 4,
                                       FB_WIDTH / 2, FB_HEIGHT / 2);
    auto lum2 = sampleLuminanceRegion(pixels2, FB_WIDTH / 4, FB_HEIGHT / 4,
                                       FB_WIDTH / 2, FB_HEIGHT / 2);

    float var1 = calculateSpatialVariation(lum1);
    float var2 = calculateSpatialVariation(lum2);

    // Both views should show meaningful normal mapping variation
    // (not necessarily the same, but both should be > threshold)
    EXPECT_GT(var1, 0.01f)
        << "Wall normal mapping not visible at camera angle 1";
    EXPECT_GT(var2, 0.01f)
        << "Wall normal mapping not visible at camera angle 2";
}

// Test 5: Metallic walls show roughness-dependent normal mapping
TEST_F(WallNormalMappingTest, MetallicWall_ShowsRoughnessVariation) {
    // Left wall is brushed steel (metallic=0.8, roughness=0.5)
    glm::vec3 leftWallCam{2.5f, 1.5f, 0.0f};
    glm::vec3 leftWallTarget{-2.0f, 1.5f, 0.0f};

    auto pixels = renderWallFromCamera(leftWallCam, leftWallTarget);

    auto luminances = sampleLuminanceRegion(pixels,
                                             FB_WIDTH / 4, FB_HEIGHT / 4,
                                             FB_WIDTH / 2, FB_HEIGHT / 2);

    float variation = calculateSpatialVariation(luminances);

    // Even metallic surfaces should show normal mapping variation
    // due to roughness and material properties
    EXPECT_GT(variation, 0.01f)
        << "Metallic wall should show visible normal mapping effects. "
        << "Spatial variation = " << variation << " (expected > 0.01)";
}

// Test 6: Wall normal mapping creates peak-to-peak luminance variation
TEST_F(WallNormalMappingTest, WallNormalMapping_HasMeasurablePeakVariation) {
    glm::vec3 camPos{0.0f, 1.5f, 2.5f};
    glm::vec3 camTarget{0.0f, 1.5f, -2.0f};

    auto pixels = renderWallFromCamera(camPos, camTarget);

    auto luminances = sampleLuminanceRegion(pixels,
                                             FB_WIDTH / 4, FB_HEIGHT / 4,
                                             FB_WIDTH / 2, FB_HEIGHT / 2);

    ASSERT_GT(luminances.size(), 10);

    float minLum = *std::min_element(luminances.begin(), luminances.end());
    float maxLum = *std::max_element(luminances.begin(), luminances.end());
    float peakVariation = maxLum - minLum;

    // Normal mapping should create at least 5% peak-to-peak luminance variation
    EXPECT_GT(peakVariation, 0.05f)
        << "Wall normal mapping peak-to-peak variation insufficient: "
        << peakVariation << " (expected > 0.05). "
        << "Normal maps should create visible surface detail variation.";
}

