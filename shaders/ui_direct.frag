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
};

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

layout(location = 0) out vec4 outColor;

// 2x2 PCF with fixed depth bias for degenerate UI mesh.
// Matches surface.frag PCF kernel for consistent shadow quality.
float sampleShadowPCF(vec4 shadowCoord) {
    vec3 proj = shadowCoord.xyz / shadowCoord.w;
    proj.z -= 0.002;  // Fixed bias for flat UI geometry
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
    outColor = vec4(1.0, 0.0, 1.0, 1.0);  // Solid magenta for render tests
#else
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
        // Pre-multiplied alpha assumed in atlas (bitmap mode)
    }

    // Apply same lighting model as surface.frag: ambient + spotlight with shadow.
    // This ensures UI text is readable even in shadow areas (ambient contribution)
    // and receives full lighting where illuminated by the spotlight.
    float shadow = sampleShadowPCF(inShadowCoord);
    vec3 lit = clamp(ambientColor.rgb + shadow * lightColor.rgb * lightIntensity, 0.0, 1.0);
    outColor = vec4(texColor.rgb * lit, texColor.a);
#endif
}
