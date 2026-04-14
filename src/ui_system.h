#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

// UI canvas constants
static constexpr uint32_t W_UI       = 512;
static constexpr uint32_t H_UI       = 128;
static constexpr uint32_t ATLAS_SIZE = 512;
static constexpr uint32_t GLYPH_CELL = 32;

// SDF atlas constants
static constexpr uint8_t SDF_ON_EDGE_VALUE    = 128;   // distance field edge value
static constexpr float   SDF_PIXEL_DIST_SCALE = 16.0f; // pixels per SDF distance unit
static constexpr int     SDF_GLYPH_PADDING    = 4;     // border around each glyph for SDF bleed
static constexpr float   SDF_THRESHOLD_DEFAULT = 0.5f; // normalized threshold = 128/255

static_assert(SDF_GLYPH_PADDING > 0,
    "SDF_GLYPH_PADDING must be positive for SDF bleed beyond glyph outline");
static_assert(SDF_PIXEL_DIST_SCALE >= 1.0f && SDF_PIXEL_DIST_SCALE <= 100.0f,
    "SDF_PIXEL_DIST_SCALE must be in [1, 100] for meaningful per-pixel resolution");
static_assert(SDF_THRESHOLD_DEFAULT >= static_cast<float>(SDF_ON_EDGE_VALUE) / 255.0f - 0.01f &&
              SDF_THRESHOLD_DEFAULT <= static_cast<float>(SDF_ON_EDGE_VALUE) / 255.0f + 0.01f,
    "SDF_THRESHOLD_DEFAULT must be within 0.01 of SDF_ON_EDGE_VALUE/255");

// A single UI vertex: 2D UI-space position + UV into glyph atlas.
struct UIVertex {
    glm::vec2 pos;   // UI-space pixels, origin top-left
    glm::vec2 uv;    // normalized UV into the 512x512 atlas
};

// UV rectangle for one glyph in the atlas.
struct GlyphRect {
    float u0, v0;  // top-left UV
    float u1, v1;  // bottom-right UV
};

// UISystem manages the glyph atlas texture, the ASCII UV lookup table,
// and the device-local vertex buffer for "Hello World".
class UISystem {
public:
    // Callable that loads raw bytes for a named asset (e.g. "assets/atlas.png").
    // Returns empty vector if the asset cannot be found.
    // On desktop: UISystem::makeFileAssetLoader() provides a filesystem-based loader.
    // On Android: the caller provides an AAssetManager-backed loader.
    using AssetLoader = std::function<std::vector<uint8_t>(const char* path)>;

    // Returns a loader that reads assets from the filesystem via fopen.
    static AssetLoader makeFileAssetLoader();

    // Upload atlas PNG and tessellate the "Hello World" vertex buffer.
    // cmdPool/queue used for staging uploads.
    // assetLoader is called with "assets/atlas.png" when SDF generation fails.
    bool init(VmaAllocator allocator,
              VkDevice device,
              VkCommandPool cmdPool,
              VkQueue queue,
              AssetLoader assetLoader);

    void cleanup();

    // Populate m_glyphTable from atlas layout constants (pure CPU, no Vulkan).
    // Called automatically by init(); also callable standalone for unit tests.
    void buildGlyphTable();

    // Return the UV rect for a printable ASCII character (32–126).
    // Characters outside this range are clamped to the space glyph.
    GlyphRect uvForChar(char c) const;

    // Rebuild HUD vertex data from a formatted string (called each frame for metrics).
    // Returns the number of vertices written.
    uint32_t tessellateString(std::string_view text, float x, float y,
                              std::vector<UIVertex>& outVerts) const;

    VkImageView  atlasView()      const { return m_atlasView; }
    VkSampler    atlasSampler()   const { return m_atlasSampler; }
    VkBuffer     helloVertBuffer() const { return m_helloVtxBuf; }
    uint32_t     helloVertCount() const { return m_helloVertCount; }
    bool         isSDF()             const { return m_sdfMode; }
    float        sdfThreshold()      const { return m_sdfMode ? SDF_THRESHOLD_DEFAULT : 0.0f; }
    bool         isGlyphTableBuilt() const { return m_glyphTableBuilt; }

private:

    VmaAllocator m_allocator{VK_NULL_HANDLE};
    VkDevice     m_device{VK_NULL_HANDLE};

    // Glyph atlas (device-local image)
    VkImage       m_atlasImage{VK_NULL_HANDLE};
    VmaAllocation m_atlasAlloc{VK_NULL_HANDLE};
    VkImageView   m_atlasView{VK_NULL_HANDLE};
    VkSampler     m_atlasSampler{VK_NULL_HANDLE};

    // "Hello World" vertex buffer (device-local, uploaded once at init)
    VkBuffer      m_helloVtxBuf{VK_NULL_HANDLE};
    VmaAllocation m_helloVtxAlloc{VK_NULL_HANDLE};
    uint32_t      m_helloVertCount{0};

    // ASCII UV lookup: index = (char - 32), covers printable ASCII [32..126]
    std::array<GlyphRect, 95> m_glyphTable{};

    bool m_sdfMode{false};
    bool m_glyphTableBuilt{false};
};
