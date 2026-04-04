#version 450

// Fragment shader for the opaque teal UI surface quad.
// Renders the moving world-space quad as solid teal with PCF shadow.
// Used in direct mode: UI geometry is drawn on top of this quad.

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

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inShadowCoord;

layout(location = 0) out vec4 outColor;

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
    float shadow = sampleShadowPCF(inShadowCoord);
    vec3 teal = vec3(0.0, 0.5, 0.5);
    vec3 lit   = clamp(ambientColor.rgb + shadow * lightColor.rgb * lightIntensity, 0.0, 1.0);
    outColor   = vec4(teal * lit, 1.0);
}
