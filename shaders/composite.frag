#version 450

layout(set = 2, binding = 1) uniform sampler2D uiRT;

layout(location = 0) in  vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(uiRT, inTexCoord);
}
