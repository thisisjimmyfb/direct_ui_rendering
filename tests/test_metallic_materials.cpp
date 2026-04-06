#include <gtest/gtest.h>
#include "scene.h"
#include "renderer.h"
#include "containment_fixture.h"
#include "test_pixel_helpers.h"

#include <algorithm>
#include <cmath>

using namespace TestPixelHelpers;

// ---------------------------------------------------------------------------
// Metallic Material Tests — Verify material variety with metallic surfaces
// ---------------------------------------------------------------------------

class MetallicMaterialTest : public ContainmentTest {
};

// Test 1: Verify that material definitions include metallic surfaces
TEST_F(MetallicMaterialTest, MaterialDefinitions_IncludeMetallicSurfaces) {
    const MaterialDefinition* mats = Scene::surfaceMaterials();
    int count = Scene::surfaceMaterialCount();

    EXPECT_EQ(count, 6) << "Should have 6 material definitions";

    bool hasMetallic = false;
    for (int i = 0; i < count; ++i) {
        if (mats[i].material.metallic > 0.3f) {
            hasMetallic = true;
            break;
        }
    }

    EXPECT_TRUE(hasMetallic)
        << "Material definitions should include at least one metallic surface (metallic > 0.3)";
}

// Test 2: Verify metallic materials exist in room mesh
TEST_F(MetallicMaterialTest, RoomMesh_ContainsMetallicVertices) {
    const RoomMesh& mesh = scene.roomMesh();
    EXPECT_GT(mesh.vertices.size(), 0);

    bool foundMetallic = false;
    for (const auto& v : mesh.vertices) {
        if (v.material.metallic > 0.3f) {
            foundMetallic = true;
            break;
        }
    }

    EXPECT_TRUE(foundMetallic)
        << "Room mesh should contain metallic vertices (metallic > 0.3)";
}

// Test 3: Verify material variety exists (mix of metallic and dielectric)
TEST_F(MetallicMaterialTest, MaterialVariety_HasMetallicAndDielectric) {
    const MaterialDefinition* mats = Scene::surfaceMaterials();
    int count = Scene::surfaceMaterialCount();

    int metallicCount = 0;
    int dielectricCount = 0;

    for (int i = 0; i < count; ++i) {
        if (mats[i].material.metallic > 0.3f) {
            metallicCount++;
        } else {
            dielectricCount++;
        }
    }

    EXPECT_GE(metallicCount, 1)
        << "Should have at least one metallic surface";
    EXPECT_GE(dielectricCount, 2)
        << "Should still have dielectric surfaces for variety";
}

// Test 4: Verify metallic and dielectric surfaces have different properties
TEST_F(MetallicMaterialTest, MetallicVsDielectric_DifferentProperties) {
    const MaterialDefinition* mats = Scene::surfaceMaterials();
    int count = Scene::surfaceMaterialCount();

    float minMetallic = 1.0f, maxMetallic = 0.0f;
    for (int i = 0; i < count; ++i) {
        minMetallic = std::min(minMetallic, mats[i].material.metallic);
        maxMetallic = std::max(maxMetallic, mats[i].material.metallic);
    }

    EXPECT_LT(minMetallic, maxMetallic)
        << "Should have variation in metallic values across surfaces";
}

// Test 5: Verify metallic materials render without crashing
TEST_F(MetallicMaterialTest, MetallicMaterials_RenderWithoutCrashing) {
    // Simply verify that metallic materials can be rendered
    std::vector<uint8_t> pixels = renderAndReadback(/*directMode=*/true);

    // Check that we got a valid framebuffer (non-empty pixels)
    EXPECT_GT(pixels.size(), 0)
        << "Should produce a valid framebuffer with metallic materials";

    // Count any non-black pixels
    int nonBlackPixels = 0;
    for (uint32_t i = 0; i < pixels.size(); i += 4) {
        if (pixels[i] + pixels[i+1] + pixels[i+2] > 0) {
            nonBlackPixels++;
        }
    }

    EXPECT_GT(nonBlackPixels, 0)
        << "Should render some lit pixels with metallic materials";
}

// Test 6: Verify metallic values are in valid range
TEST_F(MetallicMaterialTest, MetallicValues_InValidRange) {
    const MaterialDefinition* mats = Scene::surfaceMaterials();
    int count = Scene::surfaceMaterialCount();

    for (int i = 0; i < count; ++i) {
        EXPECT_GE(mats[i].material.metallic, 0.0f)
            << "Metallic value should be >= 0";
        EXPECT_LE(mats[i].material.metallic, 1.0f)
            << "Metallic value should be <= 1";
        EXPECT_FALSE(std::isnan(mats[i].material.metallic))
            << "Metallic value should not be NaN";
    }
}

// Test 7: Verify metallic surfaces in both render modes
TEST_F(MetallicMaterialTest, MetallicMaterials_WorkInBothRenderModes) {
    // Direct mode
    std::vector<uint8_t> directPixels = renderAndReadback(/*directMode=*/true);

    // Traditional mode
    std::vector<uint8_t> tradPixels = renderAndReadback(/*directMode=*/false);

    // Both should render without issues
    bool directHasPixels = false, tradHasPixels = false;

    for (uint32_t i = 0; i < directPixels.size(); i += 4) {
        if (directPixels[i] + directPixels[i+1] + directPixels[i+2] > 30) {
            directHasPixels = true;
        }
        if (tradPixels[i] + tradPixels[i+1] + tradPixels[i+2] > 30) {
            tradHasPixels = true;
        }
    }

    EXPECT_TRUE(directHasPixels) << "Direct mode should render correctly with metallic materials";
    EXPECT_TRUE(tradHasPixels) << "Traditional mode should render correctly with metallic materials";
}

// Test 8: Verify metallic material names are descriptive
TEST_F(MetallicMaterialTest, MetallicMaterials_HaveDescriptiveNames) {
    const MaterialDefinition* mats = Scene::surfaceMaterials();
    int count = Scene::surfaceMaterialCount();

    for (int i = 0; i < count; ++i) {
        EXPECT_NE(mats[i].name, nullptr);
        EXPECT_GT(std::strlen(mats[i].name), 0);
    }
}
