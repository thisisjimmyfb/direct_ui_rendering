#include <gtest/gtest.h>
#include "renderer.h"
#include "scene.h"
#include "ui_surface.h"
#include "metrics.h"
#include "perf_reference.h"

// Number of headless frames to render for each measurement.
static constexpr int PERF_FRAME_COUNT = 60;

// ---------------------------------------------------------------------------
// Performance regression tests
// These tests will be skipped (GTEST_SKIP) until perf_reference.h is filled in.
// ---------------------------------------------------------------------------

class PerfTest : public ::testing::Test {
protected:
    Renderer renderer;
    Scene    scene;
    Metrics  metrics;

    void SetUp() override {
        ASSERT_TRUE(renderer.init(/*headless=*/true))
            << "Headless renderer init failed";
        scene.init();
    }

    void TearDown() override {
        renderer.cleanup();
    }
};

TEST_F(PerfTest, DirectMode_FrameTime_WithinTolerance)
{
    if (perf_ref::FRAME_TIME_DIRECT_MS == 0.0f) {
        GTEST_SKIP() << "perf_reference.h not yet filled in";
    }

    for (int i = 0; i < PERF_FRAME_COUNT; ++i) {
        metrics.beginFrame();
        // TODO: render one headless frame in direct mode
        metrics.endFrame();
    }

    float avg = metrics.averageFrameMs();
    float limit = perf_ref::FRAME_TIME_DIRECT_MS * (1.0f + perf_ref::TIME_TOLERANCE);
    EXPECT_LE(avg, limit)
        << "Direct mode frame time " << avg << " ms exceeds reference "
        << perf_ref::FRAME_TIME_DIRECT_MS << " ms (+" << perf_ref::TIME_TOLERANCE * 100 << "%)";
}

TEST_F(PerfTest, TraditionalMode_FrameTime_WithinTolerance)
{
    if (perf_ref::FRAME_TIME_TRADITIONAL_MS == 0.0f) {
        GTEST_SKIP() << "perf_reference.h not yet filled in";
    }

    for (int i = 0; i < PERF_FRAME_COUNT; ++i) {
        metrics.beginFrame();
        // TODO: render one headless frame in traditional mode
        metrics.endFrame();
    }

    float avg = metrics.averageFrameMs();
    float limit = perf_ref::FRAME_TIME_TRADITIONAL_MS * (1.0f + perf_ref::TIME_TOLERANCE);
    EXPECT_LE(avg, limit)
        << "Traditional mode frame time " << avg << " ms exceeds reference "
        << perf_ref::FRAME_TIME_TRADITIONAL_MS << " ms (+" << perf_ref::TIME_TOLERANCE * 100 << "%)";
}

TEST_F(PerfTest, DirectMode_GPUMem_WithinTolerance)
{
    if (perf_ref::GPU_MEM_DIRECT_BYTES == 0) {
        GTEST_SKIP() << "perf_reference.h not yet filled in";
    }

    // TODO: render one frame then query VMA
    metrics.updateGPUMem(renderer.getAllocator());
    uint64_t used = metrics.gpuAllocatedBytes();
    uint64_t limit = static_cast<uint64_t>(
        perf_ref::GPU_MEM_DIRECT_BYTES * (1.0 + perf_ref::MEM_TOLERANCE));

    EXPECT_LE(used, limit)
        << "Direct mode GPU mem " << used << " B exceeds reference "
        << perf_ref::GPU_MEM_DIRECT_BYTES << " B (+" << perf_ref::MEM_TOLERANCE * 100 << "%)";
}

TEST_F(PerfTest, TraditionalMode_GPUMem_WithinTolerance)
{
    if (perf_ref::GPU_MEM_TRADITIONAL_BYTES == 0) {
        GTEST_SKIP() << "perf_reference.h not yet filled in";
    }

    // TODO: toggle to traditional mode, render one frame, query VMA
    metrics.updateGPUMem(renderer.getAllocator());
    uint64_t used = metrics.gpuAllocatedBytes();
    uint64_t limit = static_cast<uint64_t>(
        perf_ref::GPU_MEM_TRADITIONAL_BYTES * (1.0 + perf_ref::MEM_TOLERANCE));

    EXPECT_LE(used, limit)
        << "Traditional mode GPU mem " << used << " B exceeds reference "
        << perf_ref::GPU_MEM_TRADITIONAL_BYTES << " B (+" << perf_ref::MEM_TOLERANCE * 100 << "%)";
}
