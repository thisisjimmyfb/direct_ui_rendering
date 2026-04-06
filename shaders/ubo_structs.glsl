// Shared UBO structure definitions for all shaders
// Include this file at the beginning of any shader that needs access to UBO data.

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 lightViewProj;
    vec4 lightPos;         // xyz = spotlight world position
    vec4 lightDir;         // xyz = spotlight direction, w = cos(outerConeAngle)
    vec4 lightColor;       // rgb = color, w = cos(innerConeAngle)
    vec4 ambientColor;
    float lightIntensity;  // time-based pulsing intensity multiplier
    float uiColorPhase;    // time-based color animation phase for UI text
    float isTerminalMode;  // 1.0 if in terminal input mode, 0.0 otherwise
    float time;            // elapsed time in seconds for ripple animation
};

layout(set = 1, binding = 0) uniform SurfaceUBO {
    mat4 totalMatrix;     // M_wc * M_sw * M_us
    mat4 worldMatrix;     // M_sw * M_us, for clip distance computation
    vec4 clipPlanes[4];   // world-space clip planes
    float depthBias;
    float _pad[3];
};
