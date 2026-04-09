#version 450

#include "common.glsl"
#include "ubo_structs.glsl"

// Fragment shader for the opaque teal UI surface quad.
// Renders the moving world-space quad as solid teal with PCF shadow.
// Used in direct mode: UI geometry is drawn on top of this quad.

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inShadowCoord;
layout(location = 2) in vec3 inNormalWorld;
layout(location = 3) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(inNormalWorld);

    // Per-fragment light vector for the spotlight.
    vec3 toLight = lightPos.xyz - inWorldPos;
    vec3 L = normalize(toLight);

    // NdotL for diffuse
    float NdotL = max(dot(N, L), 0.0);

    // Spotlight cone attenuation.
    float spotFactor = spotlightFactor(L, lightDir, lightColor);

    // Shadow sampling with slope-scaled 3-arg version
    float shadow = sampleShadowPCF(inShadowCoord, N, L, shadowMap);

    vec3 teal = vec3(0.0, 0.5, 0.5);

    // Ambient is always visible; diffuse uses shadow and spotFactor
    vec3 ambient = ambientColor.rgb;
    vec3 diffuse = shadow * lightColor.rgb * lightIntensity * spotFactor * NdotL;

    vec3 lit = clamp(ambient + diffuse, 0.0, 1.0);
    outColor = vec4(teal * lit, 1.0);
}
