#pragma once

#include <glm/glm.hpp>
#include <array>

// Results of computing all surface-related transforms from corner positions.
struct SurfaceTransforms {
    glm::mat4 M_us;     // UI space [0,W]x[0,H] -> normalized surface space [0,1]x[0,1]
    glm::mat4 M_sw;     // normalized surface space -> world space
    glm::mat4 M_world;  // M_sw * M_us  (world-space position for any UI point)
    glm::mat4 M_total;  // M_wc * M_sw * M_us  (clip-space position; requires viewProj)
};

// Compute M_us: scale-only matrix mapping [0,W_ui]x[0,H_ui] -> [0,1]x[0,1].
glm::mat4 computeM_us(float W_ui, float H_ui);

// Compute M_sw: affine matrix mapping normalized surface space to world space.
// P_00 = top-left, P_10 = top-right, P_01 = bottom-left corner in world space.
glm::mat4 computeM_sw(glm::vec3 P_00, glm::vec3 P_10, glm::vec3 P_01);

// Compute all surface transforms in one call given surface corners, canvas dimensions,
// and the current view-projection matrix.
SurfaceTransforms computeSurfaceTransforms(glm::vec3 P_00, glm::vec3 P_10, glm::vec3 P_01,
                                           float W_ui, float H_ui,
                                           const glm::mat4& viewProj);

// Compute four inward-facing world-space clip planes from the surface edges.
// Plane equation: dot(plane.xyz, worldPos) + plane.w >= 0 means inside.
// Order: left, right, top, bottom.
std::array<glm::vec4, 4> computeClipPlanes(glm::vec3 P_00, glm::vec3 P_10, glm::vec3 P_01);
