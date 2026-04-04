#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <cmath>

// Pi constant for tests
constexpr float PI_F = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// HSVToRGBTest — Test HSV to RGB color space conversion
// ---------------------------------------------------------------------------

class HSVToRGBTest : public ::testing::Test {
protected:
    // Helper function: HSV to RGB conversion (matches shader implementation)
    static glm::vec3 hsvToRgb(float h, float s, float v) {
        float c = v * s;
        float x = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
        float m = v - c;
        glm::vec3 rgb;

        if (h < 1.0f / 6.0f) rgb = glm::vec3(c, x, 0.0f);
        else if (h < 2.0f / 6.0f) rgb = glm::vec3(x, c, 0.0f);
        else if (h < 3.0f / 6.0f) rgb = glm::vec3(0.0f, c, x);
        else if (h < 4.0f / 6.0f) rgb = glm::vec3(0.0f, x, c);
        else if (h < 5.0f / 6.0f) rgb = glm::vec3(x, 0.0f, c);
        else rgb = glm::vec3(c, 0.0f, x);

        return rgb + glm::vec3(m);
    }
};

// Test: Red hue (0.0) produces red color
TEST_F(HSVToRGBTest, RedHue_ProducesRed) {
    glm::vec3 rgb = hsvToRgb(0.0f, 1.0f, 1.0f);
    EXPECT_NEAR(rgb.r, 1.0f, 1e-5f);
    EXPECT_NEAR(rgb.g, 0.0f, 1e-5f);
    EXPECT_NEAR(rgb.b, 0.0f, 1e-5f);
}

// Test: Green hue (1/3) produces green color
TEST_F(HSVToRGBTest, GreenHue_ProducesGreen) {
    glm::vec3 rgb = hsvToRgb(1.0f / 3.0f, 1.0f, 1.0f);
    EXPECT_NEAR(rgb.r, 0.0f, 1e-5f);
    EXPECT_NEAR(rgb.g, 1.0f, 1e-5f);
    EXPECT_NEAR(rgb.b, 0.0f, 1e-5f);
}

// Test: Blue hue (2/3) produces blue color
TEST_F(HSVToRGBTest, BlueHue_ProducesBlue) {
    glm::vec3 rgb = hsvToRgb(2.0f / 3.0f, 1.0f, 1.0f);
    EXPECT_NEAR(rgb.r, 0.0f, 1e-5f);
    EXPECT_NEAR(rgb.g, 0.0f, 1e-5f);
    EXPECT_NEAR(rgb.b, 1.0f, 1e-5f);
}

// Test: Low saturation produces less vibrant colors
TEST_F(HSVToRGBTest, LowSaturation_ProducesGrayer) {
    glm::vec3 rgbFull = hsvToRgb(0.0f, 1.0f, 1.0f);  // Red, full saturation
    glm::vec3 rgbLow = hsvToRgb(0.0f, 0.5f, 1.0f);   // Red, half saturation

    // Low saturation should make all channels closer to each other
    float deltaMag = std::abs(rgbFull.r - rgbFull.g) + std::abs(rgbFull.r - rgbFull.b);
    float deltaSmall = std::abs(rgbLow.r - rgbLow.g) + std::abs(rgbLow.r - rgbLow.b);

    EXPECT_GT(deltaMag, deltaSmall) << "Low saturation should reduce color vibrancy";
}

// Test: Zero saturation produces gray (independent of hue)
TEST_F(HSVToRGBTest, ZeroSaturation_ProducesGray) {
    glm::vec3 rgb1 = hsvToRgb(0.0f, 0.0f, 0.5f);     // Red hue, gray
    glm::vec3 rgb2 = hsvToRgb(0.5f, 0.0f, 0.5f);     // Cyan hue, gray

    EXPECT_NEAR(rgb1.r, 0.5f, 1e-5f);
    EXPECT_NEAR(rgb1.g, 0.5f, 1e-5f);
    EXPECT_NEAR(rgb1.b, 0.5f, 1e-5f);

    EXPECT_NEAR(rgb2.r, 0.5f, 1e-5f);
    EXPECT_NEAR(rgb2.g, 0.5f, 1e-5f);
    EXPECT_NEAR(rgb2.b, 0.5f, 1e-5f);
}

// Test: Zero value produces black
TEST_F(HSVToRGBTest, ZeroValue_ProducesBlack) {
    glm::vec3 rgb = hsvToRgb(0.5f, 1.0f, 0.0f);

    EXPECT_NEAR(rgb.r, 0.0f, 1e-5f);
    EXPECT_NEAR(rgb.g, 0.0f, 1e-5f);
    EXPECT_NEAR(rgb.b, 0.0f, 1e-5f);
}

// Test: Output is always in valid range [0, 1]
TEST_F(HSVToRGBTest, OutputRange_AlwaysValid) {
    const float step = 0.05f;
    for (float h = 0.0f; h <= 1.0f; h += step) {
        for (float s = 0.0f; s <= 1.0f; s += step) {
            for (float v = 0.0f; v <= 1.0f; v += step) {
                glm::vec3 rgb = hsvToRgb(h, s, v);

                EXPECT_GE(rgb.r, 0.0f) << "Red out of range at h=" << h << " s=" << s << " v=" << v;
                EXPECT_LE(rgb.r, 1.0f) << "Red out of range at h=" << h << " s=" << s << " v=" << v;
                EXPECT_GE(rgb.g, 0.0f) << "Green out of range at h=" << h << " s=" << s << " v=" << v;
                EXPECT_LE(rgb.g, 1.0f) << "Green out of range at h=" << h << " s=" << s << " v=" << v;
                EXPECT_GE(rgb.b, 0.0f) << "Blue out of range at h=" << h << " s=" << s << " v=" << v;
                EXPECT_LE(rgb.b, 1.0f) << "Blue out of range at h=" << h << " s=" << s << " v=" << v;
            }
        }
    }
}

// Test: Hue wraps at 1.0 (cycling)
TEST_F(HSVToRGBTest, HueWraps_AtOne) {
    glm::vec3 rgb0 = hsvToRgb(0.0f, 1.0f, 1.0f);
    glm::vec3 rgb1 = hsvToRgb(1.0f, 1.0f, 1.0f);

    // Both should be red (hue 1.0 wraps to hue 0.0)
    EXPECT_NEAR(rgb0.r, rgb1.r, 1e-4f);
    EXPECT_NEAR(rgb0.g, rgb1.g, 1e-4f);
    EXPECT_NEAR(rgb0.b, rgb1.b, 1e-4f);
}

// Test: Linear progression through hue creates rainbow
TEST_F(HSVToRGBTest, HueProgression_CreatesRainbow) {
    const int samples = 6;
    const float hueStep = 1.0f / samples;

    glm::vec3 previousRgb = hsvToRgb(0.0f, 1.0f, 1.0f);

    // As hue increases, color should change progressively
    for (int i = 1; i < samples; ++i) {
        float hue = i * hueStep;
        glm::vec3 currentRgb = hsvToRgb(hue, 1.0f, 1.0f);

        // At least one component should differ noticeably
        float colorDiff = glm::length(currentRgb - previousRgb);
        EXPECT_GT(colorDiff, 0.3f) << "Hue progression should produce distinct colors";

        previousRgb = currentRgb;
    }
}

// ---------------------------------------------------------------------------
// UIColorAnimationPhaseTest — Test phase-based animation
// ---------------------------------------------------------------------------

class UIColorAnimationPhaseTest : public ::testing::Test {
protected:
    // Helper: Compute hue from phase (4-second cycle, matching shader)
    static float phaseToHue(float uiColorPhase) {
        return std::fmod(uiColorPhase / 4.0f, 1.0f);
    }

    // Helper: HSV to RGB
    static glm::vec3 hsvToRgb(float h, float s, float v) {
        float c = v * s;
        float x = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
        float m = v - c;
        glm::vec3 rgb;

        if (h < 1.0f / 6.0f) rgb = glm::vec3(c, x, 0.0f);
        else if (h < 2.0f / 6.0f) rgb = glm::vec3(x, c, 0.0f);
        else if (h < 3.0f / 6.0f) rgb = glm::vec3(0.0f, c, x);
        else if (h < 4.0f / 6.0f) rgb = glm::vec3(0.0f, x, c);
        else if (h < 5.0f / 6.0f) rgb = glm::vec3(x, 0.0f, c);
        else rgb = glm::vec3(c, 0.0f, x);

        return rgb + glm::vec3(m);
    }
};

// Test: Phase 0 produces red
TEST_F(UIColorAnimationPhaseTest, Phase0_ProducesRed) {
    float hue = phaseToHue(0.0f);
    glm::vec3 rgb = hsvToRgb(hue, 1.0f, 1.0f);

    EXPECT_NEAR(rgb.r, 1.0f, 1e-4f);
    EXPECT_LT(rgb.g, 0.1f);
    EXPECT_LT(rgb.b, 0.1f);
}

// Test: Phase at 1 second produces different color
TEST_F(UIColorAnimationPhaseTest, Phase1Second_DifferentColor) {
    float hue0 = phaseToHue(0.0f);
    float hue1 = phaseToHue(1.0f);

    glm::vec3 rgb0 = hsvToRgb(hue0, 1.0f, 1.0f);
    glm::vec3 rgb1 = hsvToRgb(hue1, 1.0f, 1.0f);

    // Colors should be different
    float colorDiff = glm::length(rgb1 - rgb0);
    EXPECT_GT(colorDiff, 0.2f) << "Phase 1 second should produce noticeably different color";
}

// Test: Full 4-second cycle returns to starting color
TEST_F(UIColorAnimationPhaseTest, FullCycle_ReturnsToStart) {
    float hue0 = phaseToHue(0.0f);
    float hue4 = phaseToHue(4.0f);

    glm::vec3 rgb0 = hsvToRgb(hue0, 1.0f, 1.0f);
    glm::vec3 rgb4 = hsvToRgb(hue4, 1.0f, 1.0f);

    // Should return to red (same color)
    EXPECT_NEAR(rgb0.r, rgb4.r, 1e-4f);
    EXPECT_NEAR(rgb0.g, rgb4.g, 1e-4f);
    EXPECT_NEAR(rgb0.b, rgb4.b, 1e-4f);
}

// Test: Multiple cycles maintain consistency
TEST_F(UIColorAnimationPhaseTest, MultipleCycles_Consistent) {
    float hue1 = phaseToHue(1.0f);
    float hue5 = phaseToHue(5.0f);  // 1 + 4 (one full cycle)
    float hue9 = phaseToHue(9.0f);  // 1 + 8 (two full cycles)

    glm::vec3 rgb1 = hsvToRgb(hue1, 1.0f, 1.0f);
    glm::vec3 rgb5 = hsvToRgb(hue5, 1.0f, 1.0f);
    glm::vec3 rgb9 = hsvToRgb(hue9, 1.0f, 1.0f);

    // All should be the same color (phase mod cycle)
    EXPECT_NEAR(rgb1.r, rgb5.r, 1e-4f);
    EXPECT_NEAR(rgb1.g, rgb5.g, 1e-4f);
    EXPECT_NEAR(rgb1.b, rgb5.b, 1e-4f);

    EXPECT_NEAR(rgb1.r, rgb9.r, 1e-4f);
    EXPECT_NEAR(rgb1.g, rgb9.g, 1e-4f);
    EXPECT_NEAR(rgb1.b, rgb9.b, 1e-4f);
}

// Test: Smooth color progression (no jumps in animation)
TEST_F(UIColorAnimationPhaseTest, SmoothColorProgression) {
    const float dt = 0.01f;  // 10ms steps
    const float duration = 4.0f;  // One full cycle

    glm::vec3 prevRgb = hsvToRgb(phaseToHue(0.0f), 1.0f, 1.0f);

    for (float t = dt; t <= duration; t += dt) {
        float hue = phaseToHue(t);
        glm::vec3 currentRgb = hsvToRgb(hue, 1.0f, 1.0f);

        // Color should change smoothly (no large jumps)
        float colorDelta = glm::length(currentRgb - prevRgb);
        EXPECT_LT(colorDelta, 0.05f) << "Color should transition smoothly at t=" << t;

        prevRgb = currentRgb;
    }
}

// Test: Animation period is exactly 4 seconds
TEST_F(UIColorAnimationPhaseTest, PeriodIsExactly4Seconds) {
    const float epsilon = 1e-4f;

    for (float phase = 0.0f; phase < 16.0f; phase += 1.0f) {
        float hue1 = phaseToHue(phase);
        float hue2 = phaseToHue(phase + 4.0f);  // Add one full cycle

        // Hues should match (mod 1.0)
        EXPECT_NEAR(std::fmod(hue1, 1.0f), std::fmod(hue2, 1.0f), epsilon);
    }
}

// Test: Cyan hue (0.5) produces cyan color
TEST_F(HSVToRGBTest, CyanHue_ProducesCyan) {
    glm::vec3 rgb = hsvToRgb(0.5f, 1.0f, 1.0f);
    EXPECT_NEAR(rgb.r, 0.0f, 1e-5f);
    EXPECT_NEAR(rgb.g, 1.0f, 1e-5f);
    EXPECT_NEAR(rgb.b, 1.0f, 1e-5f);
}

// Test: Yellow hue (1/6) produces yellow color
TEST_F(HSVToRGBTest, YellowHue_ProducesYellow) {
    glm::vec3 rgb = hsvToRgb(1.0f / 6.0f, 1.0f, 1.0f);
    EXPECT_NEAR(rgb.r, 1.0f, 1e-5f);
    EXPECT_NEAR(rgb.g, 1.0f, 1e-5f);
    EXPECT_NEAR(rgb.b, 0.0f, 1e-5f);
}

// Test: Magenta hue (5/6) produces magenta color
TEST_F(HSVToRGBTest, MagentaHue_ProducesMagenta) {
    glm::vec3 rgb = hsvToRgb(5.0f / 6.0f, 1.0f, 1.0f);
    EXPECT_NEAR(rgb.r, 1.0f, 1e-5f);
    EXPECT_NEAR(rgb.g, 0.0f, 1e-5f);
    EXPECT_NEAR(rgb.b, 1.0f, 1e-5f);
}

// Test: Intermediate value levels produce expected brightness
TEST_F(HSVToRGBTest, IntermediateValue_ProducesCorrectBrightness) {
    glm::vec3 rgbDim = hsvToRgb(0.0f, 1.0f, 0.5f);    // Red at 50% brightness
    glm::vec3 rgbBright = hsvToRgb(0.0f, 1.0f, 1.0f); // Red at 100% brightness

    // Bright version should have all components greater or equal
    EXPECT_GE(rgbBright.r, rgbDim.r);
    EXPECT_GE(rgbBright.g, rgbDim.g);
    EXPECT_GE(rgbBright.b, rgbDim.b);

    // Dimmer red should be around 0.5, bright red should be 1.0
    EXPECT_NEAR(rgbDim.r, 0.5f, 1e-5f);
    EXPECT_NEAR(rgbBright.r, 1.0f, 1e-5f);
}

// Test: Phase at 2 seconds (mid-cycle) produces distinct color
TEST_F(UIColorAnimationPhaseTest, Phase2Seconds_MidCycleDifferentColor) {
    float hue0 = phaseToHue(0.0f);   // Red
    float hue2 = phaseToHue(2.0f);   // Mid-cycle (cyan-ish)
    float hue4 = phaseToHue(4.0f);   // Back to red

    glm::vec3 rgb0 = hsvToRgb(hue0, 1.0f, 1.0f);
    glm::vec3 rgb2 = hsvToRgb(hue2, 1.0f, 1.0f);
    glm::vec3 rgb4 = hsvToRgb(hue4, 1.0f, 1.0f);

    // Mid-cycle should be different from start/end
    float colorDiff02 = glm::length(rgb2 - rgb0);
    float colorDiff24 = glm::length(rgb4 - rgb2);
    EXPECT_GT(colorDiff02, 0.3f) << "Phase 2 should differ noticeably from phase 0";
    EXPECT_GT(colorDiff24, 0.3f) << "Phase 4 should differ noticeably from phase 2";

    // Start and end should be the same
    EXPECT_NEAR(rgb0.r, rgb4.r, 1e-4f);
    EXPECT_NEAR(rgb0.g, rgb4.g, 1e-4f);
    EXPECT_NEAR(rgb0.b, rgb4.b, 1e-4f);
}
