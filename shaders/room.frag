#version 450

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 lightViewProj;
    vec4 lightPos;       // xyz = spotlight world position
    vec4 lightDir;       // xyz = spotlight direction, w = cos(outerConeAngle)
    vec4 lightColor;     // rgb = color, w = cos(innerConeAngle)
    vec4 ambientColor;
};

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inShadowCoord;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec4 outColor;

// 2x2 PCF with slope-scaled depth bias.
float sampleShadowPCF(vec4 shadowCoord, vec3 N, vec3 L) {
    vec3 proj = shadowCoord.xyz / shadowCoord.w;
    float bias = max(0.005 * (1.0 - dot(N, L)), 0.001);
    proj.z -= bias;
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0 / 1024.0);
    for (float x = -0.5; x <= 0.5; x += 1.0) {
        for (float y = -0.5; y <= 0.5; y += 1.0) {
            shadow += texture(shadowMap,
                vec3(proj.xy + vec2(x, y) * texelSize, proj.z));
        }
    }
    return shadow * 0.25;
}

void main() {
    vec3 N = normalize(inNormal);

    // Per-fragment light vector for the spotlight.
    vec3 toLight = lightPos.xyz - inWorldPos;
    vec3 L       = normalize(toLight);

    // Spotlight cone attenuation: smoothstep from outer to inner cone angle.
    float cosAngle   = dot(-L, normalize(lightDir.xyz));
    float outerCos   = lightDir.w;   // cos(outerConeAngle)
    float innerCos   = lightColor.w; // cos(innerConeAngle)
    float spotFactor = smoothstep(outerCos, innerCos, cosAngle);

    // Blinn-Phong diffuse
    float diff   = max(dot(N, L), 0.0);
    float shadow = sampleShadowPCF(inShadowCoord, N, L);

    vec3 ambient = ambientColor.rgb;
    vec3 diffuse = diff * shadow * spotFactor * lightColor.rgb;

    // Base grey room surface color
    vec3 surfaceColor = vec3(0.75);

    // Floor grid pattern (Y ≈ 0, the floor level)
    // Grid helps visualize depth and perspective in the scene
    if (abs(inWorldPos.y) < 0.05) {  // Check if on floor
        float gridSize = 0.5;  // Grid cell size in world units
        vec2 gridCoord = floor(inWorldPos.xz / gridSize);
        float gridPattern = mod(gridCoord.x + gridCoord.y, 2.0);

        // Mix between two colors based on grid pattern
        vec3 gridColor1 = vec3(0.75);  // Light grey
        vec3 gridColor2 = vec3(0.65);  // Slightly darker grey
        surfaceColor = mix(gridColor1, gridColor2, gridPattern);
    }

    outColor = vec4((ambient + diffuse) * surfaceColor, 1.0);
}
