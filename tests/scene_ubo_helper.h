#pragma once

#include "renderer.h"
#include "scene.h"
#include <glm/glm.hpp>
#include <cmath>

// Build a SceneUBO with proper spotlight parameters from a Scene.
// Use this in tests instead of manually setting lightDir/lightColor/lightPos.
static SceneUBO makeSpotlightSceneUBO(const Scene& scene,
                                      const glm::mat4& view,
                                      const glm::mat4& proj)
{
    SceneUBO ubo{};
    ubo.view          = view;
    ubo.proj          = proj;
    ubo.lightViewProj = scene.lightViewProj(0.0f);
    ubo.lightPos      = glm::vec4(scene.light().position, 1.0f);
    ubo.lightDir      = glm::vec4(scene.light().direction,
                                  std::cos(scene.light().outerConeAngle));
    ubo.lightColor    = glm::vec4(scene.light().color,
                                  std::cos(scene.light().innerConeAngle));
    ubo.ambientColor  = glm::vec4(scene.light().ambient, 1.0f);
    ubo.lightIntensity = 1.0f;  // Default intensity (no pulsing in tests)
    return ubo;
}
