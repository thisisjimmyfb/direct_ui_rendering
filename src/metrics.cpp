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
                                std::vector<UIVertex>& outVerts,
                                const char* inputModeStr,
                                bool paused) const
{
    if (!uiSystem.isGlyphTableBuilt()) return 0;

    char buf[64];
    // Line height: GLYPH_CELL (32) + 8px spacing for clear vertical separation
    float lineHeight = 40.0f;
    // Left margin: consistent for all HUD elements
    float leftMargin = 8.0f;
    uint32_t total = 0;

    // Line 0: mode (bold header style)
    const char* modeName = (mode == RenderMode::Direct) ? "DIRECT" : "TRADITIONAL";
    snprintf(buf, sizeof(buf), "Mode: %s", modeName);
    total += uiSystem.tessellateString(buf, leftMargin, leftMargin + 0 * lineHeight, outVerts);

    // Line 1: render mode toggle
    snprintf(buf, sizeof(buf), "  [Space] toggle render mode");
    total += uiSystem.tessellateString(buf, leftMargin, leftMargin + 1 * lineHeight, outVerts);

    // Line 2: input mode toggle (Camera/Terminal)
    snprintf(buf, sizeof(buf), "  [Tab] toggle input mode");
    total += uiSystem.tessellateString(buf, leftMargin, leftMargin + 2 * lineHeight, outVerts);

    // Line 3: depth bias adjustment
    snprintf(buf, sizeof(buf), "  [+] [-] adjust depth bias");
    total += uiSystem.tessellateString(buf, leftMargin, leftMargin + 3 * lineHeight, outVerts);

    // Line 4: quad width controls ([ and ] keys)
    snprintf(buf, sizeof(buf), "  [[] []] quad width");
    total += uiSystem.tessellateString(buf, leftMargin, leftMargin + 4 * lineHeight, outVerts);

    // Line 5: quad height controls
    snprintf(buf, sizeof(buf), "  [O] [P] quad height");
    total += uiSystem.tessellateString(buf, leftMargin, leftMargin + 5 * lineHeight, outVerts);

    // Line 6: mouse look toggle
    snprintf(buf, sizeof(buf), "  [RClick] mouse look");
    total += uiSystem.tessellateString(buf, leftMargin, leftMargin + 6 * lineHeight, outVerts);

    // Line 7: input mode toggle (optional, non-empty)
    int nextLine = 7;
    if (inputModeStr && inputModeStr[0] != '\0') {
        total += uiSystem.tessellateString(inputModeStr, leftMargin, leftMargin + nextLine * lineHeight, outVerts);
        ++nextLine;
    }

    // Frame time, GPU memory, MSAA follow immediately after (no gap when inputModeStr is absent)
    snprintf(buf, sizeof(buf), "Frame: %.1f ms", averageFrameMs());
    total += uiSystem.tessellateString(buf, leftMargin, leftMargin + nextLine * lineHeight, outVerts);
    ++nextLine;

    double mb = static_cast<double>(m_gpuBytes) / (1024.0 * 1024.0);
    snprintf(buf, sizeof(buf), "GPU Mem: %.1f MB", mb);
    total += uiSystem.tessellateString(buf, leftMargin, leftMargin + nextLine * lineHeight, outVerts);
    ++nextLine;

    snprintf(buf, sizeof(buf), "MSAA: %ux", msaaSamples);
    total += uiSystem.tessellateString(buf, leftMargin, leftMargin + nextLine * lineHeight, outVerts);
    ++nextLine;

    snprintf(buf, sizeof(buf), "  [F] pause/resume");
    total += uiSystem.tessellateString(buf, leftMargin, leftMargin + nextLine * lineHeight, outVerts);
    ++nextLine;

    if (paused) {
        snprintf(buf, sizeof(buf), "Status: PAUSED");
        total += uiSystem.tessellateString(buf, leftMargin, leftMargin + nextLine * lineHeight, outVerts);
    }

    return total;
}
