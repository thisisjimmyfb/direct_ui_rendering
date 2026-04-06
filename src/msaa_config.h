#pragma once

#include <vulkan/vulkan.h>

// ---------------------------------------------------------------------------
// MSAA Sample Count Configuration
// ---------------------------------------------------------------------------
// Default MSAA_SAMPLES is 4x. Override at compile time with -DMSAA_SAMPLES=N
// where N is one of: 1, 2, 4, 8, or 16.
// ---------------------------------------------------------------------------

#ifndef MSAA_SAMPLES
#define MSAA_SAMPLES 4
#endif

// Map compile-time MSAA_SAMPLES to Vulkan VkSampleCountFlagBits
constexpr VkSampleCountFlagBits msaaSampleCount() {
    if constexpr (MSAA_SAMPLES == 1) return VK_SAMPLE_COUNT_1_BIT;
    else if constexpr (MSAA_SAMPLES == 2) return VK_SAMPLE_COUNT_2_BIT;
    else if constexpr (MSAA_SAMPLES == 4) return VK_SAMPLE_COUNT_4_BIT;
    else if constexpr (MSAA_SAMPLES == 8) return VK_SAMPLE_COUNT_8_BIT;
    else if constexpr (MSAA_SAMPLES == 16) return VK_SAMPLE_COUNT_16_BIT;
    else return VK_SAMPLE_COUNT_1_BIT;  // fallback
}
