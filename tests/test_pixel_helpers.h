#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

/**
 * Shared pixel manipulation utilities for render tests.
 * These functions handle unpacking and sampling pixel data in RGBA8 format.
 */

namespace TestPixelHelpers {

// Unpack RGBA8 pixel data to normalized float4 (0.0 - 1.0 range)
inline glm::vec4 unpackPixel(const uint8_t* pixelData) {
    return glm::vec4(
        pixelData[0] / 255.0f,
        pixelData[1] / 255.0f,
        pixelData[2] / 255.0f,
        pixelData[3] / 255.0f
    );
}

// Get pointer to pixel data at (x, y) in a packed RGBA8 image
inline const uint8_t* samplePixel(const std::vector<uint8_t>& pixels,
                                   uint32_t x, uint32_t y,
                                   uint32_t width) {
    return pixels.data() + (y * width + x) * 4;
}

}  // namespace TestPixelHelpers
