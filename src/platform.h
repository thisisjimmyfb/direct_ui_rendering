#pragma once

#include <cstddef>
#include <cstdint>

#ifdef __ANDROID__
#include <android/native_window.h>
#endif

// Forward-declare GLFWwindow to avoid pulling in GLFW headers.
#ifndef __ANDROID__
struct GLFWwindow;
#endif

enum class PlatformType { Desktop, Android };

// NativeWindowHandle — platform-agnostic wrapper for a native window pointer.
// On desktop it wraps a GLFWwindow*; on Android it wraps an ANativeWindow*.
// Passing nullptr (or default-constructing) produces a null/headless handle.
struct NativeWindowHandle {
    NativeWindowHandle() noexcept = default;
    NativeWindowHandle(std::nullptr_t) noexcept {}

#ifndef __ANDROID__
    static NativeWindowHandle fromGLFW(GLFWwindow* w) noexcept {
        NativeWindowHandle h;
        h.m_type         = PlatformType::Desktop;
        h.m_nativeWindow = w;
        return h;
    }
#endif

#ifdef __ANDROID__
    static NativeWindowHandle fromAndroid(ANativeWindow* w) noexcept {
        NativeWindowHandle h;
        h.m_type         = PlatformType::Android;
        h.m_nativeWindow = w;
        return h;
    }

    ANativeWindow* androidWindow() const noexcept {
        if (m_type != PlatformType::Android) return nullptr;
        return static_cast<ANativeWindow*>(m_nativeWindow);
    }
#endif

#ifndef __ANDROID__
    GLFWwindow* glfwWindow() const noexcept {
        if (m_type != PlatformType::Desktop) return nullptr;
        return static_cast<GLFWwindow*>(m_nativeWindow);
    }
#endif

    PlatformType platformType() const noexcept { return m_type; }
    bool         isNull()       const noexcept { return m_nativeWindow == nullptr; }

private:
    PlatformType m_type{PlatformType::Desktop};
    void*        m_nativeWindow{nullptr};
};
