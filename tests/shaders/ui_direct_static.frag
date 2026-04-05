#version 450

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
};

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;

layout(set = 2, binding = 0) uniform sampler2D uiAtlas;

// SDF threshold at push constant offset 64 (fragment stage).
layout(push_constant) uniform PC {
    layout(offset = 64) float sdfThreshold;
};

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inShadowCoord;
layout(location = 2) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

// 2x2 PCF with fixed depth bias for degenerate UI mesh.
float sampleShadowPCF(vec4 shadowCoord) {
    vec3 proj = shadowCoord.xyz / shadowCoord.w;
    proj.z -= 0.002;
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0 / 1024.0);
    for (float x = -0.5; x <= 0.5; x += 1.0) {
        for (float y = -0.5; y <= 0.5; y += 1.0) {
            shadow += texture(shadowMap,
                vec3(proj.xy + vec2(x, y) * texelSize, proj.z));
        }
    }
    return shadow * 0.25;
}

void main() {
#ifdef UI_TEST_COLOR
    // Static white color for render tests (no rainbow animation)
    outColor = vec4(1.0, 1.0, 1.0, 1.0);
#else
    // Original rainbow animation - this would produce colored text
    // float hue = fract(uiColorPhase / 4.0);
    // vec3 rainbowColor = hsvToRgb(hue, 1.0, 1.0);
    // vec4 texColor = texture(uiAtlas, inTexCoord);
    // outColor = vec4(texColor.rgb * rainbowColor, texColor.a);

    // Static white - base color is always white regardless of animation phase
    vec4 texColor = texture(uiAtlas, inTexCoord);
    outColor = vec4(texColor.rgb, texColor.a);
#endif
}
