#version 450

#include "common.glsl"
#include "ubo_structs.glsl"

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;
layout(set = 2, binding = 1) uniform sampler2D uiRT;

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inShadowCoord;
layout(location = 2) in vec3 inNormalWorld;
layout(location = 3) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 uiColor = texture(uiRT, inTexCoord);

#ifdef UI_TEST_COLOR
    // Skip lighting in test mode so the raw UI color (e.g. magenta from the
    // test shader) reaches the readback buffer unmodified and is detectable
    // by the isMagenta() check in test_containment.cpp.
    outColor = uiColor;
#else
    vec3 N = normalize(inNormalWorld);

    // Per-fragment light vector for the spotlight.
    vec3 toLight = lightPos.xyz - inWorldPos;
    vec3 L = normalize(toLight);

    // NdotL for diffuse (Lambert)
    float NdotL = max(dot(N, L), 0.0);

    // Spotlight cone attenuation.
    float spotFactor = spotlightFactor(L, lightDir, lightColor);

    // Shadow sampling with slope-scaled bias (matches surface.frag)
    float shadow = sampleShadowPCF(inShadowCoord, N, L, shadowMap);

    // Teal base color; premul-alpha blend the UI texture on top.
    // uiColor.rgb is premultiplied, so: out = ui.rgb + teal * (1 - ui.a)
    vec3 teal = vec3(0.0, 0.5, 0.5);
    vec3 composited = uiColor.rgb + teal * (1.0 - uiColor.a);

    // Ambient is always visible; diffuse uses shadow, spotFactor, and NdotL
    vec3 ambient = ambientColor.rgb;
    vec3 diffuse = shadow * lightColor.rgb * lightIntensity * spotFactor * NdotL;

    vec3 lit = clamp(ambient + diffuse, 0.0, 1.0);
    outColor = vec4(composited * lit, 1.0);
#endif
}
