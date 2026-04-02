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

// A single face of the UI cube with its four corners in local space.
// Corner naming: P_00 = top-left, P_10 = top-right, P_01 = bottom-left, P_11 = bottom-right
// UV mapping: (0,0) at P_00, (1,0) at P_10, (0,1) at P_01, (1,1) at P_11
// Each face is oriented so that when viewed from outside the cube, the UV coordinates
// map correctly for upright text rendering. The normal points outward from the cube center.
struct CubeFace {
    glm::vec3 P_00_local;
    glm::vec3 P_10_local;
    glm::vec3 P_01_local;
    glm::vec3 P_11_local;

    // Backward compatibility for tests that expect the old quad structure.
    // The +Z face (index 4) has the same dimensions as the original quad.
    static constexpr int FRONT_FACE_INDEX = 4;

    glm::vec3& operator[](int i) { return (&P_00_local)[i]; }
    const glm::vec3& operator[](int i) const { return (&P_00_local)[i]; }
};

// UI cube definition: 6 faces, each with its own corner positions.
// Faces: +X (right), -X (left), +Y (top), -Y (bottom), +Z (front), -Z (back)
// Each face is 4 units wide (X) x 2 units tall (Y) in its local frame, centered at origin.
// This preserves backward compatibility with the original quad dimensions.
struct UISurface {
    // Default cube centered at origin. Each face: 4m wide x 2m tall.
    // Corner ordering: P_00=top-left, P_10=top-right, P_01=bottom-left, P_11=bottom-right
    // In each face's local UV space: (0,0)=P_00, (1,0)=P_10, (0,1)=P_01, (1,1)=P_11
    // Face normals point outward from cube center.
    std::array<CubeFace, 6> faces{
        // +X face (right): normal points +X, Y is up, Z varies (top to bottom for UV)
        CubeFace{glm::vec3{ 2.0f,  1.0f, -2.0f}, glm::vec3{ 2.0f,  1.0f,  2.0f}, glm::vec3{ 2.0f, -1.0f, -2.0f}, glm::vec3{ 2.0f, -1.0f,  2.0f}},
        // -X face (left): normal points -X, Y is up, Z varies (bottom to top for UV)
        CubeFace{glm::vec3{-2.0f,  1.0f,  2.0f}, glm::vec3{-2.0f,  1.0f, -2.0f}, glm::vec3{-2.0f, -1.0f,  2.0f}, glm::vec3{-2.0f, -1.0f, -2.0f}},
        // +Y face (top): normal points +Y, X is horizontal, Z is vertical (back to front for UV)
        CubeFace{glm::vec3{-2.0f,  1.0f,  2.0f}, glm::vec3{ 2.0f,  1.0f,  2.0f}, glm::vec3{-2.0f,  1.0f, -2.0f}, glm::vec3{ 2.0f,  1.0f, -2.0f}},
        // -Y face (bottom): normal points -Y, X is horizontal, Z is vertical (front to back for UV)
        CubeFace{glm::vec3{-2.0f, -1.0f, -2.0f}, glm::vec3{ 2.0f, -1.0f, -2.0f}, glm::vec3{-2.0f, -1.0f,  2.0f}, glm::vec3{ 2.0f, -1.0f,  2.0f}},
        // +Z face (front): normal points +Z, X is horizontal, Y is vertical (top to bottom for UV)
        CubeFace{glm::vec3{-2.0f,  1.0f,  2.0f}, glm::vec3{ 2.0f,  1.0f,  2.0f}, glm::vec3{-2.0f, -1.0f,  2.0f}, glm::vec3{ 2.0f, -1.0f,  2.0f}},
        // -Z face (back): normal points -Z, X is horizontal, Y is vertical (bottom to top for UV)
        CubeFace{glm::vec3{ 2.0f, -1.0f, -2.0f}, glm::vec3{-2.0f, -1.0f, -2.0f}, glm::vec3{ 2.0f,  1.0f, -2.0f}, glm::vec3{-2.0f,  1.0f, -2.0f}}
    };

    // Backward compatibility for tests: access the +Z face (front) like the old quad structure
    static constexpr int FRONT_FACE_INDEX = 4;
    const CubeFace& frontFace() const { return faces[FRONT_FACE_INDEX]; }
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

    // Transform all 6 cube faces to world space using M_anim(t).
    // scaleW and scaleH are uniform scaling factors applied to each face.
    void worldCubeCorners(float t, std::array<std::array<glm::vec3, 4>, 6>& outCorners,
                          float scaleW = 1.0f, float scaleH = 1.0f) const;

    // Compute the light's view-projection matrix for shadow mapping.
    glm::mat4 lightViewProj() const;

   const RoomMesh&       roomMesh()  const { return m_room; }
    const UISurface&      uiSurface() const { return m_uiSurface; }
    const SpotLight& light()   const { return m_light; }
    // Get local corner for a specific cube face (for testing).
    glm::vec3 faceCorner(int faceIndex, int cornerIndex) const;

private:
    RoomMesh        m_room;
    UISurface       m_uiSurface;
    SpotLight m_light;
};
