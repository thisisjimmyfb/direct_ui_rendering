#include "scene.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace {

// Build one axis-aligned quad (two triangles) and append to mesh buffers.
// corners: bottom-left, bottom-right, top-right, top-left (in world space)
void addQuad(std::vector<Vertex>& verts, std::vector<uint32_t>& idxs,
             glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
             glm::vec3 normal)
{
    uint32_t base = static_cast<uint32_t>(verts.size());
    verts.push_back({a, normal, {0.0f, 0.0f}});
    verts.push_back({b, normal, {1.0f, 0.0f}});
    verts.push_back({c, normal, {1.0f, 1.0f}});
    verts.push_back({d, normal, {0.0f, 1.0f}});

    // Two triangles: (0,1,2) and (0,2,3)
    idxs.insert(idxs.end(), {base+0, base+1, base+2, base+0, base+2, base+3});
}

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

    // Floor   (Y = 0, normal up)
    addQuad(v, i,
        {-W, 0,  D}, { W, 0,  D}, { W, 0, -D}, {-W, 0, -D},
        {0, 1, 0});

    // Ceiling (Y = H, normal down)
    addQuad(v, i,
        {-W, H, -D}, { W, H, -D}, { W, H,  D}, {-W, H,  D},
        {0, -1, 0});

    // Back wall  (Z = -D, normal forward)
    addQuad(v, i,
        {-W, 0, -D}, { W, 0, -D}, { W, H, -D}, {-W, H, -D},
        {0, 0, 1});

    // Front wall (Z = +D, normal backward) — optional, camera looks from here
    addQuad(v, i,
        { W, 0,  D}, {-W, 0,  D}, {-W, H,  D}, { W, H,  D},
        {0, 0, -1});

    // Left wall  (X = -W, normal right)
    addQuad(v, i,
        {-W, 0,  D}, {-W, 0, -D}, {-W, H, -D}, {-W, H,  D},
        {1, 0, 0});

    // Right wall (X = +W, normal left)
    addQuad(v, i,
        { W, 0, -D}, { W, 0,  D}, { W, H,  D}, { W, H, -D},
        {-1, 0, 0});
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------

glm::mat4 Scene::animationMatrix(float t) const
{
    // Slow oscillation with larger lateral translations so the quad
    // visibly drifts across the far wall.
    float angle   = glm::radians(15.0f) * std::sin(t * 0.25f);
    float lateralX = 1.2f * std::sin(t * 0.18f);   // ±1.2 m side-to-side
    float lateralY = 0.35f * std::sin(t * 0.22f);  // ±0.35 m up-down
    // ±25° wiggle around the surface normal (local Z).  Demonstrates that clip
    // planes derived per-frame from the world-space corners continue to track
    // the surface correctly even as the quad rotates in place.
    float normalAngle = glm::radians(25.0f) * std::sin(t * 0.5f);

    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(lateralX, 1.5f + lateralY, -2.5f));
    m = glm::rotate(m, angle, glm::vec3(0, 1, 0));
    m = glm::rotate(m, normalAngle, glm::vec3(0, 0, 1));
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

glm::mat4 Scene::lightViewProj() const
{
    // Perspective shadow map from the spotlight position.
    // The spotlight is positioned inside the room and aims toward the back wall,
    // casting a shadow of the floating UI quad onto the back wall.
    glm::mat4 view = glm::lookAt(
        m_light.position,
        m_light.position + m_light.direction,
        glm::vec3(0.0f, 1.0f, 0.0f));

    // FOV covers the full outer cone (outerConeAngle is the half-angle).
    float fov   = m_light.outerConeAngle * 2.0f;
    float nearZ = 0.1f;
    float farZ  = 10.0f;
    glm::mat4 proj = glm::perspective(fov, 1.0f, nearZ, farZ);
    return proj * view;
}
