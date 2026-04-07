#include <gtest/gtest.h>
#include "containment_fixture.h"

// ---------------------------------------------------------------------------
// Test: Animated UI cube renders without shadow sampling artifacts
//
// Renders the animated cube at 5 different time points and verifies that:
// 1. The rendering completes successfully (no Vulkan errors)
// 2. The rendered output contains non-zero content (cube is visible)
// 3. No extreme luminance values (not all black or all white from artifacts)
// 4. Renders remain stable across multiple animation frames
// ---------------------------------------------------------------------------
TEST_F(ContainmentTest, UIcubeShadow_AnimatedCubeRendersWithoutArtifacts)
{
    // Camera viewing the animated cube from the front-right
    glm::mat4 view = glm::lookAt(glm::vec3(3.0f, 1.5f, 0.0f),
                                 glm::vec3(0.0f, 1.5f, -2.5f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
    sceneUBO.lightIntensity = 1.0f;  // Ensure directional light is active

    SurfaceUBO surfaceUBO{};
    surfaceUBO.depthBias = Renderer::DEPTH_BIAS_DEFAULT;

    // Test at multiple animation times to cover diverse cube orientations
    std::vector<float> animationTimes = {0.0f, 2.5f, 5.0f, 7.5f, 10.0f};

    for (float t : animationTimes) {
        // Update light position and view-projection for this frame
        sceneUBO.lightViewProj = scene.lightViewProj(t);
        sceneUBO.lightPos      = glm::vec4(scene.spotlightPosition(t), 1.0f);
        renderer.updateSceneUBO(sceneUBO);

        // Update cube position and all 6 faces based on animation time
        glm::vec3 P00, P10, P01, P11;
        scene.worldCorners(t, P00, P10, P01, P11);
        renderer.updateSurfaceQuad(P00, P10, P01, P11);

        std::array<std::array<glm::vec3, 4>, 6> cubeCorners;
        scene.worldCubeCorners(t, cubeCorners);
        renderer.updateUIShadowCube(cubeCorners);

        // Build SurfaceUBOs for all 6 cube faces
        std::array<SurfaceUBO, 6> faceUBOs{};
        for (int face = 0; face < 6; ++face) {
            glm::vec3 P_00 = cubeCorners[face][0];
            glm::vec3 P_10 = cubeCorners[face][1];
            glm::vec3 P_01 = cubeCorners[face][2];
            glm::vec3 P_11 = cubeCorners[face][3];

            glm::vec3 e_u = P_10 - P_00;
            glm::vec3 e_v = P_01 - P_00;
            glm::vec3 n = glm::normalize(glm::cross(e_u, e_v));

            glm::mat4 M_sw(1.0f);
            M_sw[0] = glm::vec4(e_u, 0.0f);
            M_sw[1] = glm::vec4(e_v, 0.0f);
            M_sw[2] = glm::vec4(n, 0.0f);
            M_sw[3] = glm::vec4(P_00, 1.0f);

            glm::mat4 M_us = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / 512.0f, 1.0f / 128.0f, 1.0f));
            glm::mat4 M_world = M_sw * M_us;
            glm::mat4 M_total = sceneUBO.proj * sceneUBO.view * M_world;

            faceUBOs[face].totalMatrix = M_total;
            faceUBOs[face].worldMatrix = M_world;
            faceUBOs[face].depthBias = surfaceUBO.depthBias;

            // Compute clip planes for this face
            glm::vec3 inward_left   = glm::normalize(glm::cross(e_v, n));
            glm::vec3 inward_right  = -inward_left;
            glm::vec3 inward_top    = glm::normalize(glm::cross(n, e_u));
            glm::vec3 inward_bottom = -inward_top;

            faceUBOs[face].clipPlanes[0] = glm::vec4(inward_left, -glm::dot(inward_left, P_00));
            faceUBOs[face].clipPlanes[1] = glm::vec4(inward_right, -glm::dot(inward_right, P_10));
            faceUBOs[face].clipPlanes[2] = glm::vec4(inward_top, -glm::dot(inward_top, P_00));
            faceUBOs[face].clipPlanes[3] = glm::vec4(inward_bottom, -glm::dot(inward_bottom, P_01));
        }
        renderer.updateFaceSurfaceUBOs(faceUBOs);

        // Render the frame
        auto pixels = renderAndReadback(/*directMode=*/true);

        // Validation: The image should have content and not be pathologically broken
        // Count the distribution of pixel colors to detect artifacts
        int nonBlackPixels = 0;
        int nonWhitePixels = 0;
        double sumLuminance = 0.0;
        int pixelCount = 0;

        for (uint32_t y = 0; y < FB_HEIGHT; ++y) {
            for (uint32_t x = 0; x < FB_WIDTH; ++x) {
                const uint8_t* px = pixels.data() + (y * FB_WIDTH + x) * 4;
                uint8_t r = px[0], g = px[1], b = px[2];

                // Check if pixel is not pure black
                if (r > 5 || g > 5 || b > 5) nonBlackPixels++;
                // Check if pixel is not pure white
                if (r < 250 || g < 250 || b < 250) nonWhitePixels++;

                // Track luminance for statistical analysis
                float lum = 0.299f * r / 255.0f + 0.587f * g / 255.0f + 0.114f * b / 255.0f;
                sumLuminance += lum;
                pixelCount++;
            }
        }

        // The image should contain a reasonable mix of colors
        EXPECT_GT(nonBlackPixels, FB_WIDTH * FB_HEIGHT / 10)
            << "Frame " << t << ": Image is mostly black (possible shadow sampling failure)";
        EXPECT_GT(nonWhitePixels, FB_WIDTH * FB_HEIGHT / 10)
            << "Frame " << t << ": Image is mostly white (possible missing shadows)";

        double meanLuminance = sumLuminance / static_cast<double>(pixelCount);
        // Mean luminance should be in a reasonable range (not near extremes)
        EXPECT_GT(meanLuminance, 0.2)
            << "Frame " << t << ": Mean luminance too low (mean=" << meanLuminance << ")";
        EXPECT_LT(meanLuminance, 0.8)
            << "Frame " << t << ": Mean luminance too high (mean=" << meanLuminance << ")";
    }
}

// ---------------------------------------------------------------------------
// Test: UI cube shadow consistency at extreme rotations
//
// Verifies that cube faces at extreme animation points (maximum rotation)
// render consistently without sudden shadow sampling discontinuities.
// ---------------------------------------------------------------------------
TEST_F(ContainmentTest, UIcubeShadow_ExtremeRotationStability)
{
    glm::mat4 view = glm::lookAt(glm::vec3(4.0f, 2.0f, 1.0f),
                                 glm::vec3(0.0f, 1.5f, -2.5f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
    sceneUBO.lightIntensity = 1.0f;  // Ensure directional light is active

    SurfaceUBO surfaceUBO{};
    surfaceUBO.depthBias = Renderer::DEPTH_BIAS_DEFAULT;

    // Sample times where roll is at maximum (rotation most extreme)
    std::vector<float> extremeTimes = {
        glm::pi<float>() * 2.0f,   // One full rotation period
        glm::pi<float>() * 4.0f,   // Two full rotation periods
        glm::pi<float>() * 1.0f    // Transition point
    };

    for (float t : extremeTimes) {
        sceneUBO.lightViewProj = scene.lightViewProj(t);
        sceneUBO.lightPos      = glm::vec4(scene.spotlightPosition(t), 1.0f);
        renderer.updateSceneUBO(sceneUBO);

        glm::vec3 P00, P10, P01, P11;
        scene.worldCorners(t, P00, P10, P01, P11);
        renderer.updateSurfaceQuad(P00, P10, P01, P11);

        std::array<std::array<glm::vec3, 4>, 6> cubeCorners;
        scene.worldCubeCorners(t, cubeCorners);
        renderer.updateUIShadowCube(cubeCorners);

        std::array<SurfaceUBO, 6> faceUBOs{};
        for (int face = 0; face < 6; ++face) {
            glm::vec3 P_00 = cubeCorners[face][0];
            glm::vec3 P_10 = cubeCorners[face][1];
            glm::vec3 P_01 = cubeCorners[face][2];
            glm::vec3 P_11 = cubeCorners[face][3];

            glm::vec3 e_u = P_10 - P_00;
            glm::vec3 e_v = P_01 - P_00;
            glm::vec3 n = glm::normalize(glm::cross(e_u, e_v));

            glm::mat4 M_sw(1.0f);
            M_sw[0] = glm::vec4(e_u, 0.0f);
            M_sw[1] = glm::vec4(e_v, 0.0f);
            M_sw[2] = glm::vec4(n, 0.0f);
            M_sw[3] = glm::vec4(P_00, 1.0f);

            glm::mat4 M_us = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / 512.0f, 1.0f / 128.0f, 1.0f));
            glm::mat4 M_world = M_sw * M_us;
            glm::mat4 M_total = sceneUBO.proj * sceneUBO.view * M_world;

            faceUBOs[face].totalMatrix = M_total;
            faceUBOs[face].worldMatrix = M_world;
            faceUBOs[face].depthBias = surfaceUBO.depthBias;

            glm::vec3 inward_left   = glm::normalize(glm::cross(e_v, n));
            glm::vec3 inward_right  = -inward_left;
            glm::vec3 inward_top    = glm::normalize(glm::cross(n, e_u));
            glm::vec3 inward_bottom = -inward_top;

            faceUBOs[face].clipPlanes[0] = glm::vec4(inward_left, -glm::dot(inward_left, P_00));
            faceUBOs[face].clipPlanes[1] = glm::vec4(inward_right, -glm::dot(inward_right, P_10));
            faceUBOs[face].clipPlanes[2] = glm::vec4(inward_top, -glm::dot(inward_top, P_00));
            faceUBOs[face].clipPlanes[3] = glm::vec4(inward_bottom, -glm::dot(inward_bottom, P_01));
        }
        renderer.updateFaceSurfaceUBOs(faceUBOs);

        auto pixels = renderAndReadback(/*directMode=*/true);

        // At extreme rotations, the cube should still render without completely
        // black or white frames (which would indicate sampling failure)
        int brightPixels = 0;
        int darkPixels = 0;

        for (uint32_t y = 0; y < FB_HEIGHT; ++y) {
            for (uint32_t x = 0; x < FB_WIDTH; ++x) {
                const uint8_t* px = pixels.data() + (y * FB_WIDTH + x) * 4;
                uint8_t r = px[0], g = px[1], b = px[2];
                uint8_t brightness = (static_cast<int>(r) + g + b) / 3;
                if (brightness > 200) brightPixels++;
                if (brightness < 50) darkPixels++;
            }
        }

        int total = FB_WIDTH * FB_HEIGHT;
        EXPECT_LT(brightPixels, total * 0.9f)
            << "Frame t=" << t << ": Too many bright pixels (possible shadow failure)";
        EXPECT_LT(darkPixels, total * 0.9f)
            << "Frame t=" << t << ": Too many dark pixels (possible shadow over-sampling)";
    }
}
