#include <gtest/gtest.h>
#include "metrics.h"
#include "ui_system.h"

#include <thread>
#include <chrono>

// ---------------------------------------------------------------------------
// MetricsTest — frame timing ring buffer and HUD tessellation
// ---------------------------------------------------------------------------

// After recording exactly HISTORY_SIZE=60 frames the ring buffer is full.
// averageFrameMs() must use exactly 60 samples.  The guard here is the
// off-by-one case where m_filled is never set (or is set one frame too late),
// causing averageFrameMs() to return 0.0f because count collapses to 0.
TEST(MetricsTest, FrameTimingRollingAverage_WrapsCorrectly)
{
    Metrics m;

    // Pre-condition: no frames recorded yet.
    EXPECT_FLOAT_EQ(m.averageFrameMs(), 0.0f);

    // Record 59 near-instant frames followed by one frame that sleeps 1 ms.
    // This guarantees the average is non-zero after the buffer fills, while
    // keeping the total sleep time to a single millisecond.
    constexpr int HISTORY = 60;
    for (int i = 0; i < HISTORY - 1; ++i) {
        m.beginFrame();
        m.endFrame();
    }
    m.beginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    m.endFrame();

    // Buffer is now full (m_filled == true).  If the wrap index check is
    // off-by-one, m_filled stays false, count == 0, and the result is 0.
    float avg = m.averageFrameMs();
    EXPECT_GT(avg, 0.0f)
        << "averageFrameMs() returned 0 after " << HISTORY
        << " frames — m_filled was not set at the correct wrap index";

    // The average of 60 frames where only the last frame is ~1 ms should be
    // well below 1 ms.  If count were wrong (e.g. 1 instead of 60) the result
    // would be approximately 1 ms, catching the bug.
    EXPECT_LT(avg, 0.5f)
        << "average too large — count may be 1 instead of " << HISTORY;

    // Record a second full pass (second wrap-around).  The 1 ms frame is
    // overwritten; all current frames are near-instant.
    for (int i = 0; i < HISTORY; ++i) {
        m.beginFrame();
        m.endFrame();
    }

    float avg2 = m.averageFrameMs();
    EXPECT_GE(avg2, 0.0f)
        << "averageFrameMs() negative after second wrap-around";
    EXPECT_LT(avg2, 0.5f)
        << "averageFrameMs() unexpectedly large after second wrap-around";
}

// ---------------------------------------------------------------------------
// MetricsTest — null VmaAllocator sets gpuAllocatedBytes to zero
// ---------------------------------------------------------------------------

// updateGPUMem(VK_NULL_HANDLE) must set gpuAllocatedBytes() to 0 rather than
// dereferencing the null handle.  This test guards against the null-check
// being accidentally removed in a future refactor.
TEST(MetricsTest, UpdateGPUMem_NullAllocator_SetsZero)
{
    Metrics m;
    m.updateGPUMem(VK_NULL_HANDLE);
    EXPECT_EQ(m.gpuAllocatedBytes(), 0u)
        << "gpuAllocatedBytes() must be 0 when allocator is VK_NULL_HANDLE";
}

// ---------------------------------------------------------------------------
// MetricsTest — averageFrameMs returns exactly zero when no frames recorded
// ---------------------------------------------------------------------------

// A freshly constructed Metrics object must return exactly 0.0f from
// averageFrameMs() before any beginFrame/endFrame calls.  This guards against
// using uninitialized ring-buffer data (garbage values from the stack) as
// the average, which would silently corrupt frame timing metrics.
TEST(MetricsTest, AverageFrameMs_ExactlyZeroWhenNoFrames)
{
    Metrics m;
    // Pre-condition: no frames recorded yet.
    EXPECT_FLOAT_EQ(m.averageFrameMs(), 0.0f)
        << "averageFrameMs() must return exactly 0.0f on a fresh Metrics object, "
           "not uninitialized ring-buffer data";
}

// ---------------------------------------------------------------------------
// MetricsTest — averageFrameMs returns a positive value after a single frame
// ---------------------------------------------------------------------------

// After exactly one beginFrame/endFrame cycle that includes a brief sleep,
// averageFrameMs() must return a strictly positive value.  The single-sample
// path in averageFrameMs() uses count = m_frameIndex (== 1), so the only
// failure modes are:
//   1. count is treated as 0 → divide-by-zero guard returns 0.0f (first sample
//      is silently discarded because m_filled never gets set for a single frame)
//   2. count is negative or overflows → undefined behaviour / negative average
// The sleep ensures the measured duration is non-trivially positive (> 0 μs),
// so any value > 0.0f confirms the single-sample path works correctly.
TEST(MetricsTest, AverageFrameMs_SingleFrame_ReturnsPositiveValue)
{
    Metrics m;

    m.beginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    m.endFrame();

    float avg = m.averageFrameMs();
    EXPECT_GT(avg, 0.0f)
        << "averageFrameMs() returned " << avg
        << " after one frame with a 1 ms sleep — single-sample path may be "
           "dividing by zero or discarding the first sample (count == 0 guard "
           "triggered when m_frameIndex should be 1 and m_filled is false)";
}

// ---------------------------------------------------------------------------
// MetricsTest — averageFrameMs uses count=N for partial fill (N < HISTORY_SIZE)
// ---------------------------------------------------------------------------

// Record exactly 30 fast frames followed by 1 slow frame (1 ms sleep), so the
// ring buffer is partially filled (31 samples out of 60).  The average must
// reflect all 31 samples, not HISTORY_SIZE (60).
//
// Failure mode this test guards against:
//   Bug: count = HISTORY_SIZE regardless of m_filled
//   Effect: The 29 zero-initialized slots dilute the sum, making avg ≈ 1ms/60
//           instead of the correct avg ≈ 1ms/31.  We distinguish the two cases
//           by checking avg > 1ms/40 (a threshold between 1/31 and 1/60).
TEST(MetricsTest, AverageFrameMs_PartialFill_UsesRecordedCountNotHistorySize)
{
    Metrics m;

    // Record 30 near-instant frames.
    for (int i = 0; i < 30; ++i) {
        m.beginFrame();
        m.endFrame();
    }

    // Record one slow frame (~1 ms).
    m.beginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    m.endFrame();

    // Buffer has 31 samples; m_filled is still false.
    float avg = m.averageFrameMs();
    EXPECT_GT(avg, 0.0f)
        << "averageFrameMs() returned 0 with 31 frames — partial-fill path broken";

    // If count were HISTORY_SIZE (60) the average would be ~1ms/60 ≈ 0.017 ms.
    // With the correct count (31) the average is ~1ms/31 ≈ 0.032 ms.
    // Threshold 1ms/40 = 0.025 ms sits between the two cases.
    EXPECT_GT(avg, 1.0f / 40.0f)
        << "averageFrameMs() too small — count may be HISTORY_SIZE (60) instead "
           "of the actual 31 recorded samples; partial-fill branch not using "
           "m_frameIndex as the divisor";
}
