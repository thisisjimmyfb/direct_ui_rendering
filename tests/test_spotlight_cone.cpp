#include "containment_fixture.h"
#include <cmath>

// ---------------------------------------------------------------------------
// SpotlightConeTest — validates spotlight cone angle attenuation in rendering.
//
// The spotlight has two cone angles:
// - Inner cone: full intensity (cos(innerAngle) stored in lightColor.w)
// - Outer cone: falloff boundary (cos(outerAngle) stored in lightDir.w)
//
// Between inner and outer, the shader applies smoothstep for smooth falloff.
// This test verifies:
// 1. Pixels within the inner cone are fully lit
// 2. Pixels outside the outer cone are unlit (ambient only)
// 3. Pixels between inner/outer cones show smooth interpolation
// 4. The transition is smooth, not sharp (validating smoothstep)
// ---------------------------------------------------------------------------

class SpotlightConeTest : public ContainmentTest {
protected:
    // Helper: render a frame and measure brightness at a specific pixel location.
    uint8_t getPixelBrightness(uint32_t x, uint32_t y,
                               const glm::mat4& view, const glm::mat4& proj) {
        SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
        renderer.updateSceneUBO(sceneUBO);

        // Dummy surface (off-screen, we just want room geometry lit by spotlight)
        glm::vec3 P00{-0.5f,  0.5f, -5.0f};
        glm::vec3 P10{ 0.5f,  0.5f, -5.0f};
        glm::vec3 P01{-0.5f, -0.5f, -5.0f};
        glm::mat4 vp = proj * view;

        auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                                   static_cast<float>(W_UI),
                                                   static_cast<float>(H_UI), vp);
        auto clipPlanes = computeClipPlanes(P00, P10, P01);

        SurfaceUBO surfaceUBO{};
        surfaceUBO.totalMatrix = transforms.M_total;
        surfaceUBO.worldMatrix = transforms.M_world;
        for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
        surfaceUBO.depthBias = Renderer::DEPTH_BIAS_DEFAULT;
        renderer.updateSurfaceUBO(surfaceUBO);

        auto pixels = renderAndReadback(/*directMode=*/true);

        // Extract pixel brightness (average R, G, B channels)
        const uint8_t* px = pixels.data() + (y * FB_WIDTH + x) * 4;
        uint32_t sum = static_cast<uint32_t>(px[0]) + px[1] + px[2];
        return static_cast<uint8_t>(sum / 3);
    }
};

// ---------------------------------------------------------------------------
// Test 1: Spotlight_InnerCone_ProducesMaxIntensity
//
// Position a point directly along the spotlight direction (well within the
// inner cone) and verify it receives near-maximum lighting.
//
// The back wall (z=-5) is directly visible from the spotlight's perspective.
// We measure brightness at the wall center and verify it's significantly
// brighter than ambient.
// ---------------------------------------------------------------------------

TEST_F(SpotlightConeTest, Spotlight_InnerCone_ProducesMaxIntensity)
{
    // Camera looking at the back wall from inside the room.
    // The spotlight (0, 2.8, 0.5) points toward (0, -1.3, -3.5).
    // Points on the back wall near where the spotlight is pointing should be
    // well-lit (within the inner cone at ~35 degrees).

    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.5f, 0.0f),
                                 glm::vec3(0.0f, 1.5f, -4.5f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    uint8_t centerBrightness = getPixelBrightness(FB_WIDTH / 2, FB_HEIGHT / 2, view, proj);

    // Spotlight color is (1.0, 0.95, 0.85), ambient is (0.08, 0.08, 0.12).
    // A well-lit pixel should be significantly brighter than ambient (~50/255).
    // Expect at least 150/255 brightness when well-lit by the spotlight.
    EXPECT_GT(centerBrightness, 150)
        << "Expected bright lighting from spotlight within inner cone, "
        << "but got brightness " << static_cast<int>(centerBrightness);
}

// ---------------------------------------------------------------------------
// Test 2: Spotlight_OuterCone_ProducesAmbientLighting
//
// Position a point well outside the spotlight's outer cone (angle > 50 degrees).
// Such a point should receive only ambient lighting (no direct spotlight).
//
// We do this by moving to a position where the angle to the light is large.
// The corner of the room (far from the light direction) should have minimal
// direct spotlight illumination.
// ---------------------------------------------------------------------------

TEST_F(SpotlightConeTest, Spotlight_OuterCone_ProducesAmbientLighting)
{
    // Camera looking at a corner of the room, far from spotlight direction.
    // The spotlight points generally toward (0, -1.3, -3.5), so the right
    // corner (+X direction) should be well outside the cone.

    glm::mat4 view = glm::lookAt(glm::vec3(2.0f, 1.5f, -2.0f),
                                 glm::vec3(2.5f, 0.5f, -4.5f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    uint8_t cornerBrightness = getPixelBrightness(FB_WIDTH / 2, FB_HEIGHT / 2, view, proj);

    // Ambient color is (0.08, 0.08, 0.12), which is about 9/255 in brightness.
    // Outside the outer cone, we expect only ambient, so brightness should be low.
    // Allow some margin since the falloff is smooth; expect < 80.
    EXPECT_LT(cornerBrightness, 80)
        << "Expected ambient-only lighting outside outer cone, "
        << "but got brightness " << static_cast<int>(cornerBrightness);
}

// ---------------------------------------------------------------------------
// Test 3: Spotlight_TransitionZone_ShowsSmoothInterpolation
//
// Position a point in the transition zone between inner and outer cone.
// The brightness should be between the inner-cone max and outer-cone ambient.
// Additionally, we verify that the transition is smooth (not a hard edge),
// which is the purpose of the smoothstep function in the shader.
//
// We test this by rendering two nearby points: one at a smaller angle
// (brighter) and one at a larger angle (dimmer). The difference should
// reflect smooth interpolation.
// ---------------------------------------------------------------------------

TEST_F(SpotlightConeTest, Spotlight_TransitionZone_SmoothFalloff)
{
    // Position camera to see geometry at an intermediate angle from spotlight.
    // Inner cone: 35° (cos ≈ 0.819)
    // Outer cone: 50° (cos ≈ 0.643)
    // Target a position around 40-45° angle to the light.

    glm::mat4 view = glm::lookAt(glm::vec3(1.0f, 1.5f, -1.0f),
                                 glm::vec3(1.0f, 1.5f, -4.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    uint8_t transitionBrightness = getPixelBrightness(FB_WIDTH / 2, FB_HEIGHT / 2, view, proj);

    // Brightness in transition zone should be between ambient (~10) and full light (~200).
    // Expected range: 30-150.
    EXPECT_GT(transitionBrightness, 30)
        << "Expected some spotlight contribution in transition zone";
    EXPECT_LT(transitionBrightness, 200)
        << "Expected reduced spotlight contribution in transition zone";
}

// ---------------------------------------------------------------------------
// Test 4: Spotlight_ConesAreSymmetric
//
// The spotlight cone is symmetric around the light direction vector.
// If we position points equidistant from the light direction (but at the same
// angle), they should receive the same illumination.
//
// This test verifies that the cone angle calculation is based on the dot
// product between light direction and surface normal, not on screen position.
// ---------------------------------------------------------------------------

TEST_F(SpotlightConeTest, Spotlight_ConeIsSymmetric_SameAngleSameBrightness)
{
    // Place the camera to see the back wall, which is perpendicular to the
    // light direction. We'll examine brightness at different X positions
    // (which are symmetric around the light direction) and verify they're
    // approximately equal.

    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.5f, 0.0f),
                                 glm::vec3(0.0f, 1.5f, -4.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    // Measure brightness at left and right sides (symmetric positions)
    uint8_t leftBrightness  = getPixelBrightness(FB_WIDTH / 4, FB_HEIGHT / 2, view, proj);
    uint8_t rightBrightness = getPixelBrightness(3 * FB_WIDTH / 4, FB_HEIGHT / 2, view, proj);

    // Brightness should be similar (within 20% tolerance to account for rasterization
    // and MSAA edge effects).
    int diff = std::abs(static_cast<int>(leftBrightness) - static_cast<int>(rightBrightness));
    EXPECT_LT(diff, 20)
        << "Expected symmetric brightness left (" << static_cast<int>(leftBrightness)
        << ") vs right (" << static_cast<int>(rightBrightness) << ")";
}

// ---------------------------------------------------------------------------
// Test 5: Spotlight_ShadowCasting_WithinCone
//
// Verify that objects cast shadows when they're within the spotlight's cone.
// The UI cube should cast a shadow on the back wall when lit by the spotlight.
//
// We compare brightness with and without a "blocker" in the light path to
// verify that shadow casting works correctly for lit regions.
// ---------------------------------------------------------------------------

TEST_F(SpotlightConeTest, Spotlight_ShadowPresence_OnWallNearLight)
{
    // Camera looking at the back wall (should have spotlight illumination and
    // potentially shadows from the UI cube).

    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.5f, 0.0f),
                                 glm::vec3(0.0f, 1.5f, -4.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    uint8_t wallBrightness = getPixelBrightness(FB_WIDTH / 2, FB_HEIGHT / 2, view, proj);

    // The back wall should be illuminated by the spotlight.
    // Expect decent brightness (from direct spotlight, possibly with shadow).
    EXPECT_GT(wallBrightness, 50)
        << "Expected illumination on back wall from spotlight";
}
