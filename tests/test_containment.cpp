#include "containment_fixture.h"

// ---------------------------------------------------------------------------
// Test 1: Direct mode — existing test, now uses shared fixture setup.
// ---------------------------------------------------------------------------

TEST_F(ContainmentTest, DirectMode_MagentaPixels_InsideSurfaceQuad)
{
    glm::vec3 P00{-0.5f,  0.5f, 0.0f};
    glm::vec3 P10{ 0.5f,  0.5f, 0.0f};
    glm::vec3 P01{-0.5f, -0.5f, 0.0f};
    glm::vec3 P11{ 0.5f, -0.5f, 0.0f};

    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    renderer.updateSceneUBO(makeSpotlightSceneUBO(scene, view, proj));

    auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                               static_cast<float>(W_UI),
                                               static_cast<float>(H_UI), vp);
    auto clipPlanes = computeClipPlanes(P00, P10, P01);

    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = transforms.M_total;
    surfaceUBO.worldMatrix = transforms.M_world;
    for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
    surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
    renderer.updateSurfaceUBO(surfaceUBO);

    auto pixels = renderAndReadback(/*directMode=*/true);
    assertMagentaContained(pixels, vp, P00, P10, P11, P01);
}

// ---------------------------------------------------------------------------
// Test 2: Traditional mode — composited UI RT must also be contained in quad.
//
// The UI is first rendered to the offscreen RT (recordUIRTPass, UI_TEST_COLOR →
// solid magenta), then composited onto the teal surface quad in recordMainPass.
// Because compositing uses pre-multiplied alpha and the test color has alpha=1,
// the composited output is solid magenta where the quad falls on screen.
// The test verifies those magenta pixels lie within the projected quad boundary.
// ---------------------------------------------------------------------------

TEST_F(ContainmentTest, TraditionalMode_MagentaPixels_InsideSurfaceQuad)
{
    glm::vec3 P00{-0.5f,  0.5f, 0.0f};
    glm::vec3 P10{ 0.5f,  0.5f, 0.0f};
    glm::vec3 P01{-0.5f, -0.5f, 0.0f};
    glm::vec3 P11{ 0.5f, -0.5f, 0.0f};

    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    renderer.updateSceneUBO(makeSpotlightSceneUBO(scene, view, proj));

    auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                               static_cast<float>(W_UI),
                                               static_cast<float>(H_UI), vp);
    auto clipPlanes = computeClipPlanes(P00, P10, P01);

    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = transforms.M_total;
    surfaceUBO.worldMatrix = transforms.M_world;
    for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
    surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
    renderer.updateSurfaceUBO(surfaceUBO);

    // Position the surface quad so the compositor draws the UI at the correct location.
    renderer.updateSurfaceQuad(P00, P10, P01, P11);

    glm::mat4 uiOrtho = glm::ortho(0.0f, static_cast<float>(W_UI),
                                   static_cast<float>(H_UI), 0.0f,
                                   -1.0f, 1.0f);

    auto pixels = renderAndReadback(/*directMode=*/false, uiOrtho);

    // Ensure the composited UI RT actually produced magenta pixels in the
    // readback — a vacuous containment pass would occur if the surface quad
    // were off-screen or misconfigured and no magenta pixels were present.
    int magentaCount = countMagentaPixels(pixels);
    EXPECT_GT(magentaCount, 0)
        << "No magenta pixels found in traditional-mode readback — "
        << "surface quad may be off-screen or the composite pass is not executing";

    assertMagentaContained(pixels, vp, P00, P10, P11, P01);
}

// ---------------------------------------------------------------------------
// Test 3: Extreme angle — camera nearly edge-on to the UI surface.
//
// The camera is placed at (2, 0, 0.15) looking at the origin, so the surface
// (in the XY plane, normal = +Z) is viewed at ~85° from face-on.  The projected
// quad is a very thin sliver.  The clip planes in M_total must still correctly
// prevent UI pixels from bleeding outside that sliver.
// ---------------------------------------------------------------------------

TEST_F(ContainmentTest, ExtremeAngle_DirectMode_MagentaPixels_InsideSurfaceQuad)
{
    glm::vec3 P00{-0.5f,  0.5f, 0.0f};
    glm::vec3 P10{ 0.5f,  0.5f, 0.0f};
    glm::vec3 P01{-0.5f, -0.5f, 0.0f};
    glm::vec3 P11{ 0.5f, -0.5f, 0.0f};

    // Camera ~85° from surface normal (nearly edge-on).
    // Placed along +X with a small Z offset so the surface isn't degenerate on screen.
    glm::mat4 view = glm::lookAt(glm::vec3(2.0f, 0.0f, 0.15f),
                                 glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    renderer.updateSceneUBO(makeSpotlightSceneUBO(scene, view, proj));

    auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                               static_cast<float>(W_UI),
                                               static_cast<float>(H_UI), vp);
    auto clipPlanes = computeClipPlanes(P00, P10, P01);

    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = transforms.M_total;
    surfaceUBO.worldMatrix = transforms.M_world;
    for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
    surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
    renderer.updateSurfaceUBO(surfaceUBO);

    // Use a larger margin (4 px) for the edge-on case: the thin projected sliver
    // amplifies MSAA edge-blending effects relative to a face-on view.
    auto pixels = renderAndReadback(/*directMode=*/true);

    // Ensure the test is non-vacuous: at least one magenta pixel must be present.
    // In the extreme angle case, the projected sliver may be very thin, so we need
    // to verify that magenta pixels were actually rendered.
    EXPECT_GT(countMagentaPixels(pixels), 0)
        << "No magenta pixels found in extreme-angle direct-mode readback — "
        << "surface may be off-screen or the direct-mode pass is not executing";

    assertMagentaContained(pixels, vp, P00, P10, P11, P01, /*margin=*/4.0f);
}

// ---------------------------------------------------------------------------
// Test 4: Multi-frame animation — direct mode follows the animated surface.
//
// The Scene animates the UI surface with M_anim(t).  This test renders three
// separate frames (t=0, t=1, t=2), recomputing worldCorners() and the full
// transform chain each time.  For each frame it asserts:
//   (a) at least one magenta pixel is present (surface is on-screen),
//   (b) all magenta pixels lie within the screen-space projection of the
//       updated surface quad (clip planes track the moving surface).
// ---------------------------------------------------------------------------

TEST_F(ContainmentTest, DirectMode_AnimationFrames_MagentaContained)
{
    // Camera positioned inside the room (z < 3) looking toward the back wall
    // where the animated surface oscillates near z=-2.5, y≈1.5.
    // Using z=2.5 keeps the camera inside the room so the front wall (z=3)
    // does not occlude the surface.
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.5f, 2.5f),
                                 glm::vec3(0.0f, 1.5f, -2.5f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    renderer.updateSceneUBO(makeSpotlightSceneUBO(scene, view, proj));

    for (float t : {0.0f, 1.0f, 2.0f}) {
        SCOPED_TRACE("t=" + std::to_string(t));

        glm::vec3 P00, P10, P01, P11;
        scene.worldCorners(t, P00, P10, P01, P11);

        auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                                   static_cast<float>(W_UI),
                                                   static_cast<float>(H_UI), vp);
        auto clipPlanes = computeClipPlanes(P00, P10, P01);

        SurfaceUBO surfaceUBO{};
        surfaceUBO.totalMatrix = transforms.M_total;
        surfaceUBO.worldMatrix = transforms.M_world;
        for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
        surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
        renderer.updateSurfaceUBO(surfaceUBO);

        auto pixels = renderAndReadback(/*directMode=*/true);

        // (a) Non-vacuous: at least one magenta pixel must be present.
        EXPECT_GT(countMagentaPixels(pixels), 0)
            << "No magenta pixels at t=" << t
            << " — surface may be off-screen or M_total is degenerate";

        // (b) All magenta pixels must lie within the projected quad boundary.
        assertMagentaContained(pixels, vp, P00, P10, P11, P01);
    }
}
