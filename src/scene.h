#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <cstdint>

// Vertex layout for room geometry (position, normal, UV).
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

// Hardcoded room mesh — floor, ceiling, 4 walls compiled into the binary.
struct RoomMesh {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
};

// UI surface quad in local space.  Corner naming:
//   P_00 = top-left, P_10 = top-right, P_01 = bottom-left, P_11 = bottom-right
struct UISurface {
    glm::vec3 P_00_local{-2.0f,  1.0f, 0.0f};
    glm::vec3 P_10_local{ 2.0f,  1.0f, 0.0f};
    glm::vec3 P_01_local{-2.0f, -1.0f, 0.0f};
    glm::vec3 P_11_local{ 2.0f, -1.0f, 0.0f};
};

// Spotlight parameters.
struct SpotLight {
    glm::vec3 position{0.0f, 2.8f, 0.5f};
    glm::vec3 direction{glm::normalize(glm::vec3(0.0f, -1.3f, -3.5f))};
    glm::vec3 color{1.0f, 0.95f, 0.85f};
    glm::vec3 ambient{0.08f, 0.08f, 0.12f};
    float innerConeAngle{glm::radians(35.0f)};
    float outerConeAngle{glm::radians(50.0f)};
};

// Scene owns room mesh data, UI surface corner definitions, and light setup.
class Scene {
public:
    // Build room mesh and configure defaults.
    void init();

    // Compute animation matrix for the UI surface at time t (seconds).
    glm::mat4 animationMatrix(float t) const;

    // Transform local UI surface corners to world space using M_anim(t).
    // scaleW scales the horizontal (X) extent, scaleH scales the vertical (Y)
    // extent, both around the local origin (center of the quad).  Pass equal
    // values for uniform scaling; pass different values to change the aspect
    // ratio without retessellating text content.
    void worldCorners(float t,
                      glm::vec3& P_00, glm::vec3& P_10,
                      glm::vec3& P_01, glm::vec3& P_11,
                      float scaleW = 1.0f, float scaleH = 1.0f) const;

    // Compute the light's view-projection matrix for shadow mapping.
    glm::mat4 lightViewProj() const;

    const RoomMesh&       roomMesh()  const { return m_room; }
    const UISurface&      uiSurface() const { return m_uiSurface; }
    const SpotLight& light()   const { return m_light; }

private:
    RoomMesh        m_room;
    UISurface       m_uiSurface;
    SpotLight m_light;
};
