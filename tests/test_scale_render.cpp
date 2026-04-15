#include <gtest/gtest.h>
#include "containment_fixture.h"

// ---------------------------------------------------------------------------
// Test 7: Non-uniform quad scale — clip planes track the reshaped surface.
//
// Uses Scene::worldCorners with scaleW=2.0, scaleH=0.5 to produce a surface
// that is twice as wide and half as tall as the default.  The clip planes are
// derived from the reshaped world corners.  The test verifies:
//   (a) at least one magenta pixel is present on screen (surface is visible),
//   (b) all magenta pixels lie within the screen-space projection of the
//       reshaped quad — i.e. the clip planes correctly reflect the new bounds.
// ---------------------------------------------------------------------------

TEST_F(ContainmentTest, NonUniformScale_DirectMode_ClipPlanesTrackReshapedSurface)
{
    constexpr float scaleW = 2.0f;
    constexpr float scaleH = 0.5f;

    // Camera positioned inside the room looking toward the back wall where
    // the animated surface sits at t=0 (near z=-2.5, y≈1.5).
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.5f, 2.5f),
                                 glm::vec3(0.0f, 1.5f, -2.5f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    renderer.updateSceneUBO(makeSpotlightSceneUBO(scene, view, proj));

    // Get world corners with non-uniform scale applied.
    glm::vec3 P00, P10, P01, P11;
    scene.worldCorners(0.0f, P00, P10, P01, P11, scaleW, scaleH);

    // Scale canvas dimensions proportionally so font size is invariant.
    // This causes H_UI * scaleH content to map exactly to the reshaped surface
    // height-wise, while W_UI * scaleW is the logical canvas width.
    auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                               static_cast<float>(W_UI) * scaleW,
                                               static_cast<float>(H_UI) * scaleH,
                                               vp);
    auto clipPlanes = computeClipPlanes(P00, P10, P01);

    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = transforms.M_total;
    surfaceUBO.worldMatrix = transforms.M_world;
    for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
    surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
    renderer.updateSurfaceUBO(surfaceUBO);

    auto pixels = renderAndReadback(/*directMode=*/true);

    // (a) Non-vacuous: at least one magenta pixel must be visible.
    EXPECT_GT(countMagentaPixels(pixels), 0)
        << "No magenta pixels found with scaleW=" << scaleW << " scaleH=" << scaleH
        << " — reshaped surface may be off-screen or M_total is degenerate";

    // (b) All magenta pixels must lie within the projected reshaped quad.
    assertMagentaContained(pixels, vp, P00, P10, P11, P01);
}

// ---------------------------------------------------------------------------
// Test 8: Font-size invariance across modes — scaled quad, sub-canvas rect.
//
// Render a surface with scaleW=0.5 (half-width) in both direct mode and
// traditional mode.  The UI vertex buffer covers only the LEFT HALF of UI
// space: x in [0, W_UI/2], y in [0, H_UI].  Both modes use the unscaled
// canvas dimensions W_UI × H_UI so:
//
//   Direct mode   : M_us maps x=[0,W_UI/2] → s=[0,0.5] — left half of surface.
//   Traditional   : ortho maps x=[0,W_UI/2] to left half of RT; the full RT
//                   is composited onto the surface quad → left half of surface.
//
// The left-half sub-canvas therefore projects to the same screen region in
// both modes.  The test asserts that the bounding boxes of magenta pixels
// agree within a 3-pixel tolerance, verifying font-size invariance.
// ---------------------------------------------------------------------------

TEST_F(ContainmentTest, FontSizeInvariance_DirectVsTraditional_ScaledQuad)
{
    constexpr float scaleW = 0.5f;
    constexpr float scaleH = 1.0f;

    // Camera inside room looking toward back wall (same camera as Test 7).
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.5f, 2.5f),
                                 glm::vec3(0.0f, 1.5f, -2.5f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    renderer.updateSceneUBO(makeSpotlightSceneUBO(scene, view, proj));

    // Surface corners with scaleW=0.5 at t=0.
    glm::vec3 P00, P10, P01, P11;
    scene.worldCorners(0.0f, P00, P10, P01, P11, scaleW, scaleH);

    // Both modes use the unscaled canvas (W_UI × H_UI).
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

    // Traditional mode needs the surface quad positioned correctly.
    renderer.updateSurfaceQuad(P00, P10, P01, P11);

    // Sub-canvas vertex buffer: a rect covering the LEFT HALF of UI space.
    // x in [0, W_UI/2], y in [0, H_UI] — two triangles, 6 vertices.
    const float halfW = static_cast<float>(W_UI) * 0.5f;
    const float H     = static_cast<float>(H_UI);
    UIVertex subVerts[UI_VTX_COUNT] = {
        {{0,     0}, {0.0f, 0.0f}}, {{halfW, 0}, {0.5f, 0.0f}}, {{halfW, H}, {0.5f, 1.0f}},
        {{0,     0}, {0.0f, 0.0f}}, {{halfW, H}, {0.5f, 1.0f}}, {{0,     H}, {0.0f, 1.0f}},
    };
    VkBuffer      subVtxBuf   = VK_NULL_HANDLE;
    VmaAllocation subVtxAlloc = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size        = sizeof(subVerts);
        bci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        ASSERT_EQ(vmaCreateBuffer(renderer.getAllocator(), &bci, &ai,
                                  &subVtxBuf, &subVtxAlloc, nullptr), VK_SUCCESS);
        void* mapped = nullptr;
        vmaMapMemory(renderer.getAllocator(), subVtxAlloc, &mapped);
        memcpy(mapped, subVerts, sizeof(subVerts));
        vmaUnmapMemory(renderer.getAllocator(), subVtxAlloc);
    }

    // Temporarily replace the fixture's vertex buffer with the sub-canvas one
    // so renderAndReadback records draws using the left-half rect.
    VkBuffer      savedBuf   = uiVtxBuf;
    VmaAllocation savedAlloc = uiVtxAlloc;
    uiVtxBuf   = subVtxBuf;
    uiVtxAlloc = subVtxAlloc;

    // Ortho matrix for traditional mode: full unscaled canvas.
    glm::mat4 uiOrtho = glm::ortho(0.0f, static_cast<float>(W_UI),
                                   static_cast<float>(H_UI), 0.0f,
                                   -1.0f, 1.0f);

    auto pixelsDirect = renderAndReadback(/*directMode=*/true);
    auto pixelsTrad   = renderAndReadback(/*directMode=*/false, uiOrtho);

    // Restore original full-canvas vertex buffer; destroy sub-canvas buffer.
    uiVtxBuf   = savedBuf;
    uiVtxAlloc = savedAlloc;
    vmaDestroyBuffer(renderer.getAllocator(), subVtxBuf, subVtxAlloc);

    // Compute bounding boxes of magenta pixels for each mode.
    auto bboxDirect = render_helpers::computeMagentaBBox(pixelsDirect, FB_WIDTH, FB_HEIGHT);
    auto bboxTrad   = render_helpers::computeMagentaBBox(pixelsTrad,   FB_WIDTH, FB_HEIGHT);

    ASSERT_TRUE(bboxDirect.valid)
        << "No magenta pixels in direct-mode render "
           "(scaleW=" << scaleW << " — surface may be off-screen)";
    ASSERT_TRUE(bboxTrad.valid)
        << "No magenta pixels in traditional-mode render "
           "(scaleW=" << scaleW << " — surface quad or composite pass misconfigured)";

    // Both modes must place the left-half sub-canvas at the same screen region.
    constexpr int kTol = 3;
    EXPECT_NEAR(bboxDirect.minX, bboxTrad.minX, kTol)
        << "Left edge mismatch: direct=" << bboxDirect.minX
        << " trad=" << bboxTrad.minX;
    EXPECT_NEAR(bboxDirect.maxX, bboxTrad.maxX, kTol)
        << "Right edge mismatch: direct=" << bboxDirect.maxX
        << " trad=" << bboxTrad.maxX;
    EXPECT_NEAR(bboxDirect.minY, bboxTrad.minY, kTol)
        << "Top edge mismatch: direct=" << bboxDirect.minY
        << " trad=" << bboxTrad.minY;
    EXPECT_NEAR(bboxDirect.maxY, bboxTrad.maxY, kTol)
        << "Bottom edge mismatch: direct=" << bboxDirect.maxY
        << " trad=" << bboxTrad.maxY;
}
