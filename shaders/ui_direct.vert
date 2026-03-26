#version 450

layout(set = 1, binding = 0) uniform SurfaceUBO {
    mat4 totalMatrix;
    mat4 worldMatrix;
    vec4 clipPlanes[4];
    float depthBias;
};

layout(location = 0) in vec2 inUIPos;
layout(location = 1) in vec2 inUITexCoord;
layout(location = 0) out vec2 outTexCoord;

out gl_PerVertex {
    vec4  gl_Position;
    float gl_ClipDistance[4];
};

void main() {
    vec4 uiVert   = vec4(inUIPos, 0.0, 1.0);
    vec4 worldPos = worldMatrix * uiVert;

    gl_ClipDistance[0] = dot(clipPlanes[0], worldPos);
    gl_ClipDistance[1] = dot(clipPlanes[1], worldPos);
    gl_ClipDistance[2] = dot(clipPlanes[2], worldPos);
    gl_ClipDistance[3] = dot(clipPlanes[3], worldPos);

    gl_Position    = totalMatrix * uiVert;
    gl_Position.z -= depthBias * gl_Position.w;

    outTexCoord = inUITexCoord;
}
