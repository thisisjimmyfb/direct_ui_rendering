#pragma once

#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// GPU-side uniform buffer structs (std140 layout)
// ---------------------------------------------------------------------------

struct SceneUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 lightViewProj;
    glm::vec4 lightPos;        // xyz = spotlight world position
    glm::vec4 lightDir;        // xyz = spotlight direction, w = cos(outerConeAngle)
    glm::vec4 lightColor;      // rgb = color, w = cos(innerConeAngle)
    glm::vec4 ambientColor;
    float     lightIntensity;  // time-based pulsing intensity multiplier
    float     _pad[3];         // padding for alignment
};

struct SurfaceUBO {
    glm::mat4 totalMatrix;   // M_wc * M_sw * M_us
    glm::mat4 worldMatrix;   // M_sw * M_us (for clip distance computation)
    glm::vec4 clipPlanes[4];
    float     depthBias;
    float     _pad[3];
};
