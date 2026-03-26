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
    // Gentle oscillation: rotate around Y-axis by ±20° and translate to
    // the far wall of the room.
    float angle = glm::radians(20.0f) * std::sin(t * 0.5f);

    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, glm::vec3(0.0f, 1.5f, -2.5f)); // position on far wall
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

    // Orthographic projection covering the room bounds.
    glm::mat4 proj = glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, 0.1f, 30.0f);
    return proj * view;
}
