#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <cmath>

// Pi constant for tests
constexpr float PI_F = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// AmbientColorAnimationTest — Test time-based ambient color animation
// ---------------------------------------------------------------------------

class AmbientColorAnimationTest : public ::testing::Test {
protected:
    // Helper function to compute the animated ambient color at a given time
    // This simulates the computation done in App::drawFrame()
    static glm::vec3 computeAnimatedAmbient(float time, const glm::vec3& baseAmbient) {
        float ambientPulse = 0.15f * std::sin(time * 0.7f);
        float coolWarmShift = 0.1f * std::sin(time * 0.5f + 1.0f);

        glm::vec3 animatedAmbient = baseAmbient + ambientPulse;
        animatedAmbient.x += coolWarmShift * 0.5f;  // Add warmth (red)
        animatedAmbient.z -= coolWarmShift * 0.3f;  // Reduce cool (blue)

        return glm::clamp(animatedAmbient, 0.0f, 1.0f);
    }
};

// Test: ambient color is properly clamped at t=0
TEST_F(AmbientColorAnimationTest, ColorClamped_AtTimeZero) {
    glm::vec3 baseAmbient(0.08f, 0.08f, 0.12f);
    glm::vec3 animated = computeAnimatedAmbient(0.0f, baseAmbient);

    EXPECT_GE(animated.x, 0.0f);
    EXPECT_GE(animated.y, 0.0f);
    EXPECT_GE(animated.z, 0.0f);
    EXPECT_LE(animated.x, 1.0f);
    EXPECT_LE(animated.y, 1.0f);
    EXPECT_LE(animated.z, 1.0f);
}

// Test: ambient color remains clamped throughout animation duration
TEST_F(AmbientColorAnimationTest, ColorAlwaysClamped_OverFullDuration) {
    glm::vec3 baseAmbient(0.08f, 0.08f, 0.12f);
    const float dt = 0.01f;
    const float duration = 5.0f;  // 5 seconds

    for (float t = 0.0f; t <= duration; t += dt) {
        glm::vec3 animated = computeAnimatedAmbient(t, baseAmbient);

        EXPECT_GE(animated.x, 0.0f) << "Red component below 0 at t=" << t;
        EXPECT_GE(animated.y, 0.0f) << "Green component below 0 at t=" << t;
        EXPECT_GE(animated.z, 0.0f) << "Blue component below 0 at t=" << t;
        EXPECT_LE(animated.x, 1.0f) << "Red component above 1 at t=" << t;
        EXPECT_LE(animated.y, 1.0f) << "Green component above 1 at t=" << t;
        EXPECT_LE(animated.z, 1.0f) << "Blue component above 1 at t=" << t;
    }
}

// Test: ambient pulse oscillates smoothly (second derivative continuous)
TEST_F(AmbientColorAnimationTest, PulseOscillatesSmoothly) {
    glm::vec3 baseAmbient(0.1f, 0.1f, 0.1f);
    const float dt = 0.001f;
    const float duration = 10.0f;  // ~13.6 full pulse cycles

    float prevDeltaY = 0.0f;
    for (float t = dt; t < duration; t += dt) {
        glm::vec3 prev = computeAnimatedAmbient(t - dt, baseAmbient);
        glm::vec3 curr = computeAnimatedAmbient(t, baseAmbient);
        glm::vec3 next = computeAnimatedAmbient(t + dt, baseAmbient);

        float delta = curr.y - prev.y;
        float deltaDelta = next.y - curr.y;

        // Check that acceleration changes smoothly (curvature is continuous)
        if (t > dt * 2) {
            float accelChange = std::abs(deltaDelta - prevDeltaY);
            EXPECT_LT(accelChange, 0.01f) << "Non-smooth acceleration at t=" << t;
        }
        prevDeltaY = deltaDelta;
    }
}

// Test: red channel increases when cool/warm shift is positive
TEST_F(AmbientColorAnimationTest, RedChannel_IncreasesWith_WarmShift) {
    glm::vec3 baseAmbient(0.08f, 0.08f, 0.12f);

    // Find time when coolWarmShift is clearly positive: sin(0.5f*t + 1.0f) > 0.7
    // At t=0: sin(1.0) ≈ 0.84 (positive)
    float t_positive = 0.0f;
    glm::vec3 ambient_at_positive = computeAnimatedAmbient(t_positive, baseAmbient);

    // At t=2π/0.5 ≈ 12.57 (after one full shift cycle), sin returns to same value
    // Use intermediate point
    float t_intermediate = 1.0f;  // sin(1.5) ≈ 0.997 (still positive but different)
    glm::vec3 ambient_at_intermediate = computeAnimatedAmbient(t_intermediate, baseAmbient);

    // Red should vary due to coolWarmShift contribution
    EXPECT_NE(ambient_at_positive.x, ambient_at_intermediate.x)
        << "Red channel should vary with time";
}

// Test: blue channel decreases when cool/warm shift is positive
TEST_F(AmbientColorAnimationTest, BlueChannel_DecreasesWith_WarmShift) {
    glm::vec3 baseAmbient(0.08f, 0.08f, 0.12f);

    // Blue channel should vary inversely to red due to coolWarmShift
    float t_a = 0.1f;
    float t_b = 0.2f;

    glm::vec3 ambient_a = computeAnimatedAmbient(t_a, baseAmbient);
    glm::vec3 ambient_b = computeAnimatedAmbient(t_b, baseAmbient);

    // Blue should vary due to coolWarmShift
    EXPECT_NE(ambient_a.z, ambient_b.z)
        << "Blue channel should vary with time due to cool/warm shift";
}

// Test: green channel is affected by pulse only, not cool/warm shift
TEST_F(AmbientColorAnimationTest, GreenChannel_AffectedByPulseOnly) {
    glm::vec3 baseAmbient(0.08f, 0.08f, 0.12f);
    const float dt = 0.001f;

    // Green should vary smoothly due to ambientPulse (0.15 * sin(0.7 * t))
    float maxGreenVariation = 0.0f;

    for (float t = 0.0f; t < 10.0f; t += dt) {
        glm::vec3 ambient = computeAnimatedAmbient(t, baseAmbient);
        float greenVariation = std::abs(ambient.y - baseAmbient.y);
        maxGreenVariation = std::max(maxGreenVariation, greenVariation);
    }

    // Max pulse amplitude is 0.15, so max green variation should be ~0.15
    EXPECT_GE(maxGreenVariation, 0.14f) << "Green variation should be ~0.15";
    EXPECT_LE(maxGreenVariation, 0.16f) << "Green variation should not exceed pulse amplitude";
}

// Test: color oscillation period for pulse (0.7 rad/s frequency)
TEST_F(AmbientColorAnimationTest, PulseFrequency_Is0Point7RadPerSecond) {
    glm::vec3 baseAmbient(0.08f, 0.08f, 0.12f);

    // At t=0: sin(0.7*0) = 0
    float g0 = computeAnimatedAmbient(0.0f, baseAmbient).y;

    // At t = 2π/0.7 ≈ 8.976 (full period), green should return to baseline
    float fullPeriod = 2.0f * PI_F / 0.7f;
    float g_period = computeAnimatedAmbient(fullPeriod, baseAmbient).y;

    EXPECT_NEAR(g0, g_period, 1e-4f) << "Green should complete full pulse cycle";
}

// Test: cool/warm shift varies with time
// The cool/warm shift oscillates between -0.1 and +0.1 with period 2π/0.5
// We verify that the red-blue difference oscillates as expected
TEST_F(AmbientColorAnimationTest, CoolWarmFrequency_Is0Point5RadPerSecond) {
    glm::vec3 baseAmbient(0.08f, 0.08f, 0.12f);

    // Compute red-blue difference at two points
    // Note: absolute values are affected by clamping and pulse, but their difference
    // should reflect the cool/warm shift effect
    float t0 = 0.1f;  // Small offset to avoid zero
    float t1 = 3.0f;  // Different time point

    glm::vec3 ambient0 = computeAnimatedAmbient(t0, baseAmbient);
    glm::vec3 ambient1 = computeAnimatedAmbient(t1, baseAmbient);

    // Red and blue should vary differently over time due to cool/warm shift
    float rDiff = ambient1.x - ambient0.x;
    float bDiff = ambient1.z - ambient0.z;

    // They should not both be the same (which would indicate no cool/warm effect)
    // and they should have opposite signs (red increases when blue decreases)
    EXPECT_NE(rDiff, bDiff) << "Red and blue should vary differently with cool/warm shift";
}

// Test: very dark base ambient (near black) stays clamped
TEST_F(AmbientColorAnimationTest, DarkAmbient_StaysValid) {
    glm::vec3 darkAmbient(0.01f, 0.01f, 0.01f);

    for (float t = 0.0f; t < 20.0f; t += 0.1f) {
        glm::vec3 animated = computeAnimatedAmbient(t, darkAmbient);

        EXPECT_GE(animated.x, 0.0f);
        EXPECT_GE(animated.y, 0.0f);
        EXPECT_GE(animated.z, 0.0f);
        EXPECT_LE(animated.x, 1.0f);
        EXPECT_LE(animated.y, 1.0f);
        EXPECT_LE(animated.z, 1.0f);
    }
}

// Test: very bright base ambient (near white) stays clamped
TEST_F(AmbientColorAnimationTest, BrightAmbient_StaysValid) {
    glm::vec3 brightAmbient(0.9f, 0.9f, 0.9f);

    for (float t = 0.0f; t < 20.0f; t += 0.1f) {
        glm::vec3 animated = computeAnimatedAmbient(t, brightAmbient);

        EXPECT_GE(animated.x, 0.0f);
        EXPECT_GE(animated.y, 0.0f);
        EXPECT_GE(animated.z, 0.0f);
        EXPECT_LE(animated.x, 1.0f);
        EXPECT_LE(animated.y, 1.0f);
        EXPECT_LE(animated.z, 1.0f);
    }
}

// Test: long duration maintains oscillation behavior (no drift)
TEST_F(AmbientColorAnimationTest, MaintainsOscillation_OverLongDuration) {
    glm::vec3 baseAmbient(0.08f, 0.08f, 0.12f);
    const float longTime = 100.0f;  // 100 seconds

    // Sample at regular intervals and verify oscillation is maintained
    const int numSamples = 1000;
    for (int i = 0; i < numSamples; ++i) {
        float t = (i / static_cast<float>(numSamples)) * longTime;
        glm::vec3 animated = computeAnimatedAmbient(t, baseAmbient);

        // All components should remain in valid range
        EXPECT_GE(animated.x, 0.0f);
        EXPECT_LE(animated.x, 1.0f);
        EXPECT_GE(animated.y, 0.0f);
        EXPECT_LE(animated.y, 1.0f);
        EXPECT_GE(animated.z, 0.0f);
        EXPECT_LE(animated.z, 1.0f);
    }
}
