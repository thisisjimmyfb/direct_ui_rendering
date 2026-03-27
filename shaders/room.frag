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

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inShadowCoord;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec4 outColor;

// 2x2 PCF tap over the shadow map with slope-scaled depth bias to prevent
// shadow acne. The bias is larger for surfaces nearly perpendicular to the
// light (low N·L), matching the slope of the shadow-map depth gradient.
float sampleShadowPCF(vec4 shadowCoord, vec3 N, vec3 L) {
    vec3 proj = shadowCoord.xyz / shadowCoord.w;
    float bias = max(0.005 * (1.0 - dot(N, L)), 0.001);
    proj.z -= bias;
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
    vec3 N = normalize(inNormal);
    vec3 L = normalize(-lightDir.xyz);

    // Blinn-Phong diffuse
    float diff   = max(dot(N, L), 0.0);
    float shadow = sampleShadowPCF(inShadowCoord, N, L);

    vec3 ambient = ambientColor.rgb;
    vec3 diffuse = diff * shadow * lightColor.rgb;

    // Flat grey room surface
    vec3 surfaceColor = vec3(0.75);
    outColor = vec4((ambient + diffuse) * surfaceColor, 1.0);
}
