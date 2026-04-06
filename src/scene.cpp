#include "scene.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace {

// Build one axis-aligned quad (two triangles) and append to mesh buffers.
// corners: bottom-left, bottom-right, top-right, top-left (in world space)
void addQuad(std::vector<Vertex>& verts, std::vector<uint32_t>& idxs,
             glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
             glm::vec3 normal, const Material& mat, const glm::vec3& color)
{
    uint32_t base = static_cast<uint32_t>(verts.size());
    verts.push_back({a, normal, {0.0f, 0.0f}, mat, color});
    verts.push_back({b, normal, {1.0f, 0.0f}, mat, color});
    verts.push_back({c, normal, {1.0f, 1.0f}, mat, color});
    verts.push_back({d, normal, {0.0f, 1.0f}, mat, color});

    // Two triangles: (0,1,2) and (0,2,3)
    idxs.insert(idxs.end(), {base+0, base+1, base+2, base+0, base+2, base+3});
}

// Material definitions for different room surfaces
// Demonstrates PBR with varied materials: dielectrics and metallics
const MaterialDefinition g_surfaceMaterials[] = {
    // Floor - polished concrete (dielectric, moderate roughness)
    {{0.0f, 0.7f}, "Floor"},
    // Ceiling - polished aluminum (metallic, very low roughness)
    {{0.85f, 0.2f}, "Ceiling_Aluminum"},
    // Back wall - smooth painted wall (dielectric, smooth)
    {{0.0f, 0.6f}, "BackWall"},
    // Front wall - textured wallpaper (dielectric, high roughness)
    {{0.0f, 0.85f}, "FrontWall"},
    // Left wall - brushed steel panel (metallic, moderate roughness)
    {{0.8f, 0.5f}, "LeftWall_Steel"},
    // Right wall - polished copper (metallic, low roughness)
    {{0.9f, 0.25f}, "RightWall_Copper"}  // metallic=0.9 > 0.3 threshold
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Scene::init
// ---------------------------------------------------------------------------

void Scene::init()
{
    auto& v = m_room.vertices;
    auto& i = m_room.indices;

    // Room extents: 4m wide, 3m tall, 6m deep
    constexpr float W = 2.0f;   // half-width
    constexpr float H = 3.0f;   // full height
    constexpr float D = 3.0f;   // half-depth

    // Floor   (Y = 0, normal up) - polished concrete (dielectric, moderate roughness)
    Material floorMat{0.0f, 0.7f};
    glm::vec3 floorColor(0.6f, 0.6f, 0.65f);
    addQuad(v, i,
        {-W, 0,  D}, { W, 0,  D}, { W, 0, -D}, {-W, 0, -D},
        {0, 1, 0}, floorMat, floorColor);

    // Ceiling (Y = H, normal down) - polished aluminum (metallic, very low roughness)
    Material ceilingMat{0.85f, 0.2f};
    glm::vec3 ceilingColor(0.85f, 0.82f, 0.75f);  // Similar to original drywall
    addQuad(v, i,
        {-W, H, -D}, { W, H, -D}, { W, H,  D}, {-W, H,  D},
        {0, -1, 0}, ceilingMat, ceilingColor);

    // Back wall  (Z = -D, normal forward) - smooth painted wall (dielectric, smooth)
    Material backWallMat{0.0f, 0.6f};
    glm::vec3 backWallColor(0.45f, 0.75f, 0.9f);
    addQuad(v, i,
        {-W, 0, -D}, { W, 0, -D}, { W, H, -D}, {-W, H, -D},
        {0, 0, 1}, backWallMat, backWallColor);

    // Front wall (Z = +D, normal backward) - textured wallpaper (dielectric, high roughness)
    Material frontWallMat{0.0f, 0.85f};
    glm::vec3 frontWallColor(1.0f, 0.65f, 0.45f);
    addQuad(v, i,
        { W, 0,  D}, {-W, 0,  D}, {-W, H,  D}, { W, H,  D},
        {0, 0, -1}, frontWallMat, frontWallColor);

    // Left wall  (X = -W, normal right) - brushed steel panel (metallic, moderate roughness)
    Material leftWallMat{0.8f, 0.5f};
    glm::vec3 leftWallColor(0.5f, 0.8f, 0.55f);  // Similar to original green
    addQuad(v, i,
        {-W, 0,  D}, {-W, 0, -D}, {-W, H, -D}, {-W, H,  D},
        {1, 0, 0}, leftWallMat, leftWallColor);

    // Right wall (X = +W, normal left) - polished copper (metallic, low roughness)
    Material rightWallMat{0.9f, 0.25f};
    glm::vec3 rightWallColor(0.75f, 0.6f, 0.8f);  // Similar to original purple
    addQuad(v, i,
        { W, 0, -D}, { W, 0,  D}, { W, H,  D}, { W, H, -D},
        {-1, 0, 0}, rightWallMat, rightWallColor);
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------

glm::mat4 Scene::animationMatrix(float t) const
{
    // Enhanced dynamic animation with multiple oscillation frequencies

    // Lateral oscillation (left-right motion along X-axis with varied frequency)
    float lateralX = 1.2f * std::sin(t * 0.18f) + 0.4f * std::sin(t * 0.31f);

    // Vertical oscillation (up-down motion with varied frequency)
    float lateralY = 1.5f + 0.35f * std::sin(t * 0.22f) + 0.3f * std::cos(t * 0.37f);

    // Z position varies slightly for depth variation
    float lateralZ = -2.5f + 0.3f * std::sin(t * 0.15f);

    // Rotation around Y axis (yaw) - more pronounced
    float yaw = glm::radians(25.0f) * std::sin(t * 0.25f);

    // Rotation around Z axis (roll) - more pronounced
    float roll = glm::radians(35.0f) * std::sin(t * 0.5f);

    // Rotation around X axis (pitch) - new dynamic element
    float pitch = glm::radians(18.0f) * std::sin(t * 0.19f);

    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(lateralX, lateralY, lateralZ));
    m = glm::rotate(m, yaw, glm::vec3(0, 1, 0));
    m = glm::rotate(m, roll, glm::vec3(0, 0, 1));
    m = glm::rotate(m, pitch, glm::vec3(1, 0, 0));
    return m;
}

void Scene::worldCorners(float t,
                         glm::vec3& P_00, glm::vec3& P_10,
                         glm::vec3& P_01, glm::vec3& P_11,
                         float scaleW, float scaleH) const
{
    glm::mat4 M = animationMatrix(t);
    auto applyScale = [scaleW, scaleH](const glm::vec3& p) {
        return glm::vec3(p.x * scaleW, p.y * scaleH, p.z);
    };
    // For backward compatibility, use the +Z face corners as the "quad"
    const auto& face = m_uiSurface.faces[UISurface::FRONT_FACE_INDEX];  // +Z face (front)
    P_00 = glm::vec3(M * glm::vec4(applyScale(face.P_00_local), 1.0f));
    P_10 = glm::vec3(M * glm::vec4(applyScale(face.P_10_local), 1.0f));
    P_01 = glm::vec3(M * glm::vec4(applyScale(face.P_01_local), 1.0f));
    P_11 = glm::vec3(M * glm::vec4(applyScale(face.P_11_local), 1.0f));
}

void Scene::worldCubeCorners(float t, std::array<std::array<glm::vec3, 4>, 6>& outCorners,
                             float scaleW, float scaleH) const
{
    glm::mat4 M = animationMatrix(t);
    auto applyScale = [scaleW, scaleH](const glm::vec3& p) {
        return glm::vec3(p.x * scaleW, p.y * scaleH, p.z);
    };
    for (int face = 0; face < 6; ++face) {
        const auto& f = m_uiSurface.faces[face];
        outCorners[face][0] = glm::vec3(M * glm::vec4(applyScale(f.P_00_local), 1.0f)); // P_00
        outCorners[face][1] = glm::vec3(M * glm::vec4(applyScale(f.P_10_local), 1.0f)); // P_10
        outCorners[face][2] = glm::vec3(M * glm::vec4(applyScale(f.P_01_local), 1.0f)); // P_01
        outCorners[face][3] = glm::vec3(M * glm::vec4(applyScale(f.P_11_local), 1.0f)); // P_11
    }
}

glm::vec3 Scene::faceCorner(int faceIndex, int cornerIndex) const
{
    const auto& f = m_uiSurface.faces[faceIndex];
    switch (cornerIndex) {
        case 0: return f.P_00_local;
        case 1: return f.P_10_local;
        case 2: return f.P_01_local;
        case 3: return f.P_11_local;
        default: return glm::vec3(0.0f);
    }
}

// ---------------------------------------------------------------------------
// Light
// ---------------------------------------------------------------------------

glm::vec3 Scene::spotlightPosition(float t) const
{
    // Animate spotlight in a circular arc around the center of the room
    // This creates a dramatic effect where light moves dynamically

    // Circular motion in the horizontal (X-Z) plane at height Y
    float radius = 1.5f;
    float angle = t * 0.3f;  // Slow rotation: one complete circle every ~21 seconds
    float circularX = radius * std::cos(angle);
    float circularZ = radius * std::sin(angle);

    // Base position with circular motion superimposed
    glm::vec3 basePos(0.0f, 2.8f, 0.5f);  // Original position
    glm::vec3 animatedPos = basePos + glm::vec3(circularX, 0.0f, circularZ);

    // Add subtle vertical bobbing for more dynamic effect (max 0.2 to keep within ceiling at Y=3.0)
    float verticalBob = 0.2f * std::sin(t * 0.4f);
    animatedPos.y += verticalBob;

    return animatedPos;
}

glm::mat4 Scene::lightViewProj(float t) const
{
    // Perspective shadow map from the animated spotlight position.
    // The spotlight moves in a circular arc while maintaining direction toward the scene.
    glm::vec3 lightPos = spotlightPosition(t);

    glm::mat4 view = glm::lookAt(
        lightPos,
        lightPos + m_light.direction,
        glm::vec3(0.0f, 1.0f, 0.0f));

    // FOV covers the full outer cone (outerConeAngle is the half-angle).
    float fov   = m_light.outerConeAngle * 2.0f;
    float nearZ = 0.1f;
    float farZ  = 10.0f;
    glm::mat4 proj = glm::perspective(fov, 1.0f, nearZ, farZ);
    return proj * view;
}

glm::vec3 Scene::spotlightColor(float t) const
{
    // Dynamic spotlight color cycling between warm (sunset) and cool (moonlight) tones
    // Creates atmospheric color variations without changing light intensity too drastically

    glm::vec3 baseColor = m_light.color;  // Base warm white (1.0, 0.95, 0.85)

    // Warm tone intensity: increases red/yellow periodically (sunset effect)
    float warmCycle = 0.15f * std::sin(t * 0.33f);  // Slow warm-cool cycle

    // Cool tone intensity: modulates blue channel for cool/moonlight effect
    float coolCycle = -0.12f * std::sin(t * 0.28f + 1.0f);  // Offset phase

    // Red channel: enhance warmth when in warm phase
    glm::vec3 color = baseColor;
    color.x += warmCycle * 0.3f;  // Boost red in warm phase
    color.y += warmCycle * 0.15f; // Slight green boost
    color.z += coolCycle * 0.25f; // Modulate blue for cool tones

    // Ensure color values stay in valid range
    return glm::clamp(color, 0.3f, 1.0f);  // Keep some minimum brightness
}

const MaterialDefinition* Scene::surfaceMaterials()
{
    return g_surfaceMaterials;
}

int Scene::surfaceMaterialCount()
{
    return sizeof(g_surfaceMaterials) / sizeof(g_surfaceMaterials[0]);
}
