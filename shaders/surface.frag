#version 450

#include "common.glsl"
#include "ubo_structs.glsl"

// Fragment shader for the opaque teal UI surface quad.
// Renders the moving world-space quad as solid teal with PCF shadow.
// Used in direct mode: UI geometry is drawn on top of this quad.

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inShadowCoord;

layout(location = 0) out vec4 outColor;

void main() {
    float shadow = sampleShadowPCF(inShadowCoord, shadowMap);
    vec3 teal = vec3(0.0, 0.5, 0.5);
    vec3 lit   = clamp(ambientColor.rgb + shadow * lightColor.rgb * lightIntensity, 0.0, 1.0);
    outColor   = vec4(teal * lit, 1.0);
}
