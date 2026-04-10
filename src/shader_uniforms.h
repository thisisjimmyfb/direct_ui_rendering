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
    float     uiColorPhase;    // time-based color animation phase for UI text
    float     isTerminalMode;  // 1.0 if in terminal input mode, 0.0 otherwise
    float     time;            // elapsed time in seconds for ripple animation
};

struct SurfaceUBO {
    glm::mat4 totalMatrix;   // M_wc * M_sw * M_us
    glm::mat4 worldMatrix;   // M_sw * M_us (for clip distance computation)
    glm::vec4 clipPlanes[4];
    float     depthBias;
    float     _pad[3];
    glm::vec4 surfaceNormal; // xyz = world-space outward normal, w = 0
};
// surfaceNormal must land at byte 208 to match the GLSL std140 layout in ubo_structs.glsl.
// In GLSL, float _pad[3] would pad each element to 16 bytes (array-of-scalar rule),
// so ubo_structs.glsl uses float _pad0; float _pad1; float _pad2; instead.
static_assert(offsetof(SurfaceUBO, surfaceNormal) == 208,
    "SurfaceUBO::surfaceNormal offset mismatch — check _pad and GLSL ubo_structs.glsl");
