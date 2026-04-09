#version 450

#include "common.glsl"
#include "ubo_structs.glsl"

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;

layout(set = 2, binding = 0) uniform sampler2D uiAtlas;

// SDF threshold at push constant offset 64 (fragment stage).
// Set to 0.0 in bitmap mode; to ~0.5 in SDF mode.
layout(push_constant) uniform PC {
    layout(offset = 64) float sdfThreshold;
};

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inShadowCoord;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec4 outColor;

void main() {
#ifdef UI_TEST_COLOR
    outColor = vec4(1.0, 0.0, 1.0, 1.0);  // Solid magenta for render tests
#else
    // Static white color for UI text
    vec3 textColor = vec3(1.0);

    // Sample UI atlas
    vec4 texColor;
    if (sdfThreshold > 0.0) {
        // SDF mode: atlas is R8_UNORM, R channel is the signed distance field.
        float dist = texture(uiAtlas, inTexCoord).r;
        float spread = 0.07;
        float alpha = smoothstep(sdfThreshold - spread, sdfThreshold + spread, dist);
        texColor = vec4(alpha, alpha, alpha, alpha);  // pre-multiplied white text
    } else {
        texColor = texture(uiAtlas, inTexCoord);
    }

    // Lighting calculation matching surface.frag and room.frag
    vec3 N = normalize(inNormal);
    vec3 toLight = lightPos.xyz - inWorldPos;
    vec3 L = normalize(toLight);

    // NdotL for diffuse
    float NdotL = max(dot(N, L), 0.0);

    // Spotlight cone attenuation.
    float spotFactor = spotlightFactor(L, lightDir, lightColor);

    // Shadow sampling with slope-scaled 3-arg version
    float shadow = sampleShadowPCF(inShadowCoord, N, L, shadowMap);

    // Ambient is always visible; diffuse uses shadow and spotFactor
    vec3 ambient = ambientColor.rgb;
    vec3 diffuse = shadow * lightColor.rgb * lightIntensity * spotFactor * NdotL;

    vec3 lit = clamp(ambient + diffuse, 0.0, 1.0);

    // Output text with alpha blending: RGB channels modulated by static white and lighting, alpha preserved
    outColor = vec4(texColor.rgb * lit, texColor.a);
#endif
}
