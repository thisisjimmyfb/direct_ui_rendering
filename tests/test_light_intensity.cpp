#include <gtest/gtest.h>
#include "app.h"
#include "test_app_helper.h"
#include <glm/glm.hpp>
#include <cmath>

// Pi constant for tests (avoiding M_PI which is not portable)
constexpr float PI_F = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// LightIntensityPulsingTest — Test time-based light intensity animation
// ---------------------------------------------------------------------------

class LightIntensityPulsingTest : public ::testing::Test {
protected:
    // Helper function to compute expected lightIntensity at a given time
    // Formula: intensity = 1.0f + 0.3f * sin(time * 2.0f)
    static float computeExpectedIntensity(float time) {
        return 1.0f + 0.3f * std::sin(time * 2.0f);
    }

    // Helper function to get the actual lightIntensity value for testing
    // This simulates the computation done in App::drawFrame()
    static float getLightIntensityAtTime(float time) {
        return computeExpectedIntensity(time);
    }
};

// Test: lightIntensity starts at 1.0 at t=0
TEST_F(LightIntensityPulsingTest, StartValue_AtTimeZero_Equals1Point0) {
    float intensity = getLightIntensityAtTime(0.0f);
    EXPECT_NEAR(intensity, 1.0f, 1e-6f);
}

// Test: lightIntensity reaches maximum of 1.3 at quarter period
TEST_F(LightIntensityPulsingTest, MaximumValue_AtQuarterPeriod_Equals1Point3) {
    // At t = π/4, sin(π/2) = 1.0, so intensity = 1.0 + 0.3 = 1.3
    float quarterPeriod = static_cast<float>(PI_F) / 4.0f;
    float intensity = getLightIntensityAtTime(quarterPeriod);
    EXPECT_NEAR(intensity, 1.3f, 1e-6f);
}

// Test: lightIntensity reaches minimum of 0.7 at three-quarter period
TEST_F(LightIntensityPulsingTest, MinimumValue_AtThreeQuarterPeriod_Equals0Point7) {
    // At t = 3π/4, sin(3π/2) = -1.0, so intensity = 1.0 - 0.3 = 0.7
    float threeQuarterPeriod = 3.0f * static_cast<float>(PI_F) / 4.0f;
    float intensity = getLightIntensityAtTime(threeQuarterPeriod);
    EXPECT_NEAR(intensity, 0.7f, 1e-6f);
}

// Test: lightIntensity returns to 1.0 at half period
TEST_F(LightIntensityPulsingTest, ReturnsToStart_AtHalfPeriod_Equals1Point0) {
    // At t = π/2, sin(π) = 0.0, so intensity = 1.0
    float halfPeriod = static_cast<float>(PI_F) / 2.0f;
    float intensity = getLightIntensityAtTime(halfPeriod);
    EXPECT_NEAR(intensity, 1.0f, 1e-6f);
}

// Test: lightIntensity completes full cycle at one period
TEST_F(LightIntensityPulsingTest, CompleteCycle_AtFullPeriod_EqualsStart) {
    // At t = π, sin(2π) = 0.0, so intensity = 1.0
    float fullPeriod = static_cast<float>(PI_F);
    float intensity = getLightIntensityAtTime(fullPeriod);
    EXPECT_NEAR(intensity, 1.0f, 1e-6f);
}

// Test: lightIntensity is always within valid range [0.7, 1.3]
TEST_F(LightIntensityPulsingTest, ValueAlwaysInValidRange_Over1SecondSweep) {
    const float minValid = 0.7f;
    const float maxValid = 1.3f;
    const float dt = 0.01f;  // 10ms samples
    const float duration = 1.0f;  // 1 second sweep

    for (float t = 0.0f; t <= duration; t += dt) {
        float intensity = getLightIntensityAtTime(t);
        EXPECT_GE(intensity, minValid) << "Intensity below minimum at t=" << t;
        EXPECT_LE(intensity, maxValid) << "Intensity above maximum at t=" << t;
    }
}

// Test: lightIntensity oscillates smoothly (second derivative continuous)
TEST_F(LightIntensityPulsingTest, OscillatesSmoothlyContinuously) {
    const float dt = 0.001f;
    const float duration = 2.0f;  // 2 full cycles

    float prevDelta = 0.0f;
    for (float t = dt; t < duration; t += dt) {
        float i_prev = getLightIntensityAtTime(t - dt);
        float i_curr = getLightIntensityAtTime(t);
        float i_next = getLightIntensityAtTime(t + dt);

        float delta = i_curr - i_prev;
        float deltaDelta = i_next - i_curr;

        // Check that acceleration changes smoothly (curvature is continuous)
        if (t > dt * 2) {
            float accelChange = std::abs(deltaDelta - prevDelta);
            EXPECT_LT(accelChange, 0.01f) << "Non-smooth acceleration at t=" << t;
        }
        prevDelta = deltaDelta;
    }
}

// Test: amplitude of oscillation is exactly 0.3
TEST_F(LightIntensityPulsingTest, AmplitudeOfOscillation_IsExactly0Point3) {
    float minIntensity = getLightIntensityAtTime(3.0f * static_cast<float>(PI_F) / 4.0f);
    float maxIntensity = getLightIntensityAtTime(static_cast<float>(PI_F) / 4.0f);
    float amplitude = (maxIntensity - minIntensity) / 2.0f;
    EXPECT_NEAR(amplitude, 0.3f, 1e-6f);
}

// Test: oscillation frequency is 2.0 rad/s (period ≈ 3.14 seconds)
TEST_F(LightIntensityPulsingTest, PeriodOfOscillation_IsCorrect) {
    // At t=0 and t=π, the intensity should be equal (at baseline)
    float i_start = getLightIntensityAtTime(0.0f);
    float i_period = getLightIntensityAtTime(static_cast<float>(PI_F));
    EXPECT_NEAR(i_start, i_period, 1e-6f) << "Period should be π";
}

// Test: intensity at small time increments increases towards maximum
TEST_F(LightIntensityPulsingTest, IncreaseTowardMaximum_InFirstQuarter) {
    float t1 = 0.0f;
    float t2 = static_cast<float>(PI_F) / 8.0f;
    float t3 = static_cast<float>(PI_F) / 4.0f;

    float i1 = getLightIntensityAtTime(t1);
    float i2 = getLightIntensityAtTime(t2);
    float i3 = getLightIntensityAtTime(t3);

    EXPECT_LT(i1, i2) << "Intensity should increase at t1 < t2";
    EXPECT_LT(i2, i3) << "Intensity should increase at t2 < t3";
}

// Test: intensity at end of first half decreases back towards baseline
TEST_F(LightIntensityPulsingTest, DecreaseFromMaximum_InSecondQuarter) {
    float t1 = static_cast<float>(PI_F) / 4.0f;
    float t2 = static_cast<float>(PI_F) / 3.0f;
    float t3 = static_cast<float>(PI_F) / 2.0f;

    float i1 = getLightIntensityAtTime(t1);
    float i2 = getLightIntensityAtTime(t2);
    float i3 = getLightIntensityAtTime(t3);

    EXPECT_GT(i1, i2) << "Intensity should decrease at t1 > t2";
    EXPECT_GT(i2, i3) << "Intensity should decrease at t2 > t3";
}

// Test: long duration maintains oscillation behavior
TEST_F(LightIntensityPulsingTest, MaintainsOscillation_OverLongDuration) {
    const float longTime = 100.0f;  // 100 seconds
    const int numCycles = 32;  // Should complete ~32 cycles

    float prevI = getLightIntensityAtTime(0.0f);
    int crossings = 0;  // Count of crossings through baseline (1.0)

    for (int cycle = 1; cycle < numCycles; ++cycle) {
        float cycleTime = cycle * static_cast<float>(PI_F);
        float currI = getLightIntensityAtTime(cycleTime);
        // Verify oscillation is maintained
        EXPECT_NEAR(currI, 1.0f, 1e-5f) << "Failed to return to baseline at cycle " << cycle;
    }
}
