#version 450

#include "common.glsl"

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 lightViewProj;
    vec4 lightPos;
    vec4 lightDir;
    vec4 lightColor;
    vec4 ambientColor;
    float lightIntensity;
    float uiColorPhase;
    float isTerminalMode;
    float time;
};

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 4) in vec2 inMaterial;  // metallic (x), roughness (y)
layout(location = 5) in vec3 inColor;     // surface color

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outShadowCoord;
layout(location = 3) out vec2 outUV;
layout(location = 4) out vec2 outMaterial;  // pass through material properties
layout(location = 5) out vec3 outColor;     // pass through surface color

// Small bias to avoid shadow acne on the shadow coordinate.
const mat4 biasMat = mat4(
    0.5, 0.0, 0.0, 0.0,
    0.0, 0.5, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.5, 0.5, 0.0, 1.0
);

void main() {
    // Floor and ceiling remain static (no ripple displacement)
    vec4 worldPos4 = vec4(inPos, 1.0);

    outWorldPos     = worldPos4.xyz;
    outNormal       = inNormal;
    outShadowCoord  = biasMat * lightViewProj * worldPos4;
    outUV           = inUV;
    outMaterial     = inMaterial;
    outColor        = inColor;
    gl_Position     = proj * view * worldPos4;
}
