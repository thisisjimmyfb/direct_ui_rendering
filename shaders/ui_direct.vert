#version 450

#include "ubo_structs.glsl"

// Maps NDC [-1,1] to UV [0,1] for shadow map sampling (matches quad.vert).
const mat4 biasMat = mat4(
    0.5, 0.0, 0.0, 0.0,
    0.0, 0.5, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.5, 0.5, 0.0, 1.0
);

layout(location = 0) in vec2 inUIPos;
layout(location = 1) in vec2 inUITexCoord;
layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec4 outShadowCoord;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) out vec3 outNormal;

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
    outWorldPos = worldPos.xyz;
    outNormal   = surfaceNormal.xyz;  // World-space outward normal from SurfaceUBO
    outShadowCoord = biasMat * lightViewProj * worldPos;
}
