#include "containment_fixture.h"
#include <algorithm>

// ---------------------------------------------------------------------------
// Wall Color Tests
// Validates that wall colors specified in room.frag are correctly rendered
// in both direct and traditional rendering modes.
// ---------------------------------------------------------------------------

// Helper to check if a color matches an expected color with tolerance.
// Colors are in sRGB (0-255 range).
static bool colorMatch(uint8_t r, uint8_t g, uint8_t b,
                      uint8_t expectedR, uint8_t expectedG, uint8_t expectedB,
                      uint8_t tolerance = 40)
{
    int dr = static_cast<int>(r) - expectedR;
    int dg = static_cast<int>(g) - expectedG;
    int db = static_cast<int>(b) - expectedB;

    return std::abs(dr) <= tolerance && std::abs(dg) <= tolerance && std::abs(db) <= tolerance;
}

// Helper to convert linear float color (0-1) to sRGB (0-255)
static void linearToSRGB(uint8_t& r, uint8_t& g, uint8_t& b,
                         float linearR, float linearG, float linearB)
{
    // Apply gamma correction (approximate sRGB)
    auto toSRGB = [](float linear) -> uint8_t {
        if (linear <= 0.0f) return 0;
        if (linear >= 1.0f) return 255;
        float gamma = linear < 0.0031308f ? 12.92f * linear : (1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f);
        return static_cast<uint8_t>(std::clamp(gamma * 255.0f, 0.0f, 255.0f));
    };
    r = toSRGB(linearR);
    g = toSRGB(linearG);
    b = toSRGB(linearB);
}

// Sample average color in a rectangular region of the readback
static void sampleAverageColor(const std::vector<uint8_t>& pixels,
                               uint32_t fbWidth, uint32_t fbHeight,
                               uint32_t x0, uint32_t y0,
                               uint32_t x1, uint32_t y1,
                               uint8_t& outR, uint8_t& outG, uint8_t& outB)
{
    uint32_t sumR = 0, sumG = 0, sumB = 0;
    uint32_t count = 0;

    x0 = std::max(0u, std::min(x0, fbWidth - 1));
    y0 = std::max(0u, std::min(y0, fbHeight - 1));
    x1 = std::max(0u, std::min(x1, fbWidth - 1));
    y1 = std::max(0u, std::min(y1, fbHeight - 1));

    for (uint32_t y = y0; y <= y1; ++y) {
        for (uint32_t x = x0; x <= x1; ++x) {
            const uint8_t* px = pixels.data() + (y * fbWidth + x) * 4;
            sumR += px[0];
            sumG += px[1];
            sumB += px[2];
            ++count;
        }
    }

    if (count > 0) {
        outR = static_cast<uint8_t>(sumR / count);
        outG = static_cast<uint8_t>(sumG / count);
        outB = static_cast<uint8_t>(sumB / count);
    } else {
        outR = outG = outB = 0;
    }
}

// Test fixture that extends ContainmentTest to add wall color testing utilities
class WallColorTest : public ContainmentTest {
protected:
    // Camera positioned inside the room looking at walls
    glm::mat4 setupWallViewCamera() {
        return glm::lookAt(glm::vec3(0.0f, 1.5f, 0.5f),
                          glm::vec3(0.0f, 1.5f, -2.8f),
                          glm::vec3(0.0f, 1.0f, 0.0f));
    }

    // Standard projection
    glm::mat4 setupProjection() {
        glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                         static_cast<float>(FB_WIDTH) / FB_HEIGHT,
                                         0.1f, 100.0f);
        proj[1][1] *= -1.0f;
        return proj;
    }

    // Render room only (no UI) and return readback pixels
    std::vector<uint8_t> renderRoomOnly(bool directMode, const glm::mat4& view, const glm::mat4& proj) {
        SceneUBO sceneUBO = makeSpotlightSceneUBO(scene, view, proj);
        renderer.updateSceneUBO(sceneUBO);

        VkBuffer      readbackBuf   = VK_NULL_HANDLE;
        VmaAllocation readbackAlloc = VK_NULL_HANDLE;
        const VkDeviceSize readbackSize = static_cast<VkDeviceSize>(FB_WIDTH) * FB_HEIGHT * 4;
        {
            VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bci.size        = readbackSize;
            bci.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_CPU_ONLY;
            vmaCreateBuffer(renderer.getAllocator(), &bci, &ai,
                            &readbackBuf, &readbackAlloc, nullptr);
        }

        VkCommandBufferAllocateInfo cbAI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cbAI.commandPool        = renderer.getCommandPool();
        cbAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbAI.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(renderer.getDevice(), &cbAI, &cmd);

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        // Render shadow pass and room geometry only
        renderer.recordShadowPass(cmd);
        renderer.recordMainPass(cmd, hrt.rt, directMode, VK_NULL_HANDLE, 0);

        vku::imageBarrier(cmd, hrt.rt.image,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkBufferImageCopy copyRegion{};
        copyRegion.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.imageExtent       = {FB_WIDTH, FB_HEIGHT, 1};
        vkCmdCopyImageToBuffer(cmd, hrt.rt.image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readbackBuf, 1, &copyRegion);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &cmd;
        vkQueueSubmit(renderer.getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(renderer.getGraphicsQueue());

        void* mapped = nullptr;
        vmaMapMemory(renderer.getAllocator(), readbackAlloc, &mapped);
        std::vector<uint8_t> pixels(readbackSize);
        memcpy(pixels.data(), mapped, static_cast<size_t>(readbackSize));
        vmaUnmapMemory(renderer.getAllocator(), readbackAlloc);

        vkFreeCommandBuffers(renderer.getDevice(), renderer.getCommandPool(), 1, &cmd);
        vmaDestroyBuffer(renderer.getAllocator(), readbackBuf, readbackAlloc);

        return pixels;
    }
};

// ---------------------------------------------------------------------------
// Test 1: Back wall color (cyan) in direct mode
// ---------------------------------------------------------------------------
TEST_F(WallColorTest, DirectMode_BackWall_CyanColor)
{
    glm::mat4 view = setupWallViewCamera();
    glm::mat4 proj = setupProjection();

    auto pixels = renderRoomOnly(/*directMode=*/true, view, proj);

    // Sample a region in the back portion of the framebuffer (back wall should be visible)
    // Back wall is at Z < -2.8, camera is at Z = 0.5, so back wall is behind
    // Sample center-top region where back wall should appear
    uint8_t r, g, b;
    sampleAverageColor(pixels, FB_WIDTH, FB_HEIGHT,
                       FB_WIDTH / 2 - 100, FB_HEIGHT / 4 - 50,
                       FB_WIDTH / 2 + 100, FB_HEIGHT / 4 + 50,
                       r, g, b);

    // Expected cyan color from shader: vec3(0.45, 0.75, 0.9)
    // With lighting, this will be modulated, but cyan hue should remain dominant
    uint8_t expectedR, expectedG, expectedB;
    linearToSRGB(expectedR, expectedG, expectedB, 0.45f, 0.75f, 0.9f);

    // Use larger tolerance for lit geometry
    EXPECT_TRUE(colorMatch(r, g, b, expectedR, expectedG, expectedB, 60))
        << "Back wall color mismatch. Got: (" << (int)r << ", " << (int)g << ", " << (int)b << ")"
        << " Expected: (" << (int)expectedR << ", " << (int)expectedG << ", " << (int)expectedB << ")";
}


// ---------------------------------------------------------------------------
// Test 5: Back wall color (cyan) in traditional mode
// ---------------------------------------------------------------------------
TEST_F(WallColorTest, TraditionalMode_BackWall_CyanColor)
{
    glm::mat4 view = setupWallViewCamera();
    glm::mat4 proj = setupProjection();

    auto pixels = renderRoomOnly(/*directMode=*/false, view, proj);

    // Sample center-top region where back wall should appear
    uint8_t r, g, b;
    sampleAverageColor(pixels, FB_WIDTH, FB_HEIGHT,
                       FB_WIDTH / 2 - 100, FB_HEIGHT / 4 - 50,
                       FB_WIDTH / 2 + 100, FB_HEIGHT / 4 + 50,
                       r, g, b);

    // Expected cyan color from shader: vec3(0.45, 0.75, 0.9)
    uint8_t expectedR, expectedG, expectedB;
    linearToSRGB(expectedR, expectedG, expectedB, 0.45f, 0.75f, 0.9f);

    EXPECT_TRUE(colorMatch(r, g, b, expectedR, expectedG, expectedB, 60))
        << "Back wall color mismatch (traditional mode). Got: (" << (int)r << ", " << (int)g << ", " << (int)b << ")"
        << " Expected: (" << (int)expectedR << ", " << (int)expectedG << ", " << (int)expectedB << ")";
}

