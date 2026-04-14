#pragma once

#include "renderer.h"
#include "scene.h"
#include "ui_system.h"
#include "ui_surface.h"
#include "metrics.h"
#include "platform.h"

#ifndef __ANDROID__
#include <GLFW/glfw3.h>
#else
#include <android_native_app_glue.h>
#endif

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <chrono>

enum class InputMode { Camera, UITerminal };

// Forward declare test helper
class AppTestHelper;

// App owns the window, renderer, scene, UI system, and metrics.
// It drives the frame loop and handles keyboard input.
class App {
    friend class AppTestHelper;

public:
    // Create window, initialise all subsystems, enter the frame loop.
    int run();

    // Set timeout in seconds (0 = no timeout)
    void setTimeout(int seconds);

#ifdef __ANDROID__
    // Called before run() on Android to supply the native app state.
    void setAndroidApp(android_app* app) { m_androidApp = app; }
#endif

private:
    bool initWindow();
    bool initSubsystems();
    void mainLoop();
    void cleanup();

#ifndef __ANDROID__
    // Input callbacks (static, forwarded to instance methods) — desktop only
    static void keyCallback(GLFWwindow* win, int key, int scancode, int action, int mods);
    static void cursorPosCallback(GLFWwindow* win, double x, double y);
    static void mouseButtonCallback(GLFWwindow* win, int button, int action, int mods);
    static void charCallback(GLFWwindow* win, unsigned int codepoint);
    void onKey(int key, int action);
    void onMouseMove(double x, double y);
    void onMouseButton(int button, int action);
    void onChar(unsigned int codepoint);
#endif

    void drawFrame();

#ifndef __ANDROID__
    GLFWwindow* m_window{nullptr};
#else
    android_app* m_androidApp{nullptr};
    ANativeWindow* m_androidWindow{nullptr};
#endif

    Renderer  m_renderer;
    Scene     m_scene;
    UISystem  m_ui;
    Metrics   m_metrics;

    RenderMode m_mode{RenderMode::Direct};
    bool       m_pendingModeToggle{false};
    float      m_depthBias{Renderer::DEPTH_BIAS_DEFAULT};
    float      m_quadW{0.5f};   // horizontal scale of the UI surface quad
    float      m_quadH{0.5f};   // vertical scale of the UI surface quad
    float      m_time{0.0f};

    // Camera state
    glm::vec3 m_camPos{0.0f, 1.5f, 4.0f};
    float     m_camYaw{-1.5707963f};   // -π/2, looking toward -Z
    float     m_camPitch{0.0f};
    double    m_lastMouseX{0.0};
    double    m_lastMouseY{0.0};
    bool      m_firstMouse{true};
    bool      m_mouseCapture{false};
    InputMode  m_inputMode{InputMode::Camera};
    std::string m_terminalText;   // up to 255 chars, shown on floating quad

    VkBuffer      m_uiTermVtxBuf{VK_NULL_HANDLE};
    VmaAllocation m_uiTermVtxAlloc{VK_NULL_HANDLE};
    uint32_t      m_uiTermVtxCount{0};
    std::chrono::steady_clock::time_point m_lastFrameTime{};

    VkCommandBuffer m_cmd{VK_NULL_HANDLE};
    VkBuffer        m_hudVtxBuf{VK_NULL_HANDLE};
    VmaAllocation   m_hudVtxAlloc{VK_NULL_HANDLE};
    std::vector<UIVertex> m_hudVerts;

    // Cube corners for per-frame update
    std::array<std::array<glm::vec3, 4>, 6> m_cubeCorners{};

    static constexpr uint32_t WINDOW_WIDTH  = 1920;
    static constexpr uint32_t WINDOW_HEIGHT = 1080;

    bool m_paused{false};

    // Timeout tracking
    int m_timeoutSeconds{0};
    std::chrono::steady_clock::time_point m_startTime;
};
