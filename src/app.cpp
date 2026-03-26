#include "app.h"

#include <cstdio>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int App::run()
{
    if (!initWindow())      return 1;
    if (!initSubsystems())  return 1;
    mainLoop();
    cleanup();
    return 0;
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

bool App::initWindow()
{
    if (!glfwInit()) return false;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    m_window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT,
                                "Direct UI Rendering", nullptr, nullptr);
    if (!m_window) return false;

    glfwSetWindowUserPointer(m_window, this);
    glfwSetKeyCallback(m_window, keyCallback);
    return true;
}

bool App::initSubsystems()
{
    if (!m_renderer.init(/*headless=*/false)) return false;

    m_scene.init();

    // TODO: locate atlas file relative to binary, or embed as C array
    if (!m_ui.init(m_renderer.getAllocator(),
                   m_renderer.getDevice(),
                   m_renderer.getCommandPool(),
                   m_renderer.getGraphicsQueue(),
                   "assets/atlas.png")) {
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------

void App::mainLoop()
{
    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        drawFrame();
    }
}

void App::drawFrame()
{
    m_metrics.beginFrame();

    // Apply pending mode toggle at frame start.
    if (m_pendingModeToggle) {
        m_mode = (m_mode == RenderMode::Direct) ? RenderMode::Traditional : RenderMode::Direct;
        m_pendingModeToggle = false;
    }

    m_time += 0.016f;  // approximate 60 Hz; replace with real delta time

    // Compute world-space surface corners for this frame.
    glm::vec3 P_00, P_10, P_01, P_11;
    m_scene.worldCorners(m_time, P_00, P_10, P_01, P_11);

    // TODO: build view/proj from camera; for now use a fixed look-at
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.5f, 4.0f),
                                 glm::vec3(0.0f, 1.5f, 0.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(WINDOW_WIDTH) / WINDOW_HEIGHT,
                                      0.1f, 100.0f);

    // SceneUBO
    SceneUBO sceneUBO{};
    sceneUBO.view         = view;
    sceneUBO.proj         = proj;
    sceneUBO.lightViewProj = m_scene.lightViewProj();
    sceneUBO.lightDir     = glm::vec4(m_scene.light().direction, 0.0f);
    sceneUBO.lightColor   = glm::vec4(m_scene.light().color, 1.0f);
    sceneUBO.ambientColor = glm::vec4(m_scene.light().ambient, 1.0f);
    m_renderer.updateSceneUBO(sceneUBO);

    // SurfaceUBO (only used in direct mode, but always updated)
    auto transforms = computeSurfaceTransforms(P_00, P_10, P_01,
                                               W_UI, H_UI,
                                               proj * view);
    auto clipPlanes = computeClipPlanes(P_00, P_10, P_01);

    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = transforms.M_total;
    surfaceUBO.worldMatrix = transforms.M_world;
    for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
    surfaceUBO.depthBias   = m_depthBias;
    m_renderer.updateSurfaceUBO(surfaceUBO);

    // TODO: acquire swapchain image, allocate/begin command buffer,
    //       record passes, submit, present.

    m_metrics.endFrame();
    m_metrics.updateGPUMem(m_renderer.getAllocator());
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void App::keyCallback(GLFWwindow* win, int key, int scancode, int action, int mods)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(win));
    app->onKey(key, action);
}

void App::onKey(int key, int action)
{
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    if (key == GLFW_KEY_SPACE) {
        m_pendingModeToggle = true;
    } else if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) {
        m_depthBias += 0.0001f;
        printf("depthBias = %.5f\n", m_depthBias);
    } else if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
        m_depthBias -= 0.0001f;
        printf("depthBias = %.5f\n", m_depthBias);
    }
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void App::cleanup()
{
    m_ui.cleanup();
    m_renderer.cleanup();

    if (m_window) glfwDestroyWindow(m_window);
    glfwTerminate();
}
