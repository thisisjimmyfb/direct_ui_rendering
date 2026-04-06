#version 450

#include "ubo_structs.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 4) in vec2 inMaterial;  // metallic, roughness (unused in shadow pass)
layout(location = 5) in vec3 inColor;     // surface color (unused in shadow pass)

void main() {
    gl_Position = lightViewProj * vec4(inPos, 1.0);
}
