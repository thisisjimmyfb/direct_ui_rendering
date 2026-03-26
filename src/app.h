#pragma once

#include "renderer.h"
#include "scene.h"
#include "ui_system.h"
#include "ui_surface.h"
#include "metrics.h"

#include <GLFW/glfw3.h>
#include <string>
#include <vector>

// App owns the window, renderer, scene, UI system, and metrics.
// It drives the frame loop and handles keyboard input.
class App {
public:
    // Create window, initialise all subsystems, enter the frame loop.
    int run();

private:
    bool initWindow();
    bool initSubsystems();
    void mainLoop();
    void cleanup();

    // Input callbacks (static, forwarded to instance methods)
    static void keyCallback(GLFWwindow* win, int key, int scancode, int action, int mods);
    void onKey(int key, int action);

    void drawFrame();

    GLFWwindow* m_window{nullptr};

    Renderer  m_renderer;
    Scene     m_scene;
    UISystem  m_ui;
    Metrics   m_metrics;

    RenderMode m_mode{RenderMode::Direct};
    bool       m_pendingModeToggle{false};
    float      m_depthBias{Renderer::DEPTH_BIAS_DEFAULT};
    float      m_time{0.0f};

    VkCommandBuffer m_cmd{VK_NULL_HANDLE};
    VkBuffer        m_hudVtxBuf{VK_NULL_HANDLE};
    VmaAllocation   m_hudVtxAlloc{VK_NULL_HANDLE};
    std::vector<UIVertex> m_hudVerts;

    static constexpr uint32_t WINDOW_WIDTH  = 1280;
    static constexpr uint32_t WINDOW_HEIGHT = 720;
};
