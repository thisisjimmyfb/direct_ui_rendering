# Direct UI Rendering — Specification

## Table of Contents

1. [Goals](#1-goals)
2. [Demo Scene](#2-demo-scene)
3. [Rendering Modes](#3-rendering-modes)
4. [Transform Mathematics](#4-transform-mathematics)
5. [Simple UI System](#5-simple-ui-system)
6. [Vulkan Architecture](#6-vulkan-architecture)
7. [Shaders](#7-shaders)
8. [Metrics Overlay](#8-metrics-overlay)
9. [MSAA](#9-msaa)
10. [Testing](#10-testing)
11. [Build and Dependencies](#11-build-and-dependencies)

---

## 1. Goals

Build a standalone native Vulkan application that demonstrates two approaches to rendering UI onto a surface in a 3D scene:

- **Traditional approach** — render UI to an offscreen render target (RT), then composite the RT onto a quad in the scene.
- **Direct approach** — render UI geometry directly into the main scene pass using a composite transform matrix, with no offscreen RT.

The application visualizes both approaches (toggled at runtime), displays performance and memory metrics, and uses MSAA to demonstrate the anti-aliasing quality benefit of the direct approach.

The primary claim to validate:
> Direct rendering eliminates the offscreen RT allocation and the RT readback bandwidth, and inherits the main pass MSAA for free.

---

## 2. Demo Scene

### 2.1 World Geometry

A static indoor room constructed from hardcoded geometry (vertex/index data compiled into the binary):

- Floor, ceiling, and four walls — each a simple quad/two triangles.
- One light source (point or directional) that casts shadows via a shadow map.
- A moving quad (the "UI surface") parented to a transform that animates over time.

All geometry uses a simple Phong or Blinn-Phong shading model. No mesh loading from disk is required.

### 2.2 UI Surface

A rectangular quad in the scene that acts as the target surface for UI rendering. Its world-space corners `P_00`, `P_10`, `P_01` (top-left, top-right, bottom-left) define the affine frame used by both rendering modes.

The quad is attached to an animation matrix `M_anim(t)` that updates every frame:

```
P_corner_world(t) = M_anim(t) * P_corner_local
```

A simple looping animation is sufficient — e.g., a gentle rotation or oscillation so the quad visibly moves through the scene. This demonstrates that both rendering modes correctly follow a moving surface without requiring any changes to the UI geometry itself.

### 2.3 Lighting and Shadow Map

- One directional light with a configurable direction hardcoded at startup.
- Shadow map rendered in a dedicated depth-only pre-pass to a `VK_FORMAT_D32_SFLOAT` image (1024×1024).
- Main pass samples the shadow map with a `sampler2DShadow` and basic PCF (2×2 tap) for soft edges.
- The UI surface and room geometry both receive shadow.

---

## 3. Rendering Modes

### 3.1 Mode Toggle

Press `Space` to toggle between modes at runtime. The current mode is displayed in the metrics overlay (see Section 8). The toggle takes effect at the start of the next frame.

| Mode | Key | Description |
|------|-----|-------------|
| Traditional | `Space` (toggle) | UI rendered to offscreen RT, composited onto quad |
| Direct | `Space` (toggle) | UI rendered directly into main scene pass |

### 3.2 Traditional Mode — Offscreen RT

**Pass 1 — UI Pass:**
1. Allocate (once, reuse each frame) an offscreen color RT matching the UI canvas dimensions (`W_ui × H_ui`), format `VK_FORMAT_R8G8B8A8_UNORM`.
2. Render UI geometry (see Section 5) into the RT using standard 2D orthographic projection.
3. Resolve the RT if MSAA is active on the UI pass (optional — see Section 9).

**Pass 2 — Main Scene Pass:**
1. Render room geometry with lighting and shadow.
2. Render the UI surface quad with the offscreen RT bound as a texture. The quad's fragment shader samples the RT and alpha-blends the result.

**Pass 3 — Metrics Overlay Pass:** (see Section 8)

### 3.3 Direct Mode — Main Scene Pass Only

**Pre-pass — Shadow Map:** (same as traditional)

**Main Scene Pass:**
1. Render room geometry with lighting and shadow.
2. Render UI elements directly as world-space geometry using `M_total` (see Section 4). No separate UI pass. No offscreen RT.

**Metrics Overlay Pass:** (see Section 8)

The offscreen RT is not allocated when the app starts in direct mode; if the user toggles to traditional mode the RT is allocated on demand and persists until the app exits.

---

## 4. Transform Mathematics

The full mathematical derivation — coordinate space definitions, matrix constructions for `M_us`, `M_sw`, `M_wc`, clip plane derivation, and depth bias — is specified in [`direct_ui_rendering.md`](/spec/direct_ui_rendering.md). This section records only the implementation contracts that the CPU code and shaders must satisfy.

### 4.1 CPU Responsibilities (per frame)

1. Read the current animation matrix `M_anim(t)` and transform the four local surface corners to world space.
2. Compute `M_sw` from the world-space corners as defined in `direct_ui_rendering.md` §4.
3. Compute `M_us` from the UI canvas dimensions as defined in `direct_ui_rendering.md` §3.
4. Compute `M_total = M_wc * M_sw * M_us` and `M_world = M_sw * M_us`.
5. Derive the four world-space clip planes from the surface edges as defined in `direct_ui_rendering.md` §8.
6. Write `M_total`, `M_world`, `clipPlanes[4]`, and `depthBias` into `SurfaceUBO` (see Section 6.5).

### 4.2 Shader Contract

The direct-mode vertex shader receives UI-space positions as `vec2 inUIPos` and computes:

```glsl
vec4 uiVert   = vec4(inUIPos, 0.0, 1.0);
vec4 worldPos = worldMatrix * uiVert;      // for clip distances
gl_Position   = totalMatrix * uiVert;      // M_total applied in one multiply
gl_Position.z -= depthBias * gl_Position.w;
```

Full shader source is in Section 7.1.

### 4.3 Depth Bias Tuning

`depthBias` defaults to `0.0001` (see Appendix). It is adjustable at runtime with `+` / `-` keys and its current value is printed to stdout on change.

---

## 5. Simple UI System

### 5.1 Scope

A minimal retained-mode UI system sufficient to render "Hello World" as a textured quad using a pre-rendered glyph atlas. No layout engine, no event handling, no animation.

### 5.2 Atlas

- A single `512×512` RGBA PNG atlas containing pre-rendered glyphs for ASCII printable characters (generated offline, embedded as a C array or loaded from disk at startup).
- Each glyph has a fixed cell size (e.g., 32×32 px) for simplicity.
- A lookup table maps ASCII code → UV rect in the atlas.

### 5.3 UI Canvas

- Fixed canvas size: `W_ui = 512`, `H_ui = 128` (sufficient for a short string).
- The "Hello World" string is tessellated at startup into a list of quads (one per glyph), each with:
  - `inUIPos` — corner positions in UI space (pixels)
  - `inUITexCoord` — UV coordinates into the atlas
- This geometry is uploaded once to a vertex buffer and reused every frame.

### 5.4 Rendering in Traditional Mode

The UI quad list is rendered into the offscreen RT using an orthographic projection matrix:

```
M_ortho = ortho(0, W_ui, H_ui, 0, -1, 1)
```

### 5.5 Rendering in Direct Mode

The same quad list is rendered with the composite matrix `M_total` (Section 4.5) instead of `M_ortho`. No other changes to the geometry or draw calls.

---

## 6. Vulkan Architecture

### 6.1 Render Passes

| Pass | Condition | Attachments | Description |
|------|-----------|-------------|-------------|
| Shadow pass | Always | depth only (D32) | Depth pre-pass from light POV |
| UI RT pass | Traditional mode only | color (RGBA8) | Renders UI canvas to offscreen RT |
| Main scene pass | Always | color (MSAA) + depth (MSAA) | Room + UI surface |
| Resolve / present | Always | swapchain image | MSAA resolve to swapchain |
| Metrics overlay | Always | swapchain image (no depth) | HUD drawn on top |

### 6.2 Pipelines

| Pipeline | Vertex shader | Fragment shader | Notes |
|----------|--------------|-----------------|-------|
| `pipe_room` | `room.vert` | `room.frag` | Blinn-Phong + shadow map |
| `pipe_ui_rt` | `ui_ortho.vert` | `ui.frag` | Orthographic, for RT pass |
| `pipe_ui_direct` | `ui_direct.vert` | `ui.frag` | M_total transform, clip distances |
| `pipe_composite` | `quad.vert` | `composite.frag` | Samples offscreen RT onto quad |
| `pipe_surface` | `quad.vert` | `surface.frag` | Opaque teal quad drawn before direct-mode UI geometry |
| `pipe_metrics` | `ui_ortho.vert` | `ui.frag` | Reuses UI pipeline for HUD |

### 6.3 Descriptor Sets

| Set | Binding | Content | Updated |
|-----|---------|---------|---------|
| 0 | 0 | `SceneUBO` — view, proj, light params, shadow map matrix | Per frame |
| 0 | 1 | Shadow map sampler | Static |
| 1 | 0 | `SurfaceUBO` — M_total, M_world, clip planes, depth bias | Per frame |
| 2 | 0 | UI atlas sampler | Static |
| 2 | 1 | Offscreen RT sampler (traditional mode) | On toggle |

### 6.4 SceneUBO Layout

```c
struct SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 lightViewProj;   // for shadow map sampling
    vec4 lightDir;        // world-space directional light
    vec4 lightColor;
    vec4 ambientColor;
};
```

### 6.5 SurfaceUBO Layout

```c
struct SurfaceUBO {
    mat4 totalMatrix;     // M_wc * M_sw * M_us
    mat4 worldMatrix;     // M_sw * M_us, for clip distance computation
    vec4 clipPlanes[4];   // world-space clip planes
    float depthBias;
    float _pad[3];
};
```

### 6.6 Synchronization

- Shadow pass → main scene pass: image memory barrier on depth attachment (`DEPTH_STENCIL_ATTACHMENT_WRITE` → `SHADER_READ`).
- UI RT pass → main scene pass: image memory barrier on color attachment (`COLOR_ATTACHMENT_WRITE` → `SHADER_READ`).
- Standard semaphore-based swapchain acquire/present.

### 6.7 Memory Allocation

Use [VMA (Vulkan Memory Allocator)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) for all buffer and image allocations. Track total allocated bytes to report in the metrics overlay (Section 8).

---

## 7. Shaders

All shaders target SPIR-V via `glslc`. Shader source files live in `shaders/`.

### 7.1 `ui_direct.vert`

```glsl
#version 450

layout(set = 1, binding = 0) uniform SurfaceUBO {
    mat4 totalMatrix;
    mat4 worldMatrix;
    vec4 clipPlanes[4];
    float depthBias;
};

layout(location = 0) in vec2 inUIPos;
layout(location = 1) in vec2 inUITexCoord;
layout(location = 0) out vec2 outTexCoord;

out gl_PerVertex {
    vec4  gl_Position;
    float gl_ClipDistance[4];
};

void main() {
    vec4 uiVert  = vec4(inUIPos, 0.0, 1.0);
    vec4 worldPos = worldMatrix * uiVert;

    gl_ClipDistance[0] = dot(clipPlanes[0], worldPos);
    gl_ClipDistance[1] = dot(clipPlanes[1], worldPos);
    gl_ClipDistance[2] = dot(clipPlanes[2], worldPos);
    gl_ClipDistance[3] = dot(clipPlanes[3], worldPos);

    gl_Position    = totalMatrix * uiVert;
    gl_Position.z -= depthBias * gl_Position.w;

    outTexCoord = inUITexCoord;
}
```

### 7.2 `ui_ortho.vert`

```glsl
#version 450

layout(push_constant) uniform PC {
    mat4 orthoMatrix;
};

layout(location = 0) in vec2 inUIPos;
layout(location = 1) in vec2 inUITexCoord;
layout(location = 0) out vec2 outTexCoord;

void main() {
    gl_Position = orthoMatrix * vec4(inUIPos, 0.0, 1.0);
    outTexCoord = inUITexCoord;
}
```

### 7.3 `ui.frag`

```glsl
#version 450

layout(set = 2, binding = 0) uniform sampler2D uiAtlas;

layout(location = 0) in  vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(uiAtlas, inTexCoord);
    // Pre-multiplied alpha assumed in atlas
}
```

### 7.4 `composite.frag` (traditional mode only)

```glsl
#version 450

layout(set = 2, binding = 1) uniform sampler2D uiRT;

layout(location = 0) in  vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(uiRT, inTexCoord);
}
```

### 7.5 `room.vert` / `room.frag`

Standard Blinn-Phong with shadow map lookup. `room.frag` samples `SceneUBO.lightViewProj` to compute shadow coordinates and compares depth with a `sampler2DShadow` using `textureProj` for hardware PCF.

---

## 8. Metrics Overlay

A HUD rendered in the final pass on top of the swapchain image. Drawn using `pipe_metrics` (the orthographic UI pipeline, set 2 bound to the glyph atlas).

### 8.1 Displayed Fields

| Field | Source | Example |
|-------|--------|---------|
| Mode | App state | `Mode: DIRECT` / `Mode: TRADITIONAL` |
| Frame time | CPU timer (`std::chrono`) per frame | `Frame: 3.2 ms` |
| GPU memory | VMA total allocated bytes | `GPU Mem: 48.3 MB` |
| MSAA | Compile-time / runtime constant | `MSAA: 4x` |

### 8.2 Layout

Top-left corner, one line per field, fixed-width glyph rendering from the same atlas as the UI panel.

### 8.3 Frame Time Measurement

Average over the last 60 frames to avoid jitter. Use `VK_EXT_host_query_reset` + timestamp queries if GPU-side timing is desired in a future iteration; for now CPU-side timing is sufficient.

---

## 9. MSAA

- Sample count: **4x** (`VK_SAMPLE_COUNT_4_BIT`). Configurable at compile time via `#define MSAA_SAMPLES`.
- Applied to the main scene pass color and depth attachments.
- Resolved to the swapchain image at the end of the main pass using `VkRenderPassBeginInfo` resolve attachments (subpass resolve).
- The offscreen UI RT (traditional mode) uses **1x** MSAA — this is intentional and highlights the quality difference: the traditional mode composites a 1x RT onto an MSAA surface, while the direct mode renders UI geometry that is natively multisampled.
- Both modes display the active MSAA sample count in the metrics overlay.

---

## 10. Testing

### 10.1 Overview

Testing is split into two GoogleTest targets:

| Target | Vulkan context | What it tests |
|--------|---------------|---------------|
| `tests_unit` | None | Pure CPU math: matrix construction, clip plane derivation, metrics ring buffer, HUD tessellation |
| `tests_render` | Headless `VkDevice` | Clip containment: UI pixels fall inside the projected quad |
| `tests_sdf` | Headless `VkDevice` | SDF threshold/render tests with production shaders and real atlas |

Both targets link against the same app library (`direct_ui_rendering_lib`) and call the same functions the app uses. No math or rendering logic is duplicated.

### 10.2 Headless Renderer Design Constraint

The `Renderer` class must cleanly separate the **device/pipeline layer** from the **swapchain/presentation layer**:

- `Renderer::init(headless: bool, shaderDir: const char*)` — when `headless` is `true`, skips GLFW surface creation and swapchain setup. All pipelines, render passes, descriptor layouts, and VMA allocator are initialized identically. The `shaderDir` parameter specifies the directory path where test shaders are located; tests must pass `TEST_SHADER_DIR` at runtime so the library resolves test shaders rather than defaulting to the production `SHADER_DIR`.
- The output render target is abstracted behind a `RenderTarget` handle. In normal mode this wraps the swapchain image; in headless mode it wraps a plain `VkImage` allocated by the test.
- All render functions (`drawScene`, `drawUI`, etc.) accept a `RenderTarget&` and are unaware of whether it is a swapchain image or an offscreen image.

This constraint also applies to `UISurface` and `Scene` — neither may hold a direct reference to swapchain state.

---

## 11. Build and Dependencies

### 11.1 Language and Standard

C++20. No exceptions required; RAII for Vulkan handle lifetimes via thin wrappers or direct `vkDestroy*` in destructors.

### 11.2 Build System

CMake 3.25+. A single `CMakeLists.txt` at the repo root. Shader compilation integrated via `add_custom_command` invoking `glslc`.

### 11.3 Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| Vulkan SDK | 1.3+ | Core API, validation layers, `glslc` |
| GLFW | 3.4 | Window creation, input, Vulkan surface |
| VMA | 3.x | GPU memory allocation and tracking |
| `stb_image` | latest | PNG atlas loading |
| `glm` | 0.9.9+ | Math (matrices, vectors) |

All dependencies fetched via CMake `FetchContent` or expected on the system PATH (Vulkan SDK).

### 11.4 Validation Layers

GoogleTest is added as a `FetchContent` dependency. Both test targets link against `direct_ui_rendering_lib`, which is the app compiled as a static library with `main.cpp` excluded.

### 11.5 Validation Layers

Enabled in Debug builds: `VK_LAYER_KHRONOS_validation`. Disabled in Release.

---

## Appendix: Key Constants

| Constant | Value | Notes |
|----------|-------|-------|
| `W_ui` | 512 | UI canvas width (pixels) |
| `H_ui` | 128 | UI canvas height (pixels) |
| `SHADOW_MAP_SIZE` | 1024 | Shadow map resolution |
| `MSAA_SAMPLES` | 4 | Main pass sample count |
| `DEPTH_BIAS_DEFAULT` | 0.0001 | Initial NDC z-bias for direct mode |
| `ATLAS_SIZE` | 512 | Glyph atlas texture size (pixels) |
| `GLYPH_CELL` | 32 | Fixed glyph cell size (pixels) |