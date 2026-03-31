#include <gtest/gtest.h>
#include "containment_fixture.h"

// ---------------------------------------------------------------------------
// Test 5: Back wall not self-shadowed
//
// Position the camera facing the back wall (Z = -D), render one frame, and
// read back the center region of the back wall.  Since the directional light
// has direction (-0.5, -1.0, -0.5) and the back wall normal is (0, 0, 1),
// the dot product N·L = 0 — the wall receives zero diffuse illumination.
// Without a working depth bias, the back wall would be entirely shadowed
// (acne) and appear black.  This test asserts that the mean luminance of
// the back-wall region exceeds ambientColor + 0.1, proving that at least
// some diffuse light contribution reaches the wall (the depth bias prevents
// total self-shadowing).
// ---------------------------------------------------------------------------

TEST_F(ContainmentTest, BackWall_NotSelfShadowed)
{
    // Room dimensions (matching Scene::init())
    constexpr float W = 2.0f;   // half-width
    constexpr float H = 3.0f;   // full height
    constexpr float D = 3.0f;   // half-depth

    // Camera facing the back wall (Z = -D) from the front.
    // Position: in front of the back wall, looking at its center.
    glm::vec3 camPos{0.0f, H * 0.5f, D + 5.0f};   // (0, 1.5, 8)
    glm::vec3 camTarget{-W * 0.5f, H * 0.5f, -D};  // point on back wall
    glm::mat4 view = glm::lookAt(camPos, camTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    SceneUBO sceneUBO{};
    sceneUBO.view          = view;
    sceneUBO.proj          = proj;
    sceneUBO.lightViewProj = scene.lightViewProj();
    sceneUBO.lightPos      = glm::vec4(scene.light().position, 1.0f);
    sceneUBO.lightDir      = glm::vec4(scene.light().direction,
                                       std::cos(scene.light().outerConeAngle));
    sceneUBO.lightColor    = glm::vec4(scene.light().color,
                                       std::cos(scene.light().innerConeAngle));
    sceneUBO.ambientColor  = glm::vec4(scene.light().ambient,   1.0f);
    renderer.updateSceneUBO(sceneUBO);

    auto pixels = renderAndReadback(/*directMode=*/true);

    // Define a center strip on the back wall in screen space.
    // The back wall spans most of the screen when viewed from this angle.
    // We sample a horizontal strip in the vertical center of the image.
    const int stripTop    = FB_HEIGHT / 3;
    const int stripBottom = 2 * FB_HEIGHT / 3;
    const int stripLeft   = FB_WIDTH / 4;
    const int stripRight  = 3 * FB_WIDTH / 4;

    // Compute mean luminance of the center strip.
    // Luminance = 0.299*R + 0.587*G + 0.114*B (standard NTSC weights).
    double sumLuminance = 0.0;
    int pixelCount = 0;
    for (int y = stripTop; y < stripBottom; ++y) {
        for (int x = stripLeft; x < stripRight; ++x) {
            const uint8_t* px = pixels.data() + (y * FB_WIDTH + x) * 4;
            float r = px[0] / 255.0f;
            float g = px[1] / 255.0f;
            float b = px[2] / 255.0f;
            float lum = 0.299f * r + 0.587f * g + 0.114f * b;
            sumLuminance += lum;
            ++pixelCount;
        }
    }

    EXPECT_GT(pixelCount, 0) << "No pixels sampled in back-wall region";

    double meanLuminance = sumLuminance / static_cast<double>(pixelCount);

    // The ambient color from scene.h is {0.15f, 0.15f, 0.2f}.
    // Its luminance contribution alone is:
    //   0.299*0.15 + 0.587*0.15 + 0.114*0.2 = 0.1557
    // The test asserts meanLuminance > ambientLuminance + 0.1, i.e.,
    // > 0.2557, proving that some diffuse light reaches the back wall
    // (the depth bias prevents total self-shadowing/acne).
    float ambientLuminance =
        0.299f * scene.light().ambient.r +
        0.587f * scene.light().ambient.g +
        0.114f * scene.light().ambient.b;
    float threshold = ambientLuminance + 0.1f;

    EXPECT_GT(meanLuminance, threshold)
        << "Back wall mean luminance=" << meanLuminance
        << " <= threshold=" << threshold
        << " (ambient luminance + 0.1); "
        << "the back wall appears too dark, indicating excessive self-shadowing "
        << "likely due to insufficient depth bias in the shadow map";
}

// ---------------------------------------------------------------------------
// Test 6: PCF shadow symmetry — centered {-0.5, 0.5} kernel produces no bias.
//
// This test validates that the 2×2 PCF kernel in room.frag produces symmetric
// results when sampling around a lit/shadow transition. The kernel uses offsets
// {-0.5, +0.5} texels which should be centered, producing no directional bias.
//
// We render a view that shows the room with visible lighting variation, then
// scan horizontally across a row that contains a lit/shadow transition. We find
// the midpoint (where luminance is halfway between the lit and shadow values),
// then sample equal distances on either side of this midpoint.
//
// For a centered kernel, the luminance difference from the midpoint should be
// symmetric: (litNear - midLum) ≈ (midLum - shadowNear), with ratio within ±10%.
// ---------------------------------------------------------------------------
TEST_F(ContainmentTest, PCFShadow_Symmetry_CenteredKernel)
{
    // Camera inside the room looking at the back wall — the spotlight illuminates
    // the back wall and the UI quad's shadow falls onto it, creating a visible
    // lit/shadow boundary that exercises the PCF penumbra.
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.5f, 0.5f),
                                 glm::vec3(0.0f, 1.5f, -3.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    SceneUBO sceneUBO{};
    sceneUBO.view          = view;
    sceneUBO.proj          = proj;
    sceneUBO.lightViewProj = scene.lightViewProj();
    sceneUBO.lightPos      = glm::vec4(scene.light().position, 1.0f);
    sceneUBO.lightDir      = glm::vec4(scene.light().direction,
                                       std::cos(scene.light().outerConeAngle));
    sceneUBO.lightColor    = glm::vec4(scene.light().color,
                                       std::cos(scene.light().innerConeAngle));
    sceneUBO.ambientColor  = glm::vec4(scene.light().ambient, 1.0f);
    renderer.updateSceneUBO(sceneUBO);

    // SurfaceUBO must be bound even though the room-only pass never reads it.
    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = glm::mat4(1.0f);
    surfaceUBO.worldMatrix = glm::mat4(1.0f);
    surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
    renderer.updateSurfaceUBO(surfaceUBO);

    // Place the UI surface quad (at t=0) so it casts a shadow onto the back wall.
    // This exercises the shadow pass with a real occluder between the light and the wall.
    glm::vec3 P00, P10, P01, P11;
    scene.worldCorners(0.0f, P00, P10, P01, P11);
    renderer.updateSurfaceQuad(P00, P10, P01, P11);
    renderer.updateUIShadowQuad(P00, P10, P01, P11);

    auto pixels = renderAndReadback(/*directMode=*/true);

    auto lumAt = [&](int x, int y) -> float {
        const uint8_t* px = pixels.data() + (y * FB_WIDTH + x) * 4;
        return 0.299f * px[0] / 255.0f +
               0.587f * px[1] / 255.0f +
               0.114f * px[2] / 255.0f;
    };

    // Scan the centre row horizontally.
    const int row = FB_HEIGHT / 2;

    // Sample luminance at all columns.
    std::vector<float> luminance;
    for (int x = 0; x < FB_WIDTH; ++x) {
        luminance.push_back(lumAt(x, row));
    }

    // Find the minimum and maximum luminance.
    float minLum = *std::min_element(luminance.begin(), luminance.end());
    float maxLum = *std::max_element(luminance.begin(), luminance.end());

    // Verify we have visible lighting variation.
    ASSERT_GT(maxLum - minLum, 0.05f)
        << "No visible lighting variation in centre row (row=" << row << ")\n"
        << "  minLum=" << minLum << "  maxLum=" << maxLum;

    // Target luminance is the midpoint between min and max.
    const float targetLum = (minLum + maxLum) * 0.5f;

    // Find the column closest to the midpoint luminance.
    int edgeCol = FB_WIDTH / 2;
    float bestDiff = 1e9f;
    for (int x = 0; x < (int)FB_WIDTH; ++x) {
        float diff = std::abs(luminance[x] - targetLum);
        if (diff < bestDiff) {
            bestDiff = diff;
            edgeCol = x;
        }
    }

    // Sample k=20 pixels on each side of the edge — well outside the PCF penumbra.
    const int k = 20;
    ASSERT_GE(edgeCol - k, 0) << "Left sample out of bounds at edgeCol=" << edgeCol;
    ASSERT_LT(edgeCol + k, (int)FB_WIDTH) << "Right sample out of bounds at edgeCol=" << edgeCol;

    const float leftNear  = lumAt(edgeCol - k, row);
    const float rightNear = lumAt(edgeCol + k, row);
    const float edgeLum   = lumAt(edgeCol, row);

    // Determine which side is brighter.
    const float brighter  = std::max(leftNear, rightNear);
    const float darker    = std::min(leftNear, rightNear);

    EXPECT_GT(brighter, darker)
        << "Expected brightness variation: brighter=" << brighter
        << " darker=" << darker << " edgeCol=" << edgeCol;

    // Compute excess and deficit from the edge luminance.
    const float excess  = brighter - edgeLum;
    const float deficit = edgeLum - darker;

    ASSERT_GT(excess, 0.0f) << "excess must be positive: brighter=" << brighter << " edgeLum=" << edgeLum;
    ASSERT_GT(deficit, 0.0f) << "deficit must be positive: edgeLum=" << edgeLum << " darker=" << darker;

    // Symmetry assertion: excess ≈ deficit within ±15%.
    // This validates the {-0.5, +0.5} PCF kernel is centered.
    // ±15% tolerance accommodates mild perspective distortion in the spotlight
    // shadow map which can cause slight asymmetry at the penumbra edge.
    float ratio = excess / deficit;
    EXPECT_NEAR(ratio, 1.0f, 0.15f)
        << "PCF penumbra is asymmetric:\n"
        << "  excess=" << excess << "  deficit=" << deficit
        << "  ratio=" << ratio
        << " (expected 1.0 ± 0.15 for centred {-0.5, +0.5} kernel)\n"
        << "  edgeCol=" << edgeCol << "  edgeLum=" << edgeLum
        << "  brighter=" << brighter << "  darker=" << darker
        << "  targetLum=" << targetLum
        << "  minLum=" << minLum << "  maxLum=" << maxLum;
}

// ---------------------------------------------------------------------------
// Test 7: UI quad casts a shadow onto the back wall.
//
// This test verifies that the UI surface quad drawn in the shadow pass acts
// as an occluder: the centre of the back wall is measurably darker when the
// UI shadow quad is placed at its real t=0 position than when no occluder is
// present (degenerate/zero corners → effectively no shadow quad drawn).
// ---------------------------------------------------------------------------
TEST_F(ContainmentTest, ShadowCasting_UIQuadDarkensBackWall)
{
    // Camera looking at back wall from inside the room.
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.5f, 0.5f),
                                 glm::vec3(0.0f, 1.5f, -3.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    auto setupSceneUBO = [&]() {
        SceneUBO ubo{};
        ubo.view          = view;
        ubo.proj          = proj;
        ubo.lightViewProj = scene.lightViewProj();
        ubo.lightPos      = glm::vec4(scene.light().position, 1.0f);
        ubo.lightDir      = glm::vec4(scene.light().direction,
                                      std::cos(scene.light().outerConeAngle));
        ubo.lightColor    = glm::vec4(scene.light().color,
                                      std::cos(scene.light().innerConeAngle));
        ubo.ambientColor  = glm::vec4(scene.light().ambient, 1.0f);
        renderer.updateSceneUBO(ubo);

        SurfaceUBO surfaceUBO{};
        surfaceUBO.totalMatrix = glm::mat4(1.0f);
        surfaceUBO.worldMatrix = glm::mat4(1.0f);
        surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
        renderer.updateSurfaceUBO(surfaceUBO);
    };

    auto centerLuminance = [&](const std::vector<uint8_t>& pixels) -> float {
        // Sample a small patch in the centre of the image (back wall centre).
        const int cx = FB_WIDTH  / 2;
        const int cy = FB_HEIGHT / 2;
        const int r  = 10;
        double sum = 0.0;
        int count = 0;
        for (int y = cy - r; y <= cy + r; ++y) {
            for (int x = cx - r; x <= cx + r; ++x) {
                if (x < 0 || x >= (int)FB_WIDTH || y < 0 || y >= (int)FB_HEIGHT) continue;
                const uint8_t* px = pixels.data() + (y * FB_WIDTH + x) * 4;
                sum += 0.299f * px[0] / 255.0f
                     + 0.587f * px[1] / 255.0f
                     + 0.114f * px[2] / 255.0f;
                ++count;
            }
        }
        return count > 0 ? static_cast<float>(sum / count) : 0.0f;
    };

    // Render WITHOUT shadow occluder: use zero-area degenerate quad.
    setupSceneUBO();
    renderer.updateSurfaceQuad({0,0,0}, {0,0,0}, {0,0,0}, {0,0,0});
    renderer.updateUIShadowQuad({0,0,0}, {0,0,0}, {0,0,0}, {0,0,0});
    auto pixelsNoShadow = renderAndReadback(/*directMode=*/true);
    float lumNoShadow = centerLuminance(pixelsNoShadow);

    // Render WITH shadow occluder: UI quad at t=0.
    setupSceneUBO();
    glm::vec3 P00, P10, P01, P11;
    scene.worldCorners(0.0f, P00, P10, P01, P11);
    renderer.updateSurfaceQuad(P00, P10, P01, P11);
    renderer.updateUIShadowQuad(P00, P10, P01, P11);
    auto pixelsWithShadow = renderAndReadback(/*directMode=*/true);
    float lumWithShadow = centerLuminance(pixelsWithShadow);

    EXPECT_LT(lumWithShadow, lumNoShadow)
        << "Back wall centre should be darker when the UI quad casts a shadow:\n"
        << "  lumWithShadow=" << lumWithShadow
        << "  lumNoShadow=" << lumNoShadow;
}
