#include <gtest/gtest.h>
#include "platform.h"

// ---------------------------------------------------------------------------
// NativeWindowHandle tests — verify the platform abstraction compiles and
// behaves correctly.  These tests run on desktop (no Android NDK required).
// ---------------------------------------------------------------------------

TEST(NativeWindowHandle, DefaultConstructedIsNull) {
    NativeWindowHandle h;
    EXPECT_TRUE(h.isNull());
}

TEST(NativeWindowHandle, NullptrConstructorIsNull) {
    NativeWindowHandle h(nullptr);
    EXPECT_TRUE(h.isNull());
    EXPECT_EQ(h.platformType(), PlatformType::Desktop);
}

TEST(NativeWindowHandle, FromGLFWNullptrIsNull) {
    NativeWindowHandle h = NativeWindowHandle::fromGLFW(nullptr);
    EXPECT_EQ(h.platformType(), PlatformType::Desktop);
    EXPECT_EQ(h.glfwWindow(), nullptr);
    EXPECT_TRUE(h.isNull());
}

TEST(NativeWindowHandle, FromGLFWNonNullRoundtrips) {
    // Use a non-null dummy address to simulate a GLFW window without creating one.
    GLFWwindow* fakeWindow = reinterpret_cast<GLFWwindow*>(static_cast<uintptr_t>(0x1234));
    NativeWindowHandle h = NativeWindowHandle::fromGLFW(fakeWindow);
    EXPECT_EQ(h.platformType(), PlatformType::Desktop);
    EXPECT_EQ(h.glfwWindow(), fakeWindow);
    EXPECT_FALSE(h.isNull());
}

TEST(NativeWindowHandle, PlatformTypeIsDesktopOnNonAndroid) {
#ifndef __ANDROID__
    NativeWindowHandle h = NativeWindowHandle::fromGLFW(nullptr);
    EXPECT_EQ(h.platformType(), PlatformType::Desktop);
#else
    GTEST_SKIP() << "Not on Android";
#endif
}

#ifdef __ANDROID__
TEST(NativeWindowHandle, FromAndroidRoundtrips) {
    ANativeWindow* fakeWin = reinterpret_cast<ANativeWindow*>(static_cast<uintptr_t>(0x5678));
    NativeWindowHandle h = NativeWindowHandle::fromAndroid(fakeWin);
    EXPECT_EQ(h.platformType(), PlatformType::Android);
    EXPECT_EQ(h.androidWindow(), fakeWin);
    EXPECT_FALSE(h.isNull());
}

TEST(NativeWindowHandle, AndroidWindowReturnsNullForDesktopHandle) {
    NativeWindowHandle h = NativeWindowHandle::fromGLFW(nullptr);
    EXPECT_EQ(h.androidWindow(), nullptr);
}
#endif

TEST(NativeWindowHandle, CopyConstructorPreservesState) {
    GLFWwindow* fakeWindow = reinterpret_cast<GLFWwindow*>(static_cast<uintptr_t>(0xABCD));
    NativeWindowHandle a = NativeWindowHandle::fromGLFW(fakeWindow);
    NativeWindowHandle b = a;
    EXPECT_EQ(b.platformType(), a.platformType());
    EXPECT_EQ(b.glfwWindow(), a.glfwWindow());
    EXPECT_EQ(b.isNull(), a.isNull());
}
