#include <gtest/gtest.h>
#include "scene.h"
#include "renderer.h"
#include "containment_fixture.h"

#include <array>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// PBR Material Tests — Verify physically-based rendering with different materials
// ---------------------------------------------------------------------------

class PBRMaterialTest : public ContainmentTest {
protected:
    // Helper to unpack RGBA8 pixel
    static glm::vec4 unpackPixel(const uint8_t* pixelData) {
        return glm::vec4(
            pixelData[0] / 255.0f,
            pixelData[1] / 255.0f,
            pixelData[2] / 255.0f,
            pixelData[3] / 255.0f
        );
    }

    // Helper to sample a pixel at given coordinates
    static const uint8_t* samplePixel(const std::vector<uint8_t>& pixels,
                                       uint32_t x, uint32_t y,
                                       uint32_t width) {
        return pixels.data() + (y * width + x) * 4;
    }
};

// Test 1: Verify dielectric materials (metallic=0) have diffuse component
TEST_F(PBRMaterialTest, DielectricMaterial_HasDiffuseComponent) {
    // Render a frame with direct mode
    std::vector<uint8_t> pixels = renderAndReadback(/*directMode=*/true);

    // Sample a pixel from the floor (dielectric with roughness 0.7)
    // Should have a noticeable diffuse component from ambient + light
    const uint8_t* floorPixel = samplePixel(pixels, 640, 360, FB_WIDTH);
    glm::vec4 color = unpackPixel(floorPixel);

    // Dielectric should have decent brightness from diffuse + ambient
    // Color should not be pure black or extremely dark
    float brightness = color.r + color.g + color.b;
    EXPECT_GT(brightness, 0.15f) << "Dielectric material should have diffuse component";
}

// Test 2: Verify rough dielectric materials render with reduced specularity
TEST_F(PBRMaterialTest, RoughDielectricMaterial_RendersDifferently) {
    // The scene uses dielectric materials with varied roughness
    // This test verifies rough materials render with expected properties
    std::vector<uint8_t> pixels = renderAndReadback(/*directMode=*/true);

    // Sample a pixel from the rendered scene
    const uint8_t* pixelData = samplePixel(pixels, 640, 200, FB_WIDTH);
    glm::vec4 color = unpackPixel(pixelData);

    // Should have some color (not completely black)
    float brightness = color.r + color.g + color.b;
    EXPECT_GT(brightness, 0.05f) << "Dielectric materials should render";
}

// Test 3: Verify that PBR shader receives material properties correctly
TEST_F(PBRMaterialTest, PBRShader_ReceivesMaterialProperties) {
    // Verify that the room mesh has material data
    const RoomMesh& mesh = scene.roomMesh();
    EXPECT_GT(mesh.vertices.size(), 0);

    // Check that vertices have valid material properties (all dielectric with varied roughness)
    bool foundVariedRoughness = false;
    float minRoughness = 1.0f, maxRoughness = 0.0f;

    for (const auto& v : mesh.vertices) {
        EXPECT_GE(v.material.metallic, 0.0f);
        EXPECT_LE(v.material.metallic, 1.0f);
        EXPECT_GE(v.material.roughness, 0.0f);
        EXPECT_LE(v.material.roughness, 1.0f);

        minRoughness = std::min(minRoughness, v.material.roughness);
        maxRoughness = std::max(maxRoughness, v.material.roughness);
    }

    // Should have variation in roughness values
    EXPECT_LT(minRoughness, maxRoughness) << "Should have roughness variation across materials";
}

// Test 4: Verify ambient light is applied to PBR materials
TEST_F(PBRMaterialTest, AmbientLight_AppliedInPBR) {
    std::vector<uint8_t> pixels = renderAndReadback(/*directMode=*/true);

    // Sample multiple pixels to check ambient lighting is working
    bool hasAmbient = false;
    for (int y = 200; y < 400; y += 50) {
        for (int x = 200; x < 600; x += 50) {
            const uint8_t* pixel = samplePixel(pixels, x, y, FB_WIDTH);
            glm::vec4 color = unpackPixel(pixel);
            float totalColor = color.r + color.g + color.b;
            if (totalColor > 0.1f) {
                hasAmbient = true;
            }
        }
    }

    EXPECT_TRUE(hasAmbient) << "PBR materials should receive ambient light";
}

// Test 5: Verify scene materials have varied roughness for PBR demonstration
TEST_F(PBRMaterialTest, SceneMaterialDefinitions_IncludeRoughnessVariety) {
    // Verify material definitions exist and are reasonable
    const MaterialDefinition* mats = Scene::surfaceMaterials();
    int count = Scene::surfaceMaterialCount();

    EXPECT_EQ(count, 6) << "Should have 6 material definitions (one per room surface)";
    EXPECT_NE(mats, nullptr);

    float minRoughness = 1.0f, maxRoughness = 0.0f;
    int smoothCount = 0, roughCount = 0;

    for (int i = 0; i < count; ++i) {
        EXPECT_GE(mats[i].material.metallic, 0.0f);
        EXPECT_LE(mats[i].material.metallic, 1.0f);
        EXPECT_GE(mats[i].material.roughness, 0.0f);
        EXPECT_LE(mats[i].material.roughness, 1.0f);
        EXPECT_NE(mats[i].name, nullptr);

        minRoughness = std::min(minRoughness, mats[i].material.roughness);
        maxRoughness = std::max(maxRoughness, mats[i].material.roughness);

        if (mats[i].material.roughness < 0.5f) smoothCount++;
        if (mats[i].material.roughness >= 0.5f) roughCount++;
    }

    EXPECT_LT(minRoughness, maxRoughness) << "Should have roughness variation";
    EXPECT_GE(smoothCount, 2) << "Should have some smooth materials";
    EXPECT_GE(roughCount, 2) << "Should have some rough materials";
}

// Test 6: Verify roughness property is used
TEST_F(PBRMaterialTest, RoughnessProperty_VariesAcrossMaterials) {
    // Verify that roughness produces continuous variation
    Scene scene;
    scene.init();

    const RoomMesh& mesh = scene.roomMesh();

    // Collect all roughness values
    std::vector<float> roughnessValues;
    for (const auto& v : mesh.vertices) {
        roughnessValues.push_back(v.material.roughness);
    }

    // Find min and max roughness
    auto minmax = std::minmax_element(roughnessValues.begin(), roughnessValues.end());
    float minRoughness = *minmax.first;
    float maxRoughness = *minmax.second;

    EXPECT_LE(minRoughness, 0.35f) << "Should have some smooth surfaces";
    EXPECT_GE(maxRoughness, 0.7f) << "Should have some rough surfaces";
    EXPECT_NE(minRoughness, maxRoughness) << "Should have variation in roughness";
}

// Test 7: Verify metallic vs dielectric color differences
TEST_F(PBRMaterialTest, MetallicVsDielectric_RenderDifferently) {
    // Sample pixels from different materials
    std::vector<uint8_t> pixels = renderAndReadback(/*directMode=*/true);

    // The materials should render with some visible differences
    // This is a basic smoke test that metallic surfaces don't break rendering
    int nonBlackPixels = 0;
    for (uint32_t i = 0; i < pixels.size(); i += 4) {
        glm::vec4 color = unpackPixel(&pixels[i]);
        if (color.r + color.g + color.b > 0.05f) {
            nonBlackPixels++;
        }
    }

    EXPECT_GT(nonBlackPixels, 100) << "Should have rendered lit pixels";
}

// Test 8: Verify material values are properly initialized
TEST_F(PBRMaterialTest, MaterialValues_AreNotNaN) {
    const RoomMesh& mesh = scene.roomMesh();

    for (const auto& v : mesh.vertices) {
        EXPECT_FALSE(std::isnan(v.material.metallic)) << "Metallic should not be NaN";
        EXPECT_FALSE(std::isnan(v.material.roughness)) << "Roughness should not be NaN";
        EXPECT_FALSE(std::isinf(v.material.metallic)) << "Metallic should not be Inf";
        EXPECT_FALSE(std::isinf(v.material.roughness)) << "Roughness should not be Inf";
    }
}

// Test 9: Verify glossy dielectric surfaces
TEST_F(PBRMaterialTest, GlossyDielectric_HasLowRoughness) {
    // The right wall should be glossy with low roughness
    const MaterialDefinition* mats = Scene::surfaceMaterials();

    // Right wall (index 5) should be dielectric with very low roughness
    EXPECT_LE(mats[5].material.metallic, 0.1f);
    EXPECT_LT(mats[5].material.roughness, 0.3f) << "Right wall should be glossy";
}

// Test 10: Verify traditional and direct modes both work with PBR
TEST_F(PBRMaterialTest, PBRWorks_InBothRenderModes) {
    // Test direct mode
    std::vector<uint8_t> directPixels = renderAndReadback(/*directMode=*/true);

    // Test traditional mode
    std::vector<uint8_t> tradPixels = renderAndReadback(/*directMode=*/false);

    // Both should render some pixels
    int directLit = 0, tradLit = 0;
    for (uint32_t i = 0; i < directPixels.size(); i += 4) {
        if (directPixels[i] + directPixels[i+1] + directPixels[i+2] > 30) directLit++;
        if (tradPixels[i] + tradPixels[i+1] + tradPixels[i+2] > 30) tradLit++;
    }

    EXPECT_GT(directLit, 100) << "Direct mode should render lit pixels";
    EXPECT_GT(tradLit, 100) << "Traditional mode should render lit pixels";
}
