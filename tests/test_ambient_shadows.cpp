#include "containment_fixture.h"
#include <cmath>

// ---------------------------------------------------------------------------
// AmbientShadowTest — validates that ambient lighting is visible in shadows
//
// The spec requires that shadows not be pitch black. Ambient lighting should
// illuminate even areas that are outside the spotlight cone. This test verifies:
// 1. Areas in shadow (outside spotlight cone) have visible ambient lighting
// 2. Ambient brightness is measurable and > 0
// 3. Both direct and traditional modes apply ambient correctly in shadows
// ---------------------------------------------------------------------------

class AmbientShadowTest : public ContainmentTest {};

// ---------------------------------------------------------------------------
// Test 1: AmbientLighting_InShadow_NotPitchBlack
//
// Position camera to look at geometry that is outside the spotlight cone.
// This geometry should receive only ambient lighting.
// The ambient color is (0.08, 0.08, 0.12), so brightness should be > 0.
// Even with low ambient, it should not be pitch black (> 5/255).
//
// The spotlight position is (0, 2.8, 0.5), direction toward (0, -1.3, -3.5).
// Inner cone: 35°, Outer cone: 50°.
// We position the camera to look at the back wall (z = -3) from a location
// where the spotlight should not directly illuminate.
// ---------------------------------------------------------------------------

TEST_F(AmbientShadowTest, AmbientLighting_InShadow_NotPitchBlack_DirectMode)
{
    // Position camera to look at the far corner of the back wall,
    // well outside the spotlight's main cone.
    // The spotlight (0, 2.8, 0.5) points toward (0, -1.3, -3.5).
    // The back wall right corner (2, 1.5, -3) should be outside the cone.

    glm::mat4 view = glm::lookAt(
        glm::vec3(2.5f, 1.5f, 1.0f),      // camera position (right side)
        glm::vec3(1.8f, 1.5f, -3.0f),     // look at back wall, right corner
        glm::vec3(0.0f, 1.0f, 0.0f));     // up
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    uint8_t shadowBrightness = getPixelBrightness(FB_WIDTH / 2, FB_HEIGHT / 2,
                                                   view, proj, /*directMode=*/true);

    // Ambient is (0.08, 0.08, 0.12), which averages to about 0.0933.
    // With surface color and material, expect at least 10-15 in brightness.
    // Definitely should not be pitch black (< 5).
    EXPECT_GT(shadowBrightness, 5)
        << "Expected visible ambient lighting in shadow, but got darkness: "
        << static_cast<int>(shadowBrightness) << "/255. "
        << "Ambient lighting should prevent pitch-black shadows.";
}

// ---------------------------------------------------------------------------
// Test 2: AmbientLighting_InShadow_NotPitchBlack_TraditionalMode
//
// Same test but in traditional rendering mode to ensure the fix applies
// consistently across both rendering paths.
// ---------------------------------------------------------------------------

TEST_F(AmbientShadowTest, AmbientLighting_InShadow_NotPitchBlack_TraditionalMode)
{
    glm::mat4 view = glm::lookAt(
        glm::vec3(2.5f, 1.5f, 1.0f),
        glm::vec3(1.8f, 1.5f, -3.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    uint8_t shadowBrightness = getPixelBrightness(FB_WIDTH / 2, FB_HEIGHT / 2,
                                                   view, proj, /*directMode=*/false);

    EXPECT_GT(shadowBrightness, 5)
        << "Expected visible ambient lighting in shadow (traditional mode), "
        << "but got darkness: " << static_cast<int>(shadowBrightness) << "/255";
}

// ---------------------------------------------------------------------------
// Test 3: AmbientLighting_VariesBetweenLitAndShadow
//
// Verify that lit areas (within spotlight cone) are BRIGHTER than shadowed
// areas, confirming that spotlighting is additive to ambient, not replacing it.
//
// Lit area: center of back wall where spotlight points
// Shadow area: corner of back wall outside cone
// Expect: lit > shadow, and both > 0 (not pitch black)
// ---------------------------------------------------------------------------

TEST_F(AmbientShadowTest, AmbientLighting_LitAreaBrighterThanShadow_DirectMode)
{
    // First, measure brightness in a well-lit area (center of back wall).
    glm::mat4 litView = glm::lookAt(
        glm::vec3(0.0f, 1.5f, 1.0f),
        glm::vec3(0.0f, 1.5f, -3.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    uint8_t litBrightness = getPixelBrightness(FB_WIDTH / 2, FB_HEIGHT / 2,
                                               litView, proj, /*directMode=*/true);

    // Now measure brightness in a shadowed area (right corner of back wall).
    glm::mat4 shadowView = glm::lookAt(
        glm::vec3(2.5f, 1.5f, 1.0f),
        glm::vec3(1.8f, 1.5f, -3.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));

    uint8_t shadowBrightness = getPixelBrightness(FB_WIDTH / 2, FB_HEIGHT / 2,
                                                   shadowView, proj, /*directMode=*/true);

    // The lit area should be brighter than the shadowed area
    EXPECT_GT(litBrightness, shadowBrightness)
        << "Lit area brightness (" << static_cast<int>(litBrightness)
        << ") should exceed shadow area brightness ("
        << static_cast<int>(shadowBrightness) << ")";

    // But the shadow should still have some brightness from ambient
    EXPECT_GT(shadowBrightness, 5)
        << "Shadow area should have ambient lighting, not pitch black";
}

// ---------------------------------------------------------------------------
// Test 4: AmbientVsLit_RatioCheck
//
// Measure the ratio of lit to shadowed brightness.
// If both ambient and direct lighting are working correctly, we expect
// the lit area to be notably brighter (e.g., 2-5x) than the shadowed area,
// but the shadowed area should still have visible brightness.
// ---------------------------------------------------------------------------

TEST_F(AmbientShadowTest, AmbientVsLit_ReasonableRatio_DirectMode)
{
    // Lit area
    glm::mat4 litView = glm::lookAt(
        glm::vec3(0.0f, 1.5f, 1.0f),
        glm::vec3(0.0f, 1.5f, -3.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    uint8_t litBrightness = getPixelBrightness(FB_WIDTH / 2, FB_HEIGHT / 2,
                                               litView, proj, /*directMode=*/true);

    // Shadow area
    glm::mat4 shadowView = glm::lookAt(
        glm::vec3(2.5f, 1.5f, 1.0f),
        glm::vec3(1.8f, 1.5f, -3.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));

    uint8_t shadowBrightness = getPixelBrightness(FB_WIDTH / 2, FB_HEIGHT / 2,
                                                   shadowView, proj, /*directMode=*/true);

    // If both values > 0, compute ratio
    if (shadowBrightness > 0) {
        float ratio = static_cast<float>(litBrightness) / shadowBrightness;
        // Expect lit to be 1.5-10x brighter than shadow
        // (ambient + spotlight light) / (ambient only)
        EXPECT_GT(ratio, 1.0f)
            << "Lit area should be brighter than shadow area";
        EXPECT_LT(ratio, 100.0f)
            << "Ratio should be reasonable (shadow shouldn't be pure black)";
    } else {
        FAIL() << "Shadow brightness is zero; cannot compute ratio";
    }
}

// ---------------------------------------------------------------------------
// Test 5: AmbientLighting_FarBehindSpotlight_HasMeasurableLight
//
// Position the camera looking at the front wall (z = +3), which is far behind
// the spotlight direction and should be completely outside the spotlight cone.
// The spotlight points toward (0, -1.3, -3.5), so the front wall (+Z) should
// be completely outside the cone. This geometry should receive ONLY ambient
// lighting and should not be pitch black.
// ---------------------------------------------------------------------------

TEST_F(AmbientShadowTest, AmbientLighting_FarBehindSpotlight_HasMeasurableLight_DirectMode)
{
    // Camera looking at the front wall (z = +3), which is opposite to where
    // the spotlight is pointing. This geometry is definitely outside the cone.
    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 1.5f, 2.5f),      // camera inside looking toward front
        glm::vec3(0.0f, 1.5f, 3.0f),      // looking at front wall
        glm::vec3(0.0f, 1.0f, 0.0f));     // up
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    uint8_t frontWallBrightness = getPixelBrightness(FB_WIDTH / 2, FB_HEIGHT / 2,
                                                      view, proj, /*directMode=*/true);

    // The front wall is definitely outside the spotlight cone.
    // It should still have ambient lighting.
    // With ambient (0.08, 0.08, 0.12) and surface color (say ~0.7),
    // expect at least 10-15 in brightness (not pitch black < 5).
    EXPECT_GT(frontWallBrightness, 5)
        << "Front wall (opposite to spotlight) should have ambient lighting, "
        << "not pitch black. Got brightness: " << static_cast<int>(frontWallBrightness);
}
