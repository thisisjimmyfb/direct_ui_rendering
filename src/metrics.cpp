#include "metrics.h"
#include "ui_system.h"

#include <numeric>
#include <cstdio>

void Metrics::beginFrame()
{
    m_frameStart = std::chrono::high_resolution_clock::now();
}

void Metrics::endFrame()
{
    auto now = std::chrono::high_resolution_clock::now();
    float ms = std::chrono::duration<float, std::milli>(now - m_frameStart).count();

    m_frameTimes[m_frameIndex] = ms;
    m_frameIndex = (m_frameIndex + 1) % HISTORY_SIZE;
    if (m_frameIndex == 0) m_filled = true;
}

void Metrics::updateGPUMem(VmaAllocator allocator)
{
    if (!allocator) { m_gpuBytes = 0; return; }

    VmaTotalStatistics stats{};
    vmaCalculateStatistics(allocator, &stats);
    m_gpuBytes = stats.total.statistics.allocationBytes;
}

float Metrics::averageFrameMs() const
{
    int count = m_filled ? HISTORY_SIZE : m_frameIndex;
    if (count == 0) return 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < count; ++i) sum += m_frameTimes[i];
    return sum / static_cast<float>(count);
}

uint32_t Metrics::tessellateHUD(const UISystem& uiSystem,
                                RenderMode mode,
                                uint32_t msaaSamples,
                                std::vector<UIVertex>& outVerts) const
{
    char buf[64];
    float lineHeight = 36.0f;  // slightly larger than GLYPH_CELL (32) for spacing
    float x = 8.0f;
    uint32_t total = 0;

    // Line 0: mode
    const char* modeName = (mode == RenderMode::Direct) ? "DIRECT" : "TRADITIONAL";
    snprintf(buf, sizeof(buf), "Mode: %s", modeName);
    total += uiSystem.tessellateString(buf, x, 8.0f + 0 * lineHeight, outVerts);

    // Line 1: frame time
    snprintf(buf, sizeof(buf), "Frame: %.1f ms", averageFrameMs());
    total += uiSystem.tessellateString(buf, x, 8.0f + 1 * lineHeight, outVerts);

    // Line 2: GPU memory
    double mb = static_cast<double>(m_gpuBytes) / (1024.0 * 1024.0);
    snprintf(buf, sizeof(buf), "GPU Mem: %.1f MB", mb);
    total += uiSystem.tessellateString(buf, x, 8.0f + 2 * lineHeight, outVerts);

    // Line 3: MSAA
    snprintf(buf, sizeof(buf), "MSAA: %ux", msaaSamples);
    total += uiSystem.tessellateString(buf, x, 8.0f + 3 * lineHeight, outVerts);

    return total;
}
