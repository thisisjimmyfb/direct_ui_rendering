#version 450

// Passthrough vertex shader for the composite surface quad.
// Transforms world-space vertex positions by the scene VP matrix and passes UVs through.

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 lightViewProj;
    vec4 lightDir;
    vec4 lightColor;
    vec4 ambientColor;
};

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 0) out vec2 outUV;

void main() {
    gl_Position = proj * view * vec4(inPos, 1.0);
    outUV = inUV;
}
