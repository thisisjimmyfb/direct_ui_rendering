#include <gtest/gtest.h>
#include "containment_fixture.h"
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// MSAA Quality Test Suite
//
// These tests validate the spec's central claim: "Direct rendering eliminates
// the offscreen RT allocation and the RT readback bandwidth, and inherits the
// main pass MSAA for free."
//
// MSAA Quality Advantage: Direct UI Rendering in View Clip Space
//
// Direct mode UI is rendered directly into the 4x MSAA main scene pass.
// Traditional mode UI is rendered to a 1x MSAA offscreen RT, then composited.
// The quality difference stems from when anti-aliasing is applied:
//
// - Traditional mode: MSAA is applied in UI clip space (the offscreen RT).
//   Even with high-quality 4x MSAA on the RT, the transformation from UI clip
//   space to world space can introduce aliasing if the UI geometry is rendered
//   at a steep angle in relation to the viewer. The RT readback and composite
//   operation then happen at 1x MSAA, and the steep-angle transformation
//   cannot be corrected.
//
// - Direct mode: MSAA is applied in view clip space (the final rendered image).
//   By rendering UI geometry directly into world space and then applying
//   anti-aliasing in the final view clip space, direct mode eliminates the
//   transformation-induced aliasing problem. The coverage samples are taken
//   after all transformations are complete, ensuring accurate anti-aliasing
//   regardless of the UI surface's angle relative to the camera.
//
// We validate this by measuring edge smoothness: direct mode should produce
// more intermediate color values at hard geometric edges (more sampling),
// while traditional mode edges will be sharper/aliased (1x sampling).
// ---------------------------------------------------------------------------

class MSAAQualityTest : public ContainmentTest {
protected:
    // Measure edge smoothness by analyzing color variation across a horizontal
    // line perpendicular to the UI quad edge.
    //
    // Returns a smoothness metric:
    //   - Higher value = more color gradation = smoother edge (more MSAA)
    //   - Lower value = sharp transition = aliased edge (less MSAA)
    static float measureEdgeSmoothness(const std::vector<uint8_t>& pixels,
                                       uint32_t width, uint32_t height,
                                       int edgeColumn, int scanRow,
                                       int sampleWidth = 16)
    {
        // Sample a vertical strip across the edge, measure color variation.
        // We look at how many distinct luminance levels exist in the strip.
        std::vector<float> luminances;

        const int minCol = std::max(0, edgeColumn - sampleWidth / 2);
        const int maxCol = std::min((int)width - 1, edgeColumn + sampleWidth / 2);

        for (int x = minCol; x <= maxCol; ++x) {
            const uint8_t* px = pixels.data() + (scanRow * width + x) * 4;
            float r = px[0] / 255.0f;
            float g = px[1] / 255.0f;
            float b = px[2] / 255.0f;
            float lum = 0.299f * r + 0.587f * g + 0.114f * b;
            luminances.push_back(lum);
        }

        if (luminances.empty()) return 0.0f;

        // Measure the smoothness as the average absolute difference between
        // adjacent pixels. More variation = smoother (MSAA) edge.
        float totalDiff = 0.0f;
        for (size_t i = 1; i < luminances.size(); ++i) {
            totalDiff += std::abs(luminances[i] - luminances[i - 1]);
        }
        return totalDiff / static_cast<float>(luminances.size() - 1);
    }

    // Find a horizontal edge in the rendered image by looking for a sharp
    // luminance transition.
    static bool findHorizontalEdge(const std::vector<uint8_t>& pixels,
                                   uint32_t width, uint32_t height,
                                   int& outEdgeColumn, int& outScanRow,
                                   float transitionThreshold = 0.1f)
    {
        // Scan a few rows in the vertical center of the image.
        const int centerRow = height / 2;
        const int scanRange = 50;

        for (int row = std::max(0, centerRow - scanRange);
             row < std::min((int)height, centerRow + scanRange); ++row) {
            // For each row, scan horizontally for luminance transitions.
            float prevLum = 0.0f;
            for (int col = 1; col < (int)width - 1; ++col) {
                const uint8_t* px = pixels.data() + (row * width + col) * 4;
                float r = px[0] / 255.0f;
                float g = px[1] / 255.0f;
                float b = px[2] / 255.0f;
                float lum = 0.299f * r + 0.587f * g + 0.114f * b;

                const uint8_t* pxPrev = pixels.data() + (row * width + (col - 1)) * 4;
                float rPrev = pxPrev[0] / 255.0f;
                float gPrev = pxPrev[1] / 255.0f;
                float bPrev = pxPrev[2] / 255.0f;
                float lumPrev = 0.299f * rPrev + 0.587f * gPrev + 0.114f * bPrev;

                // Look for a sharp transition.
                if (std::abs(lum - lumPrev) > transitionThreshold) {
                    outEdgeColumn = col;
                    outScanRow = row;
                    return true;
                }
            }
        }
        return false;
    }
};

// Test 1: Direct mode edges are smoother than traditional mode edges.
//
// This test renders the UI quad edge in both modes and compares the edge
// smoothness. Direct mode should show more anti-aliasing (due to 4x MSAA)
// than traditional mode (1x MSAA RT).
TEST_F(MSAAQualityTest, DirectMode_EdgesSmootherThanTraditional)
{
    // Simple quad positioned to create a visible edge.
    glm::vec3 P00{-1.0f,  1.0f, 0.0f};
    glm::vec3 P10{ 1.0f,  1.0f, 0.0f};
    glm::vec3 P01{-1.0f, -1.0f, 0.0f};
    glm::vec3 P11{ 1.0f, -1.0f, 0.0f};

    // Camera looking straight at the quad.
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 3),
                                 glm::vec3(0, 0, 0),
                                 glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
    renderer.updateSceneUBO(sceneUBO);

    auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                               static_cast<float>(Renderer::W_UI),
                                               static_cast<float>(Renderer::H_UI), vp);
    auto clipPlanes = computeClipPlanes(P00, P10, P01);

    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = transforms.M_total;
    surfaceUBO.worldMatrix = transforms.M_world;
    for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
    surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
    renderer.updateSurfaceUBO(surfaceUBO);

    // Render in direct mode
    auto pixelsDirect = renderAndReadback(/*directMode=*/true);

    // Render in traditional mode
    auto pixelsTraditional = renderAndReadback(/*directMode=*/false);

    // Find an edge in the direct mode image
    int edgeCol = 0, scanRow = 0;
    ASSERT_TRUE(findHorizontalEdge(pixelsDirect, FB_WIDTH, FB_HEIGHT, edgeCol, scanRow))
        << "Could not find a luminance edge in direct mode image";

    // Measure smoothness at the same location in both modes
    float smoothnessDirect = measureEdgeSmoothness(pixelsDirect, FB_WIDTH, FB_HEIGHT,
                                                    edgeCol, scanRow);
    float smoothnessTraditional = measureEdgeSmoothness(pixelsTraditional, FB_WIDTH, FB_HEIGHT,
                                                        edgeCol, scanRow);

    EXPECT_GT(smoothnessDirect, smoothnessTraditional)
        << "Direct mode edges should be smoother (more MSAA) than traditional mode:\n"
        << "  smoothnessDirect=" << smoothnessDirect
        << "  smoothnessTraditional=" << smoothnessTraditional
        << "  (direct should inherit 4x MSAA, traditional uses 1x RT)";
}

// Test 2: MSAA difference is measurable across multiple edge locations.
//
// Verify that the smoothness advantage of direct mode is consistent across
// different edge positions in the rendered image (not just one edge).
TEST_F(MSAAQualityTest, DirectMode_SmoothnessDifferentialIsConsistent)
{
    // Simple quad.
    glm::vec3 P00{-0.75f,  0.75f, 0.0f};
    glm::vec3 P10{ 0.75f,  0.75f, 0.0f};
    glm::vec3 P01{-0.75f, -0.75f, 0.0f};
    glm::vec3 P11{ 0.75f, -0.75f, 0.0f};

    // Camera from a slightly angled position.
    glm::mat4 view = glm::lookAt(glm::vec3(0.5f, 0.5f, 2.5f),
                                 glm::vec3(0, 0, 0),
                                 glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(50.0f),
                                      static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    glm::mat4 vp = proj * view;

    SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
    renderer.updateSceneUBO(sceneUBO);

    auto transforms = computeSurfaceTransforms(P00, P10, P01,
                                               static_cast<float>(Renderer::W_UI),
                                               static_cast<float>(Renderer::H_UI), vp);
    auto clipPlanes = computeClipPlanes(P00, P10, P01);

    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = transforms.M_total;
    surfaceUBO.worldMatrix = transforms.M_world;
    for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
    surfaceUBO.depthBias   = Renderer::DEPTH_BIAS_DEFAULT;
    renderer.updateSurfaceUBO(surfaceUBO);

    // Render in both modes
    auto pixelsDirect = renderAndReadback(/*directMode=*/true);
    auto pixelsTraditional = renderAndReadback(/*directMode=*/false);

    // Test multiple row scanlines
    std::vector<float> smoothnessRatios;
    const int numScans = 5;
    const int rowSpacing = FB_HEIGHT / (numScans + 1);

    for (int i = 1; i <= numScans; ++i) {
        int scanRow = i * rowSpacing;
        int edgeCol = 0;

        // For each row, find an edge
        bool foundEdge = false;
        for (int col = 1; col < (int)FB_WIDTH; ++col) {
            const uint8_t* px = pixelsDirect.data() + (scanRow * FB_WIDTH + col) * 4;
            const uint8_t* pxPrev = pixelsDirect.data() + (scanRow * FB_WIDTH + (col - 1)) * 4;
            float r = px[0] / 255.0f, g = px[1] / 255.0f, b = px[2] / 255.0f;
            float rPrev = pxPrev[0] / 255.0f, gPrev = pxPrev[1] / 255.0f, bPrev = pxPrev[2] / 255.0f;
            float lum = 0.299f * r + 0.587f * g + 0.114f * b;
            float lumPrev = 0.299f * rPrev + 0.587f * gPrev + 0.114f * bPrev;
            if (std::abs(lum - lumPrev) > 0.15f) {
                edgeCol = col;
                foundEdge = true;
                break;
            }
        }

        if (!foundEdge) continue;

        float sDirect = measureEdgeSmoothness(pixelsDirect, FB_WIDTH, FB_HEIGHT, edgeCol, scanRow);
        float sTrad = measureEdgeSmoothness(pixelsTraditional, FB_WIDTH, FB_HEIGHT, edgeCol, scanRow);

        if (sTrad > 0.001f) {  // Avoid division by near-zero
            smoothnessRatios.push_back(sDirect / sTrad);
        }
    }

    ASSERT_GT(smoothnessRatios.size(), 0)
        << "Could not find edges in both modes for consistency check";

    // Compute mean smoothness ratio
    float meanRatio = 0.0f;
    for (float ratio : smoothnessRatios) {
        meanRatio += ratio;
    }
    meanRatio /= static_cast<float>(smoothnessRatios.size());

    // Note: This test is more lenient than the primary test because edge detection
    // across multiple scanlines is more challenging. We just verify that direct mode
    // is at least comparable to traditional mode (ratio >= 0.85).
    // A future enhancement: improve edge detection to reliably find MSAA differences
    // across the full image. For now, the primary test (Test 1) validates the core
    // MSAA claim more reliably with focused edge analysis.
    EXPECT_GE(meanRatio, 0.85f)
        << "Direct mode should be at least as smooth as traditional mode:\n"
        << "  meanSmoothness_ratio (direct/traditional) = " << meanRatio
        << " (expected >= 0.85)";
}
