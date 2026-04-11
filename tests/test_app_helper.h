#pragma once

#include "app.h"
#include <string>

// Forward declarations
struct GLFWwindow;

/**
 * Shared test helper class to access App private members.
 * This provides a centralized interface for all tests that need to inspect
 * or modify App internal state for testing purposes.
 */
class AppTestHelper {
public:
    // ---- Callback Forwarding ----
    static void callOnKey(App& app, int key, int action) {
        app.onKey(key, action);
    }

    static void callOnChar(App& app, unsigned int codepoint) {
        app.onChar(codepoint);
    }

    static void callOnMouseMove(App& app, double x, double y) {
        app.onMouseMove(x, y);
    }

    static void callOnMouseButton(App& app, int button, int action) {
        app.onMouseButton(button, action);
    }

    // ---- Render Mode ----
    static RenderMode getRenderMode(const App& app) {
        return app.m_mode;
    }

    static void setRenderMode(App& app, RenderMode mode) {
        app.m_mode = mode;
    }

    static bool getPendingModeToggle(const App& app) {
        return app.m_pendingModeToggle;
    }

    static void setPendingModeToggle(App& app, bool toggle) {
        app.m_pendingModeToggle = toggle;
    }

    // ---- Depth Bias ----
    static float getDepthBias(const App& app) {
        return app.m_depthBias;
    }

    static void setDepthBias(App& app, float bias) {
        app.m_depthBias = bias;
    }

    // ---- Input Mode ----
    static InputMode getInputMode(const App& app) {
        return app.m_inputMode;
    }

    static void setInputMode(App& app, InputMode mode) {
        app.m_inputMode = mode;
    }

    // ---- Terminal Input ----
    static const std::string& getTerminalText(const App& app) {
        return app.m_terminalText;
    }

    static void setTerminalText(App& app, const std::string& text) {
        app.m_terminalText = text;
    }

    // ---- Quad Dimensions ----
    static float getQuadW(const App& app) {
        return app.m_quadW;
    }

    static float getQuadH(const App& app) {
        return app.m_quadH;
    }

    // ---- Mouse State ----
    static bool getMouseCapture(const App& app) {
        return app.m_mouseCapture;
    }

    static void setMouseCapture(App& app, bool capture) {
        app.m_mouseCapture = capture;
    }

    static void setMouseState(App& app, double x, double y, bool firstMouse) {
        app.m_lastMouseX = x;
        app.m_lastMouseY = y;
        app.m_firstMouse = firstMouse;
    }

    // ---- Camera ----
    static float getCamYaw(const App& app) {
        return app.m_camYaw;
    }

    static float getCamPitch(const App& app) {
        return app.m_camPitch;
    }

    // ---- Window ----
    static GLFWwindow* getWindow(const App& app) {
        return app.m_window;
    }

    static void setWindow(App& app, GLFWwindow* win) {
        app.m_window = win;
    }

    // ---- Time ----
    static float getTime(const App& app) {
        return app.m_time;
    }

    static void setTime(App& app, float time) {
        app.m_time = time;
    }

    // ---- Pause ----
    static bool getPaused(const App& app) {
        return app.m_paused;
    }

    static void setPaused(App& app, bool paused) {
        app.m_paused = paused;
    }
};
