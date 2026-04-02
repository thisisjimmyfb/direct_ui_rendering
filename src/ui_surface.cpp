#include "ui_surface.h"
#include <glm/gtc/matrix_transform.hpp>

glm::mat4 computeM_us(float W_ui, float H_ui)
{
    // Scale [0,W_ui]x[0,H_ui] to [0,1]x[0,1]; z and w unchanged.
    // Column-major:
    //   col0 = (1/W, 0,     0, 0)
    //   col1 = (0,   1/H,   0, 0)
    //   col2 = (0,   0,     1, 0)
    //   col3 = (0,   0,     0, 1)
    glm::mat4 m(1.0f);
    m[0][0] = 1.0f / W_ui;
    m[1][1] = 1.0f / H_ui;
    return m;
}

// Safe normalize that returns zero vector for near-zero inputs.
// This handles degenerate cases where edge vectors have zero length.
static glm::vec3 safeNormalize(glm::vec3 v)
{
    float lenSq = glm::dot(v, v);
    if (lenSq < 1e-12f) return glm::vec3(0.0f);
    return glm::normalize(v);
}

glm::mat4 computeM_sw(glm::vec3 P_00, glm::vec3 P_10, glm::vec3 P_01)
{
    glm::vec3 e_u = P_10 - P_00;                    // horizontal edge
    glm::vec3 e_v = P_01 - P_00;                    // vertical edge
    glm::vec3 n   = safeNormalize(glm::cross(e_u, e_v));   // surface normal

    // Column-major affine frame matrix:
    //   col0 = e_u (maps s=1 to P_00 + e_u)
    //   col1 = e_v (maps t=1 to P_00 + e_v)
    //   col2 = n   (unused for flat UI, but gives correct depth extent)
    //   col3 = P_00 (origin translation)
    // Note: glm::mat4 constructor takes columns as arguments, not rows.
    return glm::mat4(
        glm::vec4(e_u.x, e_u.y, e_u.z, 0.0f),  // column 0 = e_u
        glm::vec4(e_v.x, e_v.y, e_v.z, 0.0f),  // column 1 = e_v
        glm::vec4(n.x,     n.y,     n.z, 0.0f), // column 2 = n
        glm::vec4(P_00.x, P_00.y, P_00.z, 1.0f) // column 3 = P_00
    );
}

SurfaceTransforms computeSurfaceTransforms(glm::vec3 P_00, glm::vec3 P_10, glm::vec3 P_01,
                                           float W_ui, float H_ui,
                                           const glm::mat4& viewProj)
{
    SurfaceTransforms t;
    t.M_us    = computeM_us(W_ui, H_ui);
    t.M_sw    = computeM_sw(P_00, P_10, P_01);
    t.M_world = t.M_sw * t.M_us;
    t.M_total = viewProj * t.M_world;
    return t;
}


std::array<glm::vec4, 4> computeClipPlanes(glm::vec3 P_00, glm::vec3 P_10, glm::vec3 P_01)
{
    glm::vec3 e_u = P_10 - P_00;
    glm::vec3 e_v = P_01 - P_00;
    glm::vec3 n   = safeNormalize(glm::cross(e_u, e_v));

    // Inward-pointing edge normals (lie in the surface plane).
    glm::vec3 n_left   =  safeNormalize(glm::cross(e_v, n));  // left edge  -> inward = +u dir
    glm::vec3 n_right  = -n_left;                             // right edge -> inward = -u dir
    glm::vec3 n_top    =  safeNormalize(glm::cross(n, e_u));  // top edge   -> inward = +v dir
    glm::vec3 n_bottom = -n_top;                              // bottom edge-> inward = -v dir

    // P_11 = P_00 + e_u + e_v
    glm::vec3 P_10_w = P_10;
    glm::vec3 P_01_w = P_01;

    // Plane: (n.xyz, d) where d = -dot(n, point_on_plane)
    // dot(plane, worldPos4) = dot(n, pos) + d  >= 0 means inside
    auto makePlane = [](glm::vec3 normal, glm::vec3 pointOnPlane) -> glm::vec4 {
        return glm::vec4(normal, -glm::dot(normal, pointOnPlane));
    };

    return {
        makePlane(n_left,   P_00),    // left plane through P_00/P_01
        makePlane(n_right,  P_10_w),  // right plane through P_10/P_11
        makePlane(n_top,    P_00),    // top plane through P_00/P_10
        makePlane(n_bottom, P_01_w),  // bottom plane through P_01/P_11
    };
}

SurfaceTransforms computeFaceTransforms(glm::vec3 P_00, glm::vec3 P_10, glm::vec3 P_01, glm::vec3 P_11,
                                        float W_ui, float H_ui, const glm::mat4& viewProj)
{
    SurfaceTransforms t;
    t.M_us = computeM_us(W_ui, H_ui);
    // P_11 is not used in the matrix computation; only P_00, P_10, P_01 define the affine frame.
    t.M_sw = computeM_sw(P_00, P_10, P_01);
    t.M_world = t.M_sw * t.M_us;
    t.M_total = viewProj * t.M_world;
    return t;
}
