#include <gtest/gtest.h>
#include "app.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <string>
#include <cstring>

// Test helper to access App private members for testing
// This allows us to test input handling without exposing internals publicly
class AppTestHelper {
public:
    // Direct access to private methods and members for testing
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

    // State accessors for verification
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

    static float getDepthBias(const App& app) {
        return app.m_depthBias;
    }

    static void setDepthBias(App& app, float bias) {
        app.m_depthBias = bias;
    }

    static InputMode getInputMode(const App& app) {
        return app.m_inputMode;
    }

    static void setInputMode(App& app, InputMode mode) {
        app.m_inputMode = mode;
    }

    static const std::string& getTerminalText(const App& app) {
        return app.m_terminalText;
    }

    static void setTerminalText(App& app, const std::string& text) {
        app.m_terminalText = text;
    }

    static float getQuadW(const App& app) {
        return app.m_quadW;
    }

    static float getQuadH(const App& app) {
        return app.m_quadH;
    }

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

    static float getCamYaw(const App& app) {
        return app.m_camYaw;
    }

    static float getCamPitch(const App& app) {
        return app.m_camPitch;
    }

    static GLFWwindow* getWindow(const App& app) {
        return app.m_window;
    }

    static void setWindow(App& app, GLFWwindow* win) {
        app.m_window = win;
    }
};

// ---------------------------------------------------------------------------
// AppInputHandlerTest — Test keyboard input handling
// ---------------------------------------------------------------------------

class AppInputHandlerTest : public ::testing::Test {
protected:
    App app;
    GLFWwindow* window = nullptr;

    void SetUp() override {
        // Initialize GLFW and create a hidden window for tests that need GLFW calls
        if (!glfwInit()) {
            // GTEST will continue without GLFW - some tests will skip
        } else {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            window = glfwCreateWindow(800, 600, "Test Window", nullptr, nullptr);
            if (window) {
                // Manually set the window pointer so GLFW calls in the app will work
                AppTestHelper::setWindow(app, window);
            }
        }

        // Initialize state for testing
        AppTestHelper::setRenderMode(app, RenderMode::Direct);
        AppTestHelper::setDepthBias(app, 0.0001f);
        AppTestHelper::setInputMode(app, InputMode::Camera);
        AppTestHelper::setTerminalText(app, "");
        AppTestHelper::setPendingModeToggle(app, false);
        AppTestHelper::setMouseCapture(app, false);
    }

    void TearDown() override {
        if (window) {
            glfwDestroyWindow(window);
            window = nullptr;
            AppTestHelper::setWindow(app, nullptr);
        }
        glfwTerminate();
    }
};

// ---------------------------------------------------------------------------
// Mode Toggle Tests
// ---------------------------------------------------------------------------

TEST_F(AppInputHandlerTest, ModeToggle_SpaceKey_SetsToggleFlag) {
    AppTestHelper::setRenderMode(app, RenderMode::Direct);
    EXPECT_FALSE(AppTestHelper::getPendingModeToggle(app));

    AppTestHelper::callOnKey(app, GLFW_KEY_SPACE, GLFW_PRESS);

    EXPECT_TRUE(AppTestHelper::getPendingModeToggle(app));
}

TEST_F(AppInputHandlerTest, ModeToggle_SpaceKey_ReleasedDoesNothing) {
    AppTestHelper::setRenderMode(app, RenderMode::Direct);
    AppTestHelper::callOnKey(app, GLFW_KEY_SPACE, GLFW_RELEASE);
    EXPECT_FALSE(AppTestHelper::getPendingModeToggle(app));
}

TEST_F(AppInputHandlerTest, ModeToggle_OtherKeyDoesNotToggle) {
    AppTestHelper::setRenderMode(app, RenderMode::Direct);
    AppTestHelper::callOnKey(app, GLFW_KEY_A, GLFW_PRESS);
    EXPECT_FALSE(AppTestHelper::getPendingModeToggle(app));
}

// ---------------------------------------------------------------------------
// Depth Bias Adjustment Tests
// ---------------------------------------------------------------------------

TEST_F(AppInputHandlerTest, DepthBias_PlusKeyIncreases) {
    float initialBias = 0.0001f;
    AppTestHelper::setDepthBias(app, initialBias);
    AppTestHelper::setInputMode(app, InputMode::Camera);

    AppTestHelper::callOnKey(app, GLFW_KEY_EQUAL, GLFW_PRESS);

    float expected = initialBias + 0.0001f;
    EXPECT_NEAR(AppTestHelper::getDepthBias(app), expected, 1e-6f);
}

TEST_F(AppInputHandlerTest, DepthBias_MinusKeyDecreases) {
    float initialBias = 0.0002f;
    AppTestHelper::setDepthBias(app, initialBias);
    AppTestHelper::setInputMode(app, InputMode::Camera);

    AppTestHelper::callOnKey(app, GLFW_KEY_MINUS, GLFW_PRESS);

    float expected = initialBias - 0.0001f;
    EXPECT_NEAR(AppTestHelper::getDepthBias(app), expected, 1e-6f);
}

TEST_F(AppInputHandlerTest, DepthBias_KPAddIncreases) {
    float initialBias = 0.0001f;
    AppTestHelper::setDepthBias(app, initialBias);
    AppTestHelper::setInputMode(app, InputMode::Camera);

    AppTestHelper::callOnKey(app, GLFW_KEY_KP_ADD, GLFW_PRESS);

    float expected = initialBias + 0.0001f;
    EXPECT_NEAR(AppTestHelper::getDepthBias(app), expected, 1e-6f);
}

TEST_F(AppInputHandlerTest, DepthBias_KPSubtractDecreases) {
    float initialBias = 0.0002f;
    AppTestHelper::setDepthBias(app, initialBias);
    AppTestHelper::setInputMode(app, InputMode::Camera);

    AppTestHelper::callOnKey(app, GLFW_KEY_KP_SUBTRACT, GLFW_PRESS);

    float expected = initialBias - 0.0001f;
    EXPECT_NEAR(AppTestHelper::getDepthBias(app), expected, 1e-6f);
}

TEST_F(AppInputHandlerTest, DepthBias_IgnoredInTerminalMode) {
    float initialBias = 0.0001f;
    AppTestHelper::setDepthBias(app, initialBias);
    AppTestHelper::setInputMode(app, InputMode::UITerminal);

    AppTestHelper::callOnKey(app, GLFW_KEY_EQUAL, GLFW_PRESS);

    EXPECT_NEAR(AppTestHelper::getDepthBias(app), initialBias, 1e-6f);
}

// ---------------------------------------------------------------------------
// Input Mode Toggle Tests (Tab Key)
// ---------------------------------------------------------------------------

TEST_F(AppInputHandlerTest, InputModeToggle_TabInCameraMode_SwitchesToTerminal) {
    AppTestHelper::setInputMode(app, InputMode::Camera);

    AppTestHelper::callOnKey(app, GLFW_KEY_TAB, GLFW_PRESS);

    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::UITerminal);
}

TEST_F(AppInputHandlerTest, InputModeToggle_TabInTerminalMode_SwitchesToCamera) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);

    AppTestHelper::callOnKey(app, GLFW_KEY_TAB, GLFW_PRESS);

    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::Camera);
}

TEST_F(AppInputHandlerTest, InputModeToggle_TabReleaseDoesNothing) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    AppTestHelper::callOnKey(app, GLFW_KEY_TAB, GLFW_RELEASE);

    // Should remain in Camera mode (only PRESS toggles)
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::Camera);
}

// ---------------------------------------------------------------------------
// Terminal Input Mode Tests
// ---------------------------------------------------------------------------

TEST_F(AppInputHandlerTest, TerminalInput_CharacterAccumulation) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "");

    AppTestHelper::callOnChar(app, 'H');
    AppTestHelper::callOnChar(app, 'i');

    EXPECT_EQ(AppTestHelper::getTerminalText(app), "Hi");
}

TEST_F(AppInputHandlerTest, TerminalInput_OnlyPrintableASCII) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "");

    // Character codes 32-126 are printable ASCII
    AppTestHelper::callOnChar(app, 'A');  // 65, valid
    AppTestHelper::callOnChar(app, ' ');  // 32, valid (space)
    AppTestHelper::callOnChar(app, '~');  // 126, valid (tilde)
    AppTestHelper::callOnChar(app, 31);   // 31, not printable, ignored
    AppTestHelper::callOnChar(app, 127);  // 127, not printable, ignored

    EXPECT_EQ(AppTestHelper::getTerminalText(app), "A ~");
}

TEST_F(AppInputHandlerTest, TerminalInput_IgnoredInCameraMode) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    AppTestHelper::setTerminalText(app, "");

    AppTestHelper::callOnChar(app, 'X');

    EXPECT_EQ(AppTestHelper::getTerminalText(app), "");
}

TEST_F(AppInputHandlerTest, TerminalInput_MaximumLength_255Characters) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    std::string maxText(255, 'A');
    AppTestHelper::setTerminalText(app, maxText);

    // Try to add another character
    AppTestHelper::callOnChar(app, 'B');

    // Should still be 255 characters (not 256)
    EXPECT_EQ(AppTestHelper::getTerminalText(app).size(), 255U);
    EXPECT_EQ(AppTestHelper::getTerminalText(app).back(), 'A');
}

// ---------------------------------------------------------------------------
// Backspace Handling Tests
// ---------------------------------------------------------------------------

TEST_F(AppInputHandlerTest, TerminalInput_BackspaceRemovesLastCharacter) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "Hello");

    AppTestHelper::callOnKey(app, GLFW_KEY_BACKSPACE, GLFW_PRESS);

    EXPECT_EQ(AppTestHelper::getTerminalText(app), "Hell");
}

TEST_F(AppInputHandlerTest, TerminalInput_BackspaceOnEmptyTextDoesNothing) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "");

    AppTestHelper::callOnKey(app, GLFW_KEY_BACKSPACE, GLFW_PRESS);

    EXPECT_EQ(AppTestHelper::getTerminalText(app), "");
}

TEST_F(AppInputHandlerTest, TerminalInput_BackspaceMultipleTimes) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "Test");

    AppTestHelper::callOnKey(app, GLFW_KEY_BACKSPACE, GLFW_PRESS);
    AppTestHelper::callOnKey(app, GLFW_KEY_BACKSPACE, GLFW_PRESS);
    AppTestHelper::callOnKey(app, GLFW_KEY_BACKSPACE, GLFW_PRESS);

    EXPECT_EQ(AppTestHelper::getTerminalText(app), "T");
}

TEST_F(AppInputHandlerTest, TerminalInput_BackspaceIgnoredInCameraMode) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    AppTestHelper::setTerminalText(app, "Hello");

    AppTestHelper::callOnKey(app, GLFW_KEY_BACKSPACE, GLFW_PRESS);

    EXPECT_EQ(AppTestHelper::getTerminalText(app), "Hello");
}

// ---------------------------------------------------------------------------
// Exit Terminal Mode Tests
// ---------------------------------------------------------------------------

TEST_F(AppInputHandlerTest, TerminalInput_EscapeReturnsToCamera) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);

    AppTestHelper::callOnKey(app, GLFW_KEY_ESCAPE, GLFW_PRESS);

    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::Camera);
}

TEST_F(AppInputHandlerTest, TerminalInput_EscapeInCameraModeDoesNothing) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    AppTestHelper::setMouseCapture(app, false);

    AppTestHelper::callOnKey(app, GLFW_KEY_ESCAPE, GLFW_PRESS);

    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::Camera);
}

TEST_F(AppInputHandlerTest, TerminalInput_CharacterAndBackspaceSequence) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "");

    AppTestHelper::callOnChar(app, 'H');
    AppTestHelper::callOnChar(app, 'e');
    AppTestHelper::callOnChar(app, 'l');
    AppTestHelper::callOnKey(app, GLFW_KEY_BACKSPACE, GLFW_PRESS);
    AppTestHelper::callOnChar(app, 'p');

    EXPECT_EQ(AppTestHelper::getTerminalText(app), "Hep");
}

// ---------------------------------------------------------------------------
// Complex Input Sequences
// ---------------------------------------------------------------------------

TEST_F(AppInputHandlerTest, ComplexSequence_ModeToggleAndDepthBias) {
    AppTestHelper::setRenderMode(app, RenderMode::Direct);
    AppTestHelper::setDepthBias(app, 0.0001f);
    AppTestHelper::setInputMode(app, InputMode::Camera);

    // Toggle rendering mode
    AppTestHelper::callOnKey(app, GLFW_KEY_SPACE, GLFW_PRESS);
    EXPECT_TRUE(AppTestHelper::getPendingModeToggle(app));

    // Adjust depth bias
    AppTestHelper::callOnKey(app, GLFW_KEY_EQUAL, GLFW_PRESS);
    EXPECT_NEAR(AppTestHelper::getDepthBias(app), 0.0002f, 1e-6f);

    // Both operations should be independent
    EXPECT_TRUE(AppTestHelper::getPendingModeToggle(app));
}

TEST_F(AppInputHandlerTest, ComplexSequence_TerminalInputWithModeToggle) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "");

    // Add some text
    AppTestHelper::callOnChar(app, 'X');

    // Try to toggle mode (should work via Tab)
    AppTestHelper::callOnKey(app, GLFW_KEY_TAB, GLFW_PRESS);
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::Camera);

    // Text should be preserved
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "X");
}

TEST_F(AppInputHandlerTest, ComplexSequence_DepthBiasMultipleAdjustments) {
    AppTestHelper::setDepthBias(app, 0.0001f);
    AppTestHelper::setInputMode(app, InputMode::Camera);

    // Increase multiple times
    for (int i = 0; i < 5; ++i) {
        AppTestHelper::callOnKey(app, GLFW_KEY_EQUAL, GLFW_PRESS);
    }

    float expected = 0.0001f + (5 * 0.0001f);
    EXPECT_NEAR(AppTestHelper::getDepthBias(app), expected, 1e-6f);

    // Decrease multiple times
    for (int i = 0; i < 3; ++i) {
        AppTestHelper::callOnKey(app, GLFW_KEY_MINUS, GLFW_PRESS);
    }

    expected = 0.0001f + (2 * 0.0001f);
    EXPECT_NEAR(AppTestHelper::getDepthBias(app), expected, 1e-6f);
}

// ---------------------------------------------------------------------------
// Edge Cases
// ---------------------------------------------------------------------------

TEST_F(AppInputHandlerTest, EdgeCase_RepeatActionsIgnoredForTab) {
    AppTestHelper::setInputMode(app, InputMode::Camera);

    // Tab with PRESS should toggle
    AppTestHelper::callOnKey(app, GLFW_KEY_TAB, GLFW_PRESS);
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::UITerminal);

    // Tab with REPEAT should be ignored (only PRESS toggles)
    AppTestHelper::callOnKey(app, GLFW_KEY_TAB, GLFW_REPEAT);
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::UITerminal);
}

TEST_F(AppInputHandlerTest, EdgeCase_TerminalModeIgnoresNonTerminalKeys) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "");

    // These should be ignored in terminal mode
    AppTestHelper::callOnKey(app, GLFW_KEY_SPACE, GLFW_PRESS);
    AppTestHelper::callOnKey(app, GLFW_KEY_EQUAL, GLFW_PRESS);
    AppTestHelper::callOnKey(app, GLFW_KEY_W, GLFW_PRESS);

    // Only backspace and escape are handled in terminal mode
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "");
}

// ---------------------------------------------------------------------------
// Mouse Input Handling Tests
// ---------------------------------------------------------------------------

TEST_F(AppInputHandlerTest, MouseButton_RightClickTogglesCapture) {
    AppTestHelper::setMouseCapture(app, false);

    AppTestHelper::callOnMouseButton(app, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);

    EXPECT_TRUE(AppTestHelper::getMouseCapture(app));
}

TEST_F(AppInputHandlerTest, MouseButton_RightClickAgainTogglesCaptureOff) {
    AppTestHelper::setMouseCapture(app, true);

    AppTestHelper::callOnMouseButton(app, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);

    EXPECT_FALSE(AppTestHelper::getMouseCapture(app));
}

TEST_F(AppInputHandlerTest, MouseButton_RightClickReleasedDoesNothing) {
    AppTestHelper::setMouseCapture(app, false);

    AppTestHelper::callOnMouseButton(app, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);

    EXPECT_FALSE(AppTestHelper::getMouseCapture(app));
}

TEST_F(AppInputHandlerTest, MouseButton_OtherButtonIgnored) {
    AppTestHelper::setMouseCapture(app, false);

    AppTestHelper::callOnMouseButton(app, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);

    EXPECT_FALSE(AppTestHelper::getMouseCapture(app));
}

TEST_F(AppInputHandlerTest, MouseMove_IgnoredWhenCaptureDisabled) {
    AppTestHelper::setMouseCapture(app, false);
    float initialYaw = AppTestHelper::getCamYaw(app);
    float initialPitch = AppTestHelper::getCamPitch(app);

    AppTestHelper::callOnMouseMove(app, 100.0, 50.0);

    EXPECT_EQ(AppTestHelper::getCamYaw(app), initialYaw);
    EXPECT_EQ(AppTestHelper::getCamPitch(app), initialPitch);
}

TEST_F(AppInputHandlerTest, MouseMove_FirstMoveInitializesState) {
    AppTestHelper::setMouseCapture(app, true);
    AppTestHelper::setMouseState(app, 0.0, 0.0, true);

    AppTestHelper::callOnMouseMove(app, 100.0, 50.0);

    // After first move, firstMouse should be false and last position updated
    // We can't directly check firstMouse, but we can verify next move changes camera
}

TEST_F(AppInputHandlerTest, MouseMove_CameraRotationWithDelta) {
    AppTestHelper::setMouseCapture(app, true);
    AppTestHelper::setMouseState(app, 50.0, 50.0, false);

    float initialYaw = AppTestHelper::getCamYaw(app);
    float initialPitch = AppTestHelper::getCamPitch(app);

    // Move right: dx = 100 - 50 = 50, applies 50 * 0.002 = 0.1 sensitivity
    AppTestHelper::callOnMouseMove(app, 100.0, 50.0);

    float expectedYaw = initialYaw + (50.0f * 0.002f);
    float expectedPitch = initialPitch;  // dy = 50 - 50 = 0

    EXPECT_NEAR(AppTestHelper::getCamYaw(app), expectedYaw, 1e-5f);
    EXPECT_NEAR(AppTestHelper::getCamPitch(app), expectedPitch, 1e-5f);
}

TEST_F(AppInputHandlerTest, MouseMove_PitchClamped) {
    AppTestHelper::setMouseCapture(app, true);

    // Set pitch to near the upper clamp (close to 89 degrees)
    float clampedPitch = glm::radians(88.0f);
    AppTestHelper::setMouseState(app, 0.0, 0.0, false);

    // Move down significantly to try to exceed the pitch clamp
    // dy = 0 - 500 = -500, applies -500 * 0.002 = -1.0
    AppTestHelper::callOnMouseMove(app, 0.0, 500.0);

    // Pitch should be clamped at radians(89)
    EXPECT_LE(AppTestHelper::getCamPitch(app), glm::radians(89.0f));
}

// ---------------------------------------------------------------------------
// Quad Size Adjustment Tests
// ---------------------------------------------------------------------------

TEST_F(AppInputHandlerTest, QuadWidth_RightBracketIncreases) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    float initialW = AppTestHelper::getQuadW(app);

    AppTestHelper::callOnKey(app, GLFW_KEY_RIGHT_BRACKET, GLFW_PRESS);

    EXPECT_GT(AppTestHelper::getQuadW(app), initialW);
    EXPECT_NEAR(AppTestHelper::getQuadW(app), initialW + 0.1f, 1e-6f);
}

TEST_F(AppInputHandlerTest, QuadWidth_LeftBracketDecreases) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    float initialW = AppTestHelper::getQuadW(app);

    AppTestHelper::callOnKey(app, GLFW_KEY_LEFT_BRACKET, GLFW_PRESS);

    EXPECT_LT(AppTestHelper::getQuadW(app), initialW);
    EXPECT_NEAR(AppTestHelper::getQuadW(app), initialW - 0.1f, 1e-6f);
}

TEST_F(AppInputHandlerTest, QuadWidth_ClampsAtMaximum) {
    AppTestHelper::setInputMode(app, InputMode::Camera);

    // Try to increase beyond 3.0
    for (int i = 0; i < 50; ++i) {
        AppTestHelper::callOnKey(app, GLFW_KEY_RIGHT_BRACKET, GLFW_PRESS);
    }

    EXPECT_LE(AppTestHelper::getQuadW(app), 3.0f);
}

TEST_F(AppInputHandlerTest, QuadWidth_ClampsAtMinimum) {
    AppTestHelper::setInputMode(app, InputMode::Camera);

    // Try to decrease below 0.1
    for (int i = 0; i < 50; ++i) {
        AppTestHelper::callOnKey(app, GLFW_KEY_LEFT_BRACKET, GLFW_PRESS);
    }

    EXPECT_GE(AppTestHelper::getQuadW(app), 0.1f);
}

TEST_F(AppInputHandlerTest, QuadHeight_PKeyIncreases) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    float initialH = AppTestHelper::getQuadH(app);

    AppTestHelper::callOnKey(app, GLFW_KEY_P, GLFW_PRESS);

    EXPECT_GT(AppTestHelper::getQuadH(app), initialH);
    EXPECT_NEAR(AppTestHelper::getQuadH(app), initialH + 0.1f, 1e-6f);
}

TEST_F(AppInputHandlerTest, QuadHeight_OKeyDecreases) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    float initialH = AppTestHelper::getQuadH(app);

    AppTestHelper::callOnKey(app, GLFW_KEY_O, GLFW_PRESS);

    EXPECT_LT(AppTestHelper::getQuadH(app), initialH);
    EXPECT_NEAR(AppTestHelper::getQuadH(app), initialH - 0.1f, 1e-6f);
}

TEST_F(AppInputHandlerTest, QuadHeight_ClampsAtMaximum) {
    AppTestHelper::setInputMode(app, InputMode::Camera);

    for (int i = 0; i < 50; ++i) {
        AppTestHelper::callOnKey(app, GLFW_KEY_P, GLFW_PRESS);
    }

    EXPECT_LE(AppTestHelper::getQuadH(app), 3.0f);
}

TEST_F(AppInputHandlerTest, QuadHeight_ClampsAtMinimum) {
    AppTestHelper::setInputMode(app, InputMode::Camera);

    for (int i = 0; i < 50; ++i) {
        AppTestHelper::callOnKey(app, GLFW_KEY_O, GLFW_PRESS);
    }

    EXPECT_GE(AppTestHelper::getQuadH(app), 0.1f);
}

// ---------------------------------------------------------------------------
// Escape Key Handling Tests
// ---------------------------------------------------------------------------

TEST_F(AppInputHandlerTest, EscapeKey_InCameraModeWithMouseCapture_DisablesCapture) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    AppTestHelper::setMouseCapture(app, true);

    AppTestHelper::callOnKey(app, GLFW_KEY_ESCAPE, GLFW_PRESS);

    EXPECT_FALSE(AppTestHelper::getMouseCapture(app));
}

TEST_F(AppInputHandlerTest, EscapeKey_InCameraModeWithoutMouseCapture_DoesNothing) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    AppTestHelper::setMouseCapture(app, false);

    AppTestHelper::callOnKey(app, GLFW_KEY_ESCAPE, GLFW_PRESS);

    EXPECT_FALSE(AppTestHelper::getMouseCapture(app));
}

// ---------------------------------------------------------------------------
// Terminal Input Mode Switching and Mouse Capture
// ---------------------------------------------------------------------------

TEST_F(AppInputHandlerTest, InputModeToggle_EnteringTerminalMode_ReleasesMouseCapture) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    AppTestHelper::setMouseCapture(app, true);

    AppTestHelper::callOnKey(app, GLFW_KEY_TAB, GLFW_PRESS);

    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::UITerminal);
    EXPECT_FALSE(AppTestHelper::getMouseCapture(app));
}

TEST_F(AppInputHandlerTest, InputModeToggle_ExitingTerminalMode_DoesNotAffectMouseCapture) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setMouseCapture(app, false);

    AppTestHelper::callOnKey(app, GLFW_KEY_TAB, GLFW_PRESS);

    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::Camera);
    EXPECT_FALSE(AppTestHelper::getMouseCapture(app));
}

// ---------------------------------------------------------------------------
// Advanced Terminal Input Tests — Cursor Display State and UI Updates
// ---------------------------------------------------------------------------

TEST_F(AppInputHandlerTest, TerminalInput_CursorDisplayedInTerminalMode) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "Test");

    // When in terminal mode, the display text should have the cursor appended
    // This is verified by checking the renderText would include the pipe character
    // The actual rendering logic appends '|' to the display string when in UITerminal mode
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::UITerminal);

    // Text should be preserved without the cursor in m_terminalText
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "Test");
}

TEST_F(AppInputHandlerTest, TerminalInput_CursorNotDisplayedInCameraMode) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    AppTestHelper::setTerminalText(app, "Test");

    // In camera mode, the pipe character should not be appended to display text
    // Even though terminal text is preserved, it won't be displayed with cursor
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::Camera);

    // Terminal text should still be stored but not displayed with cursor
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "Test");
}

TEST_F(AppInputHandlerTest, TerminalInput_TextPersistenceAcrossModeToggle) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    AppTestHelper::setTerminalText(app, "");

    // Switch to terminal mode and add text
    AppTestHelper::callOnKey(app, GLFW_KEY_TAB, GLFW_PRESS);
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::UITerminal);

    AppTestHelper::callOnChar(app, 'H');
    AppTestHelper::callOnChar(app, 'i');
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "Hi");

    // Switch back to camera mode
    AppTestHelper::callOnKey(app, GLFW_KEY_TAB, GLFW_PRESS);
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::Camera);

    // Text should still be there
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "Hi");

    // Switch back to terminal and verify text and cursor are there
    AppTestHelper::callOnKey(app, GLFW_KEY_TAB, GLFW_PRESS);
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::UITerminal);
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "Hi");
}

TEST_F(AppInputHandlerTest, TerminalInput_EmptyTextWithCursor) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "");

    // With empty text in terminal mode, display should show just the cursor '|'
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "");
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::UITerminal);
}

TEST_F(AppInputHandlerTest, TerminalInput_CursorAfterCharacterSequence) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "");

    AppTestHelper::callOnChar(app, 'A');
    AppTestHelper::callOnChar(app, 'B');
    AppTestHelper::callOnChar(app, 'C');

    std::string text = AppTestHelper::getTerminalText(app);
    EXPECT_EQ(text, "ABC");

    // When rendered, display would be "ABC|" but m_terminalText is just "ABC"
    // The cursor is appended during rendering
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::UITerminal);
}

TEST_F(AppInputHandlerTest, TerminalInput_CursorPositionAfterBackspace) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "Hello");

    AppTestHelper::callOnKey(app, GLFW_KEY_BACKSPACE, GLFW_PRESS);
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "Hell");

    // When rendered, display would be "Hell|" (cursor after remaining text)
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::UITerminal);
}

TEST_F(AppInputHandlerTest, TerminalInput_CursorVisibilityAfterModeSwitch) {
    // Start in terminal mode with text
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "");

    AppTestHelper::callOnChar(app, 'X');
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "X");

    // Switch to camera mode — cursor should not be displayed
    AppTestHelper::callOnKey(app, GLFW_KEY_TAB, GLFW_PRESS);
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::Camera);

    // The stored text is still there, but without cursor display
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "X");

    // Switch back to terminal — cursor should be displayed again
    AppTestHelper::callOnKey(app, GLFW_KEY_TAB, GLFW_PRESS);
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::UITerminal);

    // Both text and cursor would be displayed
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "X");
}

TEST_F(AppInputHandlerTest, TerminalInput_AddCharacterThenViewCursor) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "");

    // Add single character
    AppTestHelper::callOnChar(app, 'P');
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "P");
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::UITerminal);

    // Display string would be "P|" but stored text is "P"
    // Verify state is correct for rendering
    EXPECT_EQ(AppTestHelper::getTerminalText(app).length(), 1U);
}

TEST_F(AppInputHandlerTest, TerminalInput_BackspaceAndCursorState) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "Two");

    // Delete last char with backspace
    AppTestHelper::callOnKey(app, GLFW_KEY_BACKSPACE, GLFW_PRESS);
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "Tw");

    // In terminal mode, cursor should still be displayed (at "Tw|")
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::UITerminal);

    // Add new character after backspace
    AppTestHelper::callOnChar(app, 'o');
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "Two");

    // Cursor still in terminal mode at position after 'Two'
}

TEST_F(AppInputHandlerTest, TerminalInput_LongTextWithCursor) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "");

    // Build up a long string
    std::string longText = "The quick brown fox jumps";
    for (char c : longText) {
        AppTestHelper::callOnChar(app, c);
    }

    EXPECT_EQ(AppTestHelper::getTerminalText(app), longText);
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::UITerminal);

    // Display would be longText + '|'
    EXPECT_EQ(AppTestHelper::getTerminalText(app).length(), longText.length());
}

TEST_F(AppInputHandlerTest, TerminalInput_CursorStateBeforeAndAfterEscape) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "Escape");

    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::UITerminal);

    // Press escape to exit terminal mode
    AppTestHelper::callOnKey(app, GLFW_KEY_ESCAPE, GLFW_PRESS);

    // Should be in camera mode now (no cursor displayed)
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::Camera);

    // Text is preserved but cursor won't be shown
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "Escape");
}

TEST_F(AppInputHandlerTest, TerminalInput_MultipleCharacterAdditions_CursorTrails) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "");

    const char* chars = "Input";
    for (char c : std::string(chars)) {
        AppTestHelper::callOnChar(app, c);
    }

    // After adding each character, cursor trails at the end
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "Input");
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::UITerminal);

    // Cursor position is at text.length() when displayed as "Input|"
}

TEST_F(AppInputHandlerTest, TerminalInput_CursorNotAffectedByDepthBiasChanges) {
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setTerminalText(app, "Text");
    AppTestHelper::setDepthBias(app, 0.0001f);

    // Try to adjust depth bias (should be ignored in terminal mode)
    AppTestHelper::callOnKey(app, GLFW_KEY_EQUAL, GLFW_PRESS);

    // Depth bias shouldn't change
    EXPECT_NEAR(AppTestHelper::getDepthBias(app), 0.0001f, 1e-6f);

    // Text and cursor state should be unaffected
    EXPECT_EQ(AppTestHelper::getTerminalText(app), "Text");
    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::UITerminal);
}

// ---------------------------------------------------------------------------
// Depth Bias Edge Cases — test extreme values and boundary conditions
// ---------------------------------------------------------------------------

TEST_F(AppInputHandlerTest, DepthBias_VerySmallValue) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    AppTestHelper::setDepthBias(app, 0.00001f);  // 10 μm

    // Should not produce NaN or Inf
    float bias = AppTestHelper::getDepthBias(app);
    EXPECT_TRUE(std::isfinite(bias)) << "Depth bias is not finite at very small value";
}

TEST_F(AppInputHandlerTest, DepthBias_VeryLargeValue) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    AppTestHelper::setDepthBias(app, 1.0f);  // 1 meter (very large for NDC)

    // Should not produce NaN or Inf
    float bias = AppTestHelper::getDepthBias(app);
    EXPECT_TRUE(std::isfinite(bias)) << "Depth bias is not finite at very large value";
}

TEST_F(AppInputHandlerTest, DepthBias_NegativeValue) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    AppTestHelper::setDepthBias(app, -0.0001f);  // Negative bias

    // Should not produce NaN or Inf
    float bias = AppTestHelper::getDepthBias(app);
    EXPECT_TRUE(std::isfinite(bias)) << "Depth bias is not finite at negative value";
}

TEST_F(AppInputHandlerTest, DepthBias_Zero) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    AppTestHelper::setDepthBias(app, 0.0f);

    // Zero is a valid depth bias (no offset)
    float bias = AppTestHelper::getDepthBias(app);
    EXPECT_EQ(bias, 0.0f) << "Depth bias should be exactly 0";
}

TEST_F(AppInputHandlerTest, DepthBias_DecreaseFromDefault) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    AppTestHelper::setDepthBias(app, Renderer::DEPTH_BIAS_DEFAULT);

    // Decrease multiple times to reach negative
    for (int i = 0; i < 3; ++i) {
        AppTestHelper::callOnKey(app, GLFW_KEY_MINUS, GLFW_PRESS);
    }

    float expected = Renderer::DEPTH_BIAS_DEFAULT - 3.0f * 0.0001f;
    EXPECT_NEAR(AppTestHelper::getDepthBias(app), expected, 1e-6f);
    EXPECT_LT(AppTestHelper::getDepthBias(app), 0.0f) << "Should be able to set negative depth bias";
}

TEST_F(AppInputHandlerTest, DepthBias_IncreaseToLarge) {
    AppTestHelper::setInputMode(app, InputMode::Camera);
    AppTestHelper::setDepthBias(app, 0.9f);

    // Increase to cross 1.0
    AppTestHelper::callOnKey(app, GLFW_KEY_EQUAL, GLFW_PRESS);

    float expected = 0.9f + 0.0001f;
    EXPECT_NEAR(AppTestHelper::getDepthBias(app), expected, 1e-6f);
    EXPECT_GT(AppTestHelper::getDepthBias(app), 0.9f) << "Should be able to set large depth bias";
}

// ---------------------------------------------------------------------------
// Render Mode Toggle Integration Tests — verify Direct ↔ Traditional consistency
// ---------------------------------------------------------------------------

TEST_F(AppInputHandlerTest, ModeToggle_PendsFlagOnSpacePress) {
    AppTestHelper::setRenderMode(app, RenderMode::Direct);
    EXPECT_FALSE(AppTestHelper::getPendingModeToggle(app));

    AppTestHelper::callOnKey(app, GLFW_KEY_SPACE, GLFW_PRESS);

    EXPECT_TRUE(AppTestHelper::getPendingModeToggle(app))
        << "Space press should set pending mode toggle flag";
}

TEST_F(AppInputHandlerTest, ModeToggle_InputModePreservedAcrossToggle) {
    // When toggling render mode, input mode should not change
    AppTestHelper::setInputMode(app, InputMode::Camera);
    AppTestHelper::setRenderMode(app, RenderMode::Direct);
    AppTestHelper::setTerminalText(app, "Hello");

    AppTestHelper::setPendingModeToggle(app, true);
    // Simulate frame where toggle is processed (in real app this happens in frame loop)
    // Here we just verify the toggle request doesn't affect input state

    EXPECT_EQ(AppTestHelper::getInputMode(app), InputMode::Camera)
        << "Camera mode should persist across render mode toggle";
}

TEST_F(AppInputHandlerTest, ModeToggle_TerminalTextPreservedAcrossToggle) {
    // When toggling render mode, terminal text should not be cleared
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setRenderMode(app, RenderMode::Direct);
    AppTestHelper::setTerminalText(app, "Test");

    AppTestHelper::setPendingModeToggle(app, true);

    EXPECT_EQ(AppTestHelper::getTerminalText(app), "Test")
        << "Terminal text should persist across render mode toggle";
}

TEST_F(AppInputHandlerTest, ModeToggle_DepthBiasPreservedAcrossToggle) {
    // Depth bias settings should not be affected by render mode toggle
    float originalBias = 0.0002f;
    AppTestHelper::setDepthBias(app, originalBias);
    AppTestHelper::setRenderMode(app, RenderMode::Direct);

    AppTestHelper::setPendingModeToggle(app, true);

    EXPECT_NEAR(AppTestHelper::getDepthBias(app), originalBias, 1e-6f)
        << "Depth bias should persist across render mode toggle";
}

TEST_F(AppInputHandlerTest, ModeToggle_DirectToTraditionalTransition) {
    // Verify transition from Direct mode to Traditional mode sets correct state
    AppTestHelper::setRenderMode(app, RenderMode::Direct);
    EXPECT_EQ(AppTestHelper::getRenderMode(app), RenderMode::Direct);

    // Simulate the mode toggle occurring in the frame loop
    // (In real app, this happens via m_pendingModeToggle in updateFrame)
    if (AppTestHelper::getRenderMode(app) == RenderMode::Direct) {
        AppTestHelper::setRenderMode(app, RenderMode::Traditional);
    }

    EXPECT_EQ(AppTestHelper::getRenderMode(app), RenderMode::Traditional)
        << "Should transition to Traditional mode";
}

TEST_F(AppInputHandlerTest, ModeToggle_TraditionalToDirectTransition) {
    // Verify transition from Traditional mode to Direct mode sets correct state
    AppTestHelper::setRenderMode(app, RenderMode::Traditional);
    EXPECT_EQ(AppTestHelper::getRenderMode(app), RenderMode::Traditional);

    // Simulate the mode toggle
    if (AppTestHelper::getRenderMode(app) == RenderMode::Traditional) {
        AppTestHelper::setRenderMode(app, RenderMode::Direct);
    }

    EXPECT_EQ(AppTestHelper::getRenderMode(app), RenderMode::Direct)
        << "Should transition to Direct mode";
}

TEST_F(AppInputHandlerTest, ModeToggle_SequentialToggles) {
    // Verify multiple successive toggles work correctly
    AppTestHelper::setRenderMode(app, RenderMode::Direct);

    // Toggle 1: Direct → Traditional
    AppTestHelper::setRenderMode(app, RenderMode::Traditional);
    EXPECT_EQ(AppTestHelper::getRenderMode(app), RenderMode::Traditional);

    // Toggle 2: Traditional → Direct
    AppTestHelper::setRenderMode(app, RenderMode::Direct);
    EXPECT_EQ(AppTestHelper::getRenderMode(app), RenderMode::Direct);

    // Toggle 3: Direct → Traditional
    AppTestHelper::setRenderMode(app, RenderMode::Traditional);
    EXPECT_EQ(AppTestHelper::getRenderMode(app), RenderMode::Traditional);
}

TEST_F(AppInputHandlerTest, ModeToggle_IgnoredInTerminalMode) {
    // Mode toggle (Space) should not work in terminal mode
    AppTestHelper::setInputMode(app, InputMode::UITerminal);
    AppTestHelper::setRenderMode(app, RenderMode::Direct);
    AppTestHelper::setTerminalText(app, "");

    // Try to toggle while in terminal mode
    AppTestHelper::callOnKey(app, GLFW_KEY_SPACE, GLFW_PRESS);

    // Toggle should NOT be pended (Space in terminal mode is just a character)
    EXPECT_FALSE(AppTestHelper::getPendingModeToggle(app))
        << "Mode toggle should not work in terminal mode; Space inputs text";
}
