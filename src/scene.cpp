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

    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(lateralX, 1.5f + lateralY, -2.5f));
    m = glm::rotate(m, angle, glm::vec3(0, 1, 0));
    return m;
}

void Scene::worldCorners(float t,
                         glm::vec3& P_00, glm::vec3& P_10,
                         glm::vec3& P_01, glm::vec3& P_11) const
{
    glm::mat4 M = animationMatrix(t);
    P_00 = glm::vec3(M * glm::vec4(m_uiSurface.P_00_local, 1.0f));
    P_10 = glm::vec3(M * glm::vec4(m_uiSurface.P_10_local, 1.0f));
    P_01 = glm::vec3(M * glm::vec4(m_uiSurface.P_01_local, 1.0f));
    P_11 = glm::vec3(M * glm::vec4(m_uiSurface.P_11_local, 1.0f));
}

// ---------------------------------------------------------------------------
// Light
// ---------------------------------------------------------------------------

glm::mat4 Scene::lightViewProj() const
{
    // Place a virtual "light camera" far above/behind the scene looking down.
    glm::vec3 lightPos = -m_light.direction * 10.0f;
    glm::mat4 view = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0, 1, 0));

    // Tight orthographic frustum: transform all 8 room corners into light view
    // space and derive the axis-aligned bounding box.  The room occupies
    // ±2 m (X), 0–3 m (Y), ±3 m (Z) — matching Scene::init() constants.
    constexpr float W = 2.0f, H = 3.0f, D = 3.0f;
    const glm::vec3 corners[8] = {
        {-W, 0, -D}, { W, 0, -D}, {-W, H, -D}, { W, H, -D},
        {-W, 0,  D}, { W, 0,  D}, {-W, H,  D}, { W, H,  D},
    };

    float minX =  1e9f, maxX = -1e9f;
    float minY =  1e9f, maxY = -1e9f;
    float minZ =  1e9f, maxZ = -1e9f;
    for (const auto& c : corners) {
        glm::vec4 lv = view * glm::vec4(c, 1.0f);
        if (lv.x < minX) minX = lv.x;  if (lv.x > maxX) maxX = lv.x;
        if (lv.y < minY) minY = lv.y;  if (lv.y > maxY) maxY = lv.y;
        if (lv.z < minZ) minZ = lv.z;  if (lv.z > maxZ) maxZ = lv.z;
    }

    // GLM ortho near/far are positive distances along -Z from the light camera.
    // View-space Z is negative for objects in front of the camera, so:
    //   near = -maxZ (closest corner), far = -minZ (farthest corner).
    // A small margin keeps all geometry clear of the clip planes.
    constexpr float kMargin = 0.25f;
    float nearZ = std::max(0.1f, -maxZ - kMargin);
    float farZ  = -minZ + kMargin;

    glm::mat4 proj = glm::ortho(
        minX - kMargin, maxX + kMargin,
        minY - kMargin, maxY + kMargin,
        nearZ, farZ
    );
    return proj * view;
}
