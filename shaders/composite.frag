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
};

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;
layout(set = 2, binding = 1) uniform sampler2D uiRT;

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inShadowCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 uiColor = texture(uiRT, inTexCoord);

#ifdef UI_TEST_COLOR
    // Skip lighting in test mode so the raw UI color (e.g. magenta from the
    // test shader) reaches the readback buffer unmodified and is detectable
    // by the isMagenta() check in test_containment.cpp.
    outColor = uiColor;
#else
    float shadow  = sampleShadowPCF(inShadowCoord, shadowMap);
    vec3  lit     = clamp(ambientColor.rgb + shadow * lightColor.rgb * lightIntensity, 0.0, 1.0);

    // Teal base color; premul-alpha blend the UI texture on top.
    // uiColor.rgb is premultiplied, so: out = ui.rgb + teal * (1 - ui.a)
    vec3 teal = vec3(0.0, 0.5, 0.5);
    vec3 composited = uiColor.rgb + teal * (1.0 - uiColor.a);
    outColor = vec4(composited * lit, 1.0);
#endif
}
