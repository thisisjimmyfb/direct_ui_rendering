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

// Compute ripple displacement for floor surfaces (Y ≈ 0)
vec3 applyFloorRipple(vec3 worldPos, vec3 normal) {
    // Only apply to floor surfaces (Y close to 0 with normal pointing up/down)
    float floorness = abs(dot(normal, vec3(0, 1, 0)));
    if (floorness < 0.95) return worldPos;  // Not a floor surface

    // Create ripple pattern using multiple sine waves with different frequencies
    // and modulated by Perlin noise for natural variation

    // Wave 1: Horizontal ripple along X-Z plane (stronger amplitude for visibility)
    float ripple1 = 0.35 * sin(worldPos.x * 2.5 + time * 1.5) *
                    sin(worldPos.z * 1.8 + time * 1.2);

    // Wave 2: Circular ripple emanating from center (stronger amplitude)
    float distFromCenter = sqrt(worldPos.x * worldPos.x + worldPos.z * worldPos.z);
    float ripple2 = 0.28 * sin(distFromCenter * 3.0 - time * 2.0);

    // Wave 3: Noise-based organic ripple (stronger)
    float ripple3 = 0.20 * (noisePerlin(worldPos * 1.5 + time * 0.5) - 0.5) * 2.0;

    // Wave 4: Simple sinusoidal wave at higher frequency
    float ripple4 = 0.25 * sin((worldPos.x + worldPos.z) * 3.5 + time * 2.2);

    // Combine ripples
    float totalRipple = ripple1 + ripple2 + ripple3 + ripple4;

    // Apply displacement along Y (up/down direction)
    vec3 displacedPos = worldPos;
    displacedPos.y += totalRipple;

    return displacedPos;
}

void main() {
    // Apply ripple effect to floor vertices
    vec3 worldPos = applyFloorRipple(inPos, inNormal);
    vec4 worldPos4 = vec4(worldPos, 1.0);

    outWorldPos     = worldPos4.xyz;
    outNormal       = inNormal;
    outShadowCoord  = biasMat * lightViewProj * worldPos4;
    outUV           = inUV;
    outMaterial     = inMaterial;
    outColor        = inColor;
    gl_Position     = proj * view * worldPos4;
}
