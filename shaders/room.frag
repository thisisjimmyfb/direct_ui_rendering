#version 450

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 lightViewProj;
    vec4 lightPos;         // xyz = spotlight world position
    vec4 lightDir;         // xyz = spotlight direction, w = cos(outerConeAngle)
    vec4 lightColor;       // rgb = color, w = cos(innerConeAngle)
    vec4 ambientColor;
    float lightIntensity;  // time-based pulsing intensity multiplier
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
    vec3 diffuse = diff * shadow * spotFactor * lightColor.rgb * lightIntensity;

    // Determine surface color and material properties based on wall/surface
    vec3 surfaceColor = vec3(0.75);  // Default grey
    float shininess = 8.0;           // Default shininess
    vec3 specularColor = vec3(1.0);  // Default specular highlight color

    // Floor grid pattern (Y ≈ 0, the floor level)
    if (abs(inWorldPos.y) < 0.05) {
        float gridSize = 0.5;  // Grid cell size in world units
        vec2 gridCoord = floor(inWorldPos.xz / gridSize);
        float gridPattern = mod(gridCoord.x + gridCoord.y, 2.0);

        // Mix between two colors based on grid pattern
        vec3 gridColor1 = vec3(0.75);  // Light grey
        vec3 gridColor2 = vec3(0.65);  // Slightly darker grey
        surfaceColor = mix(gridColor1, gridColor2, gridPattern);

        // Add very subtle grout lines for floor tiles
        vec2 gridFrac = fract(inWorldPos.xz / gridSize);
        float groutWidth = 0.05;
        if (gridFrac.x < groutWidth || gridFrac.x > (1.0 - groutWidth) ||
            gridFrac.y < groutWidth || gridFrac.y > (1.0 - groutWidth)) {
            surfaceColor *= 0.95;  // Very subtle darkening
        }

        shininess = 4.0;  // Matte floor
        specularColor = vec3(0.5);
    }
    // Ceiling (Y > 2.5)
    else if (inWorldPos.y > 2.5) {
        surfaceColor = vec3(0.85, 0.82, 0.75);  // Warm beige/tan

        // Add very subtle ceiling panels
        float panelSize = 1.0;
        vec2 panelCoord = floor((inWorldPos.xz + vec2(2.0)) / panelSize);
        float panelPattern = mod(panelCoord.x + panelCoord.y, 2.0);
        surfaceColor = mix(surfaceColor, surfaceColor * 0.98, panelPattern * 0.15);

        shininess = 6.0;
        specularColor = vec3(0.6);
    }
    // Back wall (Z ≤ -2.8)
    else if (inWorldPos.z < -2.8) {
        surfaceColor = vec3(0.45, 0.75, 0.9);  // Cool cyan/blue

        // Add very subtle vertical wall panels
        float panelHeight = 0.8;
        float panelWidth = 0.6;
        vec2 panelCoord = floor(inWorldPos.xy / vec2(panelWidth, panelHeight));
        float panelPattern = mod(panelCoord.x + panelCoord.y, 2.0);
        surfaceColor = mix(surfaceColor, surfaceColor * 0.98, panelPattern * 0.12);

        shininess = 12.0;  // Slightly glossy
        specularColor = vec3(0.8, 0.9, 1.0);  // Cyan-tinted specular
    }
    // Front wall (Z ≥ 2.8)
    else if (inWorldPos.z > 2.8) {
        surfaceColor = vec3(1.0, 0.65, 0.45);  // Warm coral/orange

        // Add very subtle horizontal wall stripes
        float stripeHeight = 0.4;
        float stripe = mod(inWorldPos.y, stripeHeight * 2.0);
        if (stripe > stripeHeight) {
            surfaceColor *= 0.97;
        }

        shininess = 10.0;
        specularColor = vec3(1.0, 0.8, 0.7);  // Warm specular
    }
    // Left wall (X ≤ -1.8)
    else if (inWorldPos.x < -1.8) {
        surfaceColor = vec3(0.5, 0.8, 0.55);  // Soft green

        // Add very subtle diagonal pattern
        float diagonalFreq = 3.0;
        float diagonal = sin((inWorldPos.y + inWorldPos.z) * diagonalFreq) * 0.5 + 0.5;
        surfaceColor = mix(surfaceColor, surfaceColor * 0.98, diagonal * 0.12);

        shininess = 8.0;
        specularColor = vec3(0.7, 0.9, 0.7);  // Green-tinted specular
    }
    // Right wall (X ≥ 1.8)
    else if (inWorldPos.x > 1.8) {
        surfaceColor = vec3(0.75, 0.6, 0.8);  // Soft purple

        // Add very subtle wave pattern
        float waveFreq = 4.0;
        float wave = sin(inWorldPos.y * waveFreq + inWorldPos.z * 1.5) * 0.5 + 0.5;
        surfaceColor = mix(surfaceColor, surfaceColor * 0.98, wave * 0.14);

        shininess = 14.0;  // More glossy purple
        specularColor = vec3(0.9, 0.8, 1.0);  // Purple-tinted specular
    }

    // Blinn-Phong specular highlights (subtle)
    vec3 V = normalize(-inWorldPos);  // View direction (approximate, camera at origin)
    vec3 H = normalize(L + V);        // Half vector
    float spec = pow(max(dot(N, H), 0.0), shininess);
    vec3 specular = spec * shadow * spotFactor * specularColor * lightColor.rgb * lightIntensity * 0.15;  // Reduce intensity

    outColor = vec4((ambient + diffuse) * surfaceColor + specular, 1.0);
}
