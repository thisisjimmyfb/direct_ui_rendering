#version 450

// Vertex shader for the composite surface quad (traditional mode).
// Transforms world-space vertex positions by the scene VP matrix, passes UVs through,
// and computes shadow coordinates for shadow-map sampling in composite.frag.

#include "ubo_structs.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in int inFaceIndex;
layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outShadowCoord;
layout(location = 2) out vec3 inNormalWorld;
layout(location = 3) out vec3 inWorldPos;

// Maps NDC [-1,1] to UV [0,1] for shadow map sampling.
const mat4 biasMat = mat4(
    0.5, 0.0, 0.0, 0.0,
    0.0, 0.5, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.5, 0.5, 0.0, 1.0
);

void main() {
    vec4 worldPos4 = vec4(inPos, 1.0);
    gl_Position    = proj * view * worldPos4;
    outUV          = inUV;
    outShadowCoord = biasMat * lightViewProj * worldPos4;
    inNormalWorld  = inNormal;  // Pass normal to fragment shader
    inWorldPos     = inPos;     // Pass world position to fragment shader
    // inFaceIndex is consumed but not used for the teal cube surface
}
