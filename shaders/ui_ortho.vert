#version 450

layout(push_constant) uniform PC {
    mat4 orthoMatrix;
};

layout(location = 0) in vec2 inUIPos;
layout(location = 1) in vec2 inUITexCoord;
layout(location = 0) out vec2 outTexCoord;

void main() {
    gl_Position = orthoMatrix * vec4(inUIPos, 0.0, 1.0);
    outTexCoord = inUITexCoord;
}
