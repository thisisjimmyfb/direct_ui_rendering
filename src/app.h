#pragma once

#include "renderer.h"
#include "scene.h"
#include "ui_system.h"
#include "ui_surface.h"
#include "metrics.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <chrono>

// App owns the window, renderer, scene, UI system, and metrics.
// It drives the frame loop and handles keyboard input.
class App {
public:
    // Create window, initialise all subsystems, enter the frame loop.
    int run();

    // Set timeout in seconds (0 = no timeout)
    void setTimeout(int seconds);

private:
    bool initWindow();
    bool initSubsystems();
    void mainLoop();
    void cleanup();

    // Input callbacks (static, forwarded to instance methods)
    static void keyCallback(GLFWwindow* win, int key, int scancode, int action, int mods);
    static void cursorPosCallback(GLFWwindow* win, double x, double y);
    static void mouseButtonCallback(GLFWwindow* win, int button, int action, int mods);
    void onKey(int key, int action);
    void onMouseMove(double x, double y);
    void onMouseButton(int button, int action);

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

    // Camera state
    glm::vec3 m_camPos{0.0f, 1.5f, 4.0f};
    float     m_camYaw{-1.5707963f};   // -π/2, looking toward -Z
    float     m_camPitch{0.0f};
    double    m_lastMouseX{0.0};
    double    m_lastMouseY{0.0};
    bool      m_firstMouse{true};
    bool      m_mouseCapture{false};
    std::chrono::steady_clock::time_point m_lastFrameTime{};

    VkCommandBuffer m_cmd{VK_NULL_HANDLE};
    VkBuffer        m_hudVtxBuf{VK_NULL_HANDLE};
    VmaAllocation   m_hudVtxAlloc{VK_NULL_HANDLE};
    std::vector<UIVertex> m_hudVerts;

    static constexpr uint32_t WINDOW_WIDTH  = 1280;
    static constexpr uint32_t WINDOW_HEIGHT = 720;

    // Timeout tracking
    int m_timeoutSeconds{0};
    std::chrono::steady_clock::time_point m_startTime;
};
