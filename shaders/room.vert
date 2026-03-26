#version 450

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 lightViewProj;
    vec4 lightDir;
    vec4 lightColor;
    vec4 ambientColor;
};

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outShadowCoord;
layout(location = 3) out vec2 outUV;

// Small bias to avoid shadow acne on the shadow coordinate.
const mat4 biasMat = mat4(
    0.5, 0.0, 0.0, 0.0,
    0.0, 0.5, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.5, 0.5, 0.0, 1.0
);

void main() {
    vec4 worldPos4 = vec4(inPos, 1.0);
    outWorldPos   = worldPos4.xyz;
    outNormal     = inNormal;
    outShadowCoord = biasMat * lightViewProj * worldPos4;
    outUV         = inUV;
    gl_Position   = proj * view * worldPos4;
}
