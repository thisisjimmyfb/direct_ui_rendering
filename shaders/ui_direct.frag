#version 450

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 lightViewProj;
    vec4 lightPos;
    vec4 lightDir;
    vec4 lightColor;
    vec4 ambientColor;
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

// 2x2 PCF tap — matches room.frag shadow quality.
float sampleShadowPCF(vec4 shadowCoord) {
    vec3 proj = shadowCoord.xyz / shadowCoord.w;
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0 / 1024.0);
    for (int x = 0; x <= 1; ++x) {
        for (int y = 0; y <= 1; ++y) {
            shadow += texture(shadowMap,
                vec3(proj.xy + vec2(x, y) * texelSize, proj.z));
        }
    }
    return shadow * 0.25;
}

// Compute spotlight cone attenuation for a fragment.
// inWorldPos is the fragment position in world space.
float spotlightAttenuation(vec3 inWorldPos) {
    vec3 toLight = lightPos.xyz - inWorldPos;
    vec3 L       = normalize(toLight);

    // Spotlight cone attenuation: smoothstep from outer to inner cone angle.
    float cosAngle   = dot(-L, normalize(lightDir.xyz));
    float outerCos   = lightDir.w;   // cos(outerConeAngle)
    float innerCos   = lightColor.w; // cos(innerConeAngle)
    return smoothstep(outerCos, innerCos, cosAngle);
}

void main() {
#ifdef UI_TEST_COLOR
    outColor = vec4(1.0, 0.0, 1.0, 1.0);
#else
    float shadow = sampleShadowPCF(inShadowCoord);
    float spot   = spotlightAttenuation(inWorldPos);
    vec3  lit    = clamp(ambientColor.rgb + shadow * spot * lightColor.rgb, 0.0, 1.0);

    if (sdfThreshold > 0.0) {
        float dist  = texture(uiAtlas, inTexCoord).r;
        float spread = 0.07;
        float alpha = smoothstep(sdfThreshold - spread, sdfThreshold + spread, dist);
        outColor = vec4(lit * alpha, alpha);  // pre-multiplied lit white text
    } else {
        vec4 atlasColor = texture(uiAtlas, inTexCoord);
        outColor = vec4(atlasColor.rgb * lit, atlasColor.a);
    }
#endif
}
