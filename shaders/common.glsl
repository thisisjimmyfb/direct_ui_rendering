// Common GLSL functions shared across multiple shaders
// Include this file in other shaders using: #include "common.glsl"

// Spotlight cone attenuation: smooth transition from outer to inner cone angle.
// Returns 1.0 inside the inner cone, 0.0 outside the outer cone.
// L: normalised light vector (from surface to light), lightDir4.xyz: spotlight axis,
// lightDir4.w: cos(outerConeAngle), lightColor4.w: cos(innerConeAngle).
float spotlightFactor(vec3 L, vec4 lightDir4, vec4 lightColor4) {
    float cosAngle = dot(-L, normalize(lightDir4.xyz));
    return smoothstep(lightDir4.w, lightColor4.w, cosAngle);
}

// 2x2 PCF shadow sampling with slope-scaled depth bias.
// Parameters:
//   shadowCoord: shadow coordinates from vertex shader
//   N: surface normal (for bias calculation)
//   L: light direction (for bias calculation)
//   shadowMap: sampler2DShadow for shadow lookups
//
// The depth bias prevents shadow acne (self-shadowing artifacts) on surfaces that are
// nearly perpendicular to the light direction. Slope-scaled bias adapts to surface angle:
// - Surfaces facing the light (N·L ≈ 1) get minimal bias (0.002)
// - Surfaces perpendicular to light (N·L ≈ 0) get maximum bias (0.008)
// This prevents acne on walls while minimizing false shadows on lit surfaces.
float sampleShadowPCF(vec4 shadowCoord, vec3 N, vec3 L, sampler2DShadow shadowMap) {
    vec3 proj = shadowCoord.xyz / shadowCoord.w;
    // Slope-scaled bias: stronger scaling to prevent acne on steep surfaces
    float cosTheta = dot(N, L);
    float bias = max(0.008 * (1.0 - cosTheta), 0.002);
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

// 2x2 PCF shadow sampling with fixed depth bias (for degenerate UI mesh).
// Parameters:
//   shadowCoord: shadow coordinates from vertex shader
//   shadowMap: sampler2DShadow for shadow lookups
float sampleShadowPCF(vec4 shadowCoord, sampler2DShadow shadowMap) {
    vec3 proj = shadowCoord.xyz / shadowCoord.w;
    proj.z -= 0.002;  // Fixed bias for flat UI geometry
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

// Procedural hash function for noise generation (Weyl sequence-based)
float hash(vec3 p) {
    return fract(sin(dot(p, vec3(12.9898, 78.233, 45.164))) * 43758.5453);
}

// Perlin-like noise using hash function
float noisePerlin(vec3 p) {
    vec3 pi = floor(p);
    vec3 pf = fract(p);

    // Smooth interpolation curve
    vec3 u = pf * pf * (3.0 - 2.0 * pf);

    // Hash the 8 corners
    float n000 = hash(pi + vec3(0.0, 0.0, 0.0));
    float n100 = hash(pi + vec3(1.0, 0.0, 0.0));
    float n010 = hash(pi + vec3(0.0, 1.0, 0.0));
    float n110 = hash(pi + vec3(1.0, 1.0, 0.0));
    float n001 = hash(pi + vec3(0.0, 0.0, 1.0));
    float n101 = hash(pi + vec3(1.0, 0.0, 1.0));
    float n011 = hash(pi + vec3(0.0, 1.0, 1.0));
    float n111 = hash(pi + vec3(1.0, 1.0, 1.0));

    // Trilinear interpolation
    float nx0 = mix(n000, n100, u.x);
    float nx1 = mix(n010, n110, u.x);
    float nxy0 = mix(nx0, nx1, u.y);

    float nx0z = mix(n001, n101, u.x);
    float nx1z = mix(n011, n111, u.x);
    float nxy1 = mix(nx0z, nx1z, u.y);

    return mix(nxy0, nxy1, u.z);
}
