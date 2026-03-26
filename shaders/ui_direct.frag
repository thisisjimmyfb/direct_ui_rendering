#version 450

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 lightViewProj;
    vec4 lightDir;
    vec4 lightColor;
    vec4 ambientColor;
};

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;
layout(set = 2, binding = 0) uniform sampler2D uiAtlas;

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inShadowCoord;

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

void main() {
#ifdef UI_TEST_COLOR
    // Bypass shadow and atlas — output solid magenta for containment testing.
    outColor = vec4(1.0, 0.0, 1.0, 1.0);
#else
    vec4 atlasColor = texture(uiAtlas, inTexCoord);
    float shadow    = sampleShadowPCF(inShadowCoord);
    vec3  lit       = clamp(ambientColor.rgb + shadow * lightColor.rgb, 0.0, 1.0);
    outColor = vec4(atlasColor.rgb * lit, atlasColor.a);
#endif
}
