#version 450

#include "common.glsl"

#define PI 3.14159265359

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

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inShadowCoord;
layout(location = 3) in vec2 inUV;
layout(location = 4) in vec2 inMaterial;  // metallic (x), roughness (y)
layout(location = 5) in vec3 inColor;     // surface color

layout(location = 0) out vec4 outColor;

// -----------------------------------------------------------------------------
// PBR Helper Functions
// -----------------------------------------------------------------------------

// Smith geometry function (Schlick-GGX)
float smithGGXCorrelation(float NdotV, float roughness) {
    float a2 = roughness * roughness;
    float k = (a2 + 1.0) / 2.0;
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return nom / denom;
}

// Distribution function (GGX/Trowbridge-Reitz)
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a2 = roughness * roughness;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Fresnel-Schlick with energy conservation
vec3 fresnelSchlick2(float cosTheta, vec3 F0) {
    return F0 * (1.0 - cosTheta) + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}


// Generate roughness variation based on world position (simulating a roughness map)
float getRoughnessVariation(vec3 worldPos, float baseRoughness) {
    // Multi-octave Perlin-like noise for natural variation
    float variation = 0.0;
    float amplitude = 0.3;  // First octave strength
    float frequency = 1.0;
    float maxAmplitude = 0.0;

    // 3 octaves of noise for natural looking variation
    for (int i = 0; i < 3; i++) {
        variation += noisePerlin(worldPos * frequency * 0.5) * amplitude;
        maxAmplitude += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    // Normalize and scale to roughness range
    variation /= maxAmplitude;

    // Add variation: -0.2 to +0.2 range
    float variationAmount = (variation - 0.5) * 0.4;

    // Clamp final roughness to valid range [0, 1]
    return clamp(baseRoughness + variationAmount, 0.0, 1.0);
}

// Simulate subtle normal map effects by perturbing the surface normal
// This creates the appearance of microscopic depth variance
vec3 perturbNormal(vec3 N, vec3 worldPos, float strength) {
    // Generate subtle perturbation vectors from noise
    float noiseX = noisePerlin(worldPos * 2.0 + vec3(10.0, 0.0, 0.0));
    float noiseY = noisePerlin(worldPos * 2.0 + vec3(0.0, 10.0, 0.0));
    float noiseZ = noisePerlin(worldPos * 2.0 + vec3(0.0, 0.0, 10.0));

    // Create small perturbation vectors
    vec3 perturbation = normalize(vec3(
        (noiseX - 0.5) * 2.0,
        (noiseY - 0.5) * 2.0,
        (noiseZ - 0.5) * 2.0
    )) * strength;

    // Blend original normal with perturbation
    vec3 perturbedN = normalize(N + perturbation);
    return mix(N, perturbedN, strength);
}

void main() {
    vec3 N = normalize(inNormal);

    // Floor and ceiling remain static (no ripple normal perturbation)
    // Apply subtle normal perturbation to simulate normal map effects only
    // Strength varies with roughness: rougher surfaces get more pronounced normal variation
    float normalPerturbationStrength = 0.08 * inMaterial.y;  // Base perturbation scaled by roughness
    N = perturbNormal(N, inWorldPos, normalPerturbationStrength);

    // Get material properties from vertex shader
    float metallic = inMaterial.x;
    float baseRoughness = inMaterial.y;

    // Apply roughness variation based on world position (normal map simulation)
    float roughness = getRoughnessVariation(inWorldPos, baseRoughness);

    // Per-fragment light vector for the spotlight.
    vec3 toLight = lightPos.xyz - inWorldPos;
    vec3 L = normalize(toLight);

    // View direction (camera at origin for simplicity)
    vec3 V = normalize(-inWorldPos);

    // Half vector for BRDF
    vec3 H = normalize(L + V);

    // Spotlight cone attenuation: smoothstep from outer to inner cone angle.
    float cosAngle = dot(-L, normalize(lightDir.xyz));
    float outerCos = lightDir.w;   // cos(outerConeAngle)
    float innerCos = lightColor.w; // cos(innerConeAngle)
    float spotFactor = smoothstep(outerCos, innerCos, cosAngle);

    // Shadow sampling
    float shadow = sampleShadowPCF(inShadowCoord, N, L, shadowMap);

    // PBR Fresnel term: F0 is base reflectivity at normal incidence
    // Dielectrics (non-metals): F0 = 0.04
    // Metals: F0 = albedo color (from surface color)
    vec3 F0 = mix(vec3(0.04), inColor.rgb, metallic);

    // Fresnel-Schlick approximation
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    // PBR Diffuse term (Lambertian with energy conservation)
    vec3 kD = vec3(1.0) - F;  // Energy conservation: kD = 1 - F
    kD *= 1.0 - metallic;     // Metals have no diffuse component
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = kD * inColor.rgb * NdotL * shadow * spotFactor * lightIntensity;

    // PBR Specular term (Cook-Torrance BRDF)
    // 1. Distribution (GGX/Trowbridge-Reitz)
    float NdotH = max(dot(N, H), 0.0);
    float roughnessSq = roughness * roughness;
    float a2 = roughnessSq * roughnessSq;
    float nom = roughnessSq;
    float denom = (NdotH * (roughnessSq - 1.0) + 1.0);
    denom = PI * denom * denom;
    float D = nom / denom;

    // 2. Geometry (Smith GGX)
    float NdotV = max(dot(N, V), 0.0);
    float NdotL2 = NdotL;
    float V2 = smithGGXCorrelation(NdotV, roughness);
    float L2 = smithGGXCorrelation(NdotL2, roughness);
    float G = V2 * L2;

    // 3. Combine into Cook-Torrance BRDF
    // Fresnel term F is already computed above
    // Specular = (D * G * F) / (4 * NdotV * NdotL)
    vec3 specular = (D * G * F) * shadow * spotFactor;

    // Ambient term (environmental lighting approximation)
    vec3 ambient = ambientColor.rgb * inColor.rgb * (1.0 - metallic * 0.9);

    // Combine all components: ambient is always visible (not multiplied by spotFactor),
    // while diffuse and specular are only visible where the spotlight shines.
    // This ensures shadows are illuminated by ambient light, not pitch black.
    vec3 result = ambient + (diffuse + specular) * spotFactor;

    outColor = vec4(result, 1.0);
}
