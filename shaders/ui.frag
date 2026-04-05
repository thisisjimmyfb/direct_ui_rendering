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

layout(set = 2, binding = 0) uniform sampler2D uiAtlas;

// SDF threshold at push constant offset 64 (fragment stage).
// Set to 0.0 in bitmap mode; to ~0.5 in SDF mode.
layout(push_constant) uniform PC {
    layout(offset = 64) float sdfThreshold;
};

layout(location = 0) in  vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
#ifdef UI_TEST_COLOR
    outColor = vec4(1.0, 0.0, 1.0, 1.0);  // Solid magenta for render tests
#else
    // Static white color for UI text
    vec3 textColor = vec3(1.0);

    if (sdfThreshold > 0.0) {
        // SDF mode: atlas is R8_UNORM, R channel is the signed distance field.
        float dist = texture(uiAtlas, inTexCoord).r;
        float spread = 0.07;
        float alpha = smoothstep(sdfThreshold - spread, sdfThreshold + spread, dist);
        outColor = vec4(vec3(alpha), alpha);  // pre-multiplied white text
    } else {
        vec4 texColor = texture(uiAtlas, inTexCoord);
        outColor = vec4(texColor.rgb * textColor * texColor.a, texColor.a);  // pre-multiplied bitmap text with static white
    }
#endif
}
