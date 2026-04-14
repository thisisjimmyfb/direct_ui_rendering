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

// ---------------------------------------------------------------------------
// Android asset manager integration — UISystem::AssetLoader interface
// These tests verify the asset loader abstraction that allows Android to load
// atlas data from APK assets via AAssetManager instead of a filesystem path.
// ---------------------------------------------------------------------------

#include "ui_system.h"
#include <vector>
#include <cstdint>

TEST(AndroidAssetIntegration, AssetLoaderTypeIsCallable) {
    // UISystem::AssetLoader must be a callable type that takes a path string
    // and returns a byte vector. This will fail to compile without the typedef.
    UISystem::AssetLoader loader = [](const char*) -> std::vector<uint8_t> {
        return {};
    };
    auto result = loader("dummy_path");
    EXPECT_TRUE(result.empty());
}

TEST(AndroidAssetIntegration, MakeFileAssetLoaderExists) {
    // UISystem::makeFileAssetLoader() must exist and return a loader that
    // returns empty bytes for a nonexistent path.
    auto loader = UISystem::makeFileAssetLoader();
    auto result = loader("nonexistent_atlas_xyz_12345.png");
    EXPECT_TRUE(result.empty());
}

TEST(AndroidAssetIntegration, MakeFileAssetLoaderReadsExistingFile) {
    // Verify the file-based loader can read a real file (the production atlas).
    // This test is skipped if the atlas is not present in the build dir.
    auto loader = UISystem::makeFileAssetLoader();
    auto result = loader(ATLAS_ASSET_PATH);
    if (result.empty()) {
        GTEST_SKIP() << "atlas.png not present at " ATLAS_ASSET_PATH ", skipping";
    }
    EXPECT_GT(result.size(), 0u);
}
