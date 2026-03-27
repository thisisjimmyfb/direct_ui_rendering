#pragma once

#include <vk_mem_alloc.h>
#include <chrono>
#include <array>
#include <string>
#include <cstdint>

// Forward declaration to avoid pulling in all of ui_system.h in every TU.
class UISystem;
struct UIVertex;

// Modes for display in the metrics overlay.
enum class RenderMode { Direct, Traditional };

// Metrics tracks frame timing, VMA memory stats, and generates HUD vertex data.
class Metrics {
public:
    // Call at the start of each frame.
    void beginFrame();
    // Call at the end of each frame to record elapsed time.
    void endFrame();

    // Query total VMA allocated bytes.  allocator may be VK_NULL_HANDLE (returns 0).
    void updateGPUMem(VmaAllocator allocator);

    // Format the HUD lines and tessellate them using uiSystem.
    // inputModeStr is an optional 5th line (pass nullptr to omit).
    // Returns the number of vertices written into outVerts.
    uint32_t tessellateHUD(const UISystem& uiSystem,
                           RenderMode mode,
                           uint32_t msaaSamples,
                           std::vector<UIVertex>& outVerts,
                           const char* inputModeStr = nullptr) const;

    float averageFrameMs() const;
    uint64_t gpuAllocatedBytes() const { return m_gpuBytes; }

private:
    static constexpr int HISTORY_SIZE = 60;

    std::array<float, HISTORY_SIZE> m_frameTimes{};
    int    m_frameIndex{0};
    bool   m_filled{false};

    std::chrono::high_resolution_clock::time_point m_frameStart;

    uint64_t m_gpuBytes{0};
};
