# Direct UI-to-Surface Rendering: Mathematical Framework

## Motivation

Traditional mobile UI rendering follows a two-pass approach:

1. **Pass 1** — Render UI elements to an offscreen render target (RT) in 2D UI space.
2. **Pass 2** — Composite the RT onto the final surface (e.g., a quad in world space).

This incurs the cost of an extra render target allocation, a full-screen resolve, and a texture fetch during compositing. The idea here is to **eliminate the offscreen RT entirely** by computing a single transform that maps UI-space vertices directly into clip space such that they land exactly on the target surface in the 3D scene. The UI element becomes a degenerate (flat) 3D mesh rendered in the main scene pass and will benefit from the anti-aliasing techniques used by the main scene pass.

---

## 1. Coordinate Space Definitions

We define four coordinate spaces and the transforms between them:

### UI Space (U)

A 2D coordinate system in which UI is authored.

- Origin at top-left of the UI canvas.
- X-axis right, Y-axis down.
- Dimensions: `W_ui × H_ui` (e.g., 1920 × 1080 logical pixels).
- A vertex in UI space: `p_ui = (x_ui, y_ui)`

### Normalized Surface Space (S)

The unit square representing the target surface, before it is placed in the world.

- `(0, 0)` = top-left corner of the surface
- `(1, 1)` = bottom-right corner of the surface
- This is the "UV-like" parameterization of the surface.

### World Space (W)

Standard 3D world coordinates where the scene lives. The target surface is a planar quad defined by its four corners in world space.

### Clip Space (C)

The output of the vertex shader: `(x_c, y_c, z_c, w_c)` — homogeneous coordinates that the GPU's rasterizer consumes.

---

## 2. The Transform Chain

The full transform is:

```
p_ui  →  p_s  →  p_w  →  p_clip
       M_us     M_sw     M_wc
```

Where:
- **M_us** : UI space → Normalized surface space
- **M_sw** : Normalized surface space → World space
- **M_wc** : World space → Clip space (the scene's View-Projection matrix)

Since each of these is a matrix (or can be expressed as one), the composite is:

```
M_total = M_wc * M_sw * M_us
```

This single matrix is set on the UI mesh and the GPU does the rest.

---

## 3. Stage 1: UI Space → Normalized Surface Space (M_us)

This maps the UI canvas onto the `[0, 1] × [0, 1]` unit square. It is a simple scale and (optionally) translation if the UI element occupies only a sub-region of the surface.

### Full canvas mapping

For UI coordinates `(x_ui, y_ui)` in the range `[0, W_ui] × [0, H_ui]`:

```
        ┌ 1/W_ui    0      0   0 ┐
M_us =  │   0     1/H_ui   0   0 │
        │   0       0      1   0 │
        └   0       0      0   1 ┘
```

So `p_s = M_us * p_ui_h` where `p_ui_h = (x_ui, y_ui, 0, 1)`.

### Sub-region mapping

If the UI element occupies a rectangular sub-region `[x0, x1] × [y0, y1]` of the canvas and maps to surface region `[u0, u1] × [v0, v1]`:

```
s_x = (u1 - u0) / (x1 - x0)
s_y = (v1 - v0) / (y1 - y0)
t_x = u0 - s_x * x0
t_y = v0 - s_y * y0
```

```
        ┌ s_x   0    0   t_x ┐
M_us =  │  0   s_y   0   t_y │
        │  0    0    1    0   │
        └  0    0    0    1   ┘
```

---

## 4. Stage 2: Normalized Surface Space → World Space (M_sw)

This is the key geometric step. The target surface is a planar quad in world space defined by its four corners. We parameterize it using an **affine frame**.

### Rigid Rectangular Surface

The surface is a flat rectangle in world space defined by:

- `P_00` = top-left corner (corresponds to `(s, t) = (0, 0)` in normalized surface space)
- `P_10` = top-right corner (corresponds to `(s, t) = (1, 0)` in normalized surface space)
- `P_01` = bottom-left corner (corresponds to `(s, t) = (0, 1)` in normalized surface space)

Here `s` and `t` are the normalized surface space coordinates from Section 1, with `s` running horizontally and `t` vertically across the `[0, 1] × [0, 1]` unit square.

Derive the edge vectors:

```
e_u = P_10 - P_00    (horizontal edge in world space)
e_v = P_01 - P_00    (vertical edge in world space)
```

The world-space position for surface parameter `(s, t)` is:

```
p_w = P_00 + s * e_u + t * e_v
```

As a 4×4 homogeneous matrix:

```
         ┌ e_u.x   e_v.x   n.x   P_00.x ┐
M_sw  =  │ e_u.y   e_v.y   n.y   P_00.y │
         │ e_u.z   e_v.z   n.z   P_00.z │
         └   0       0      0      1     ┘
```

Where `n = normalize(e_u × e_v)` is the surface normal. The third column is the surface normal — it doesn't affect the mapping of the 2D coordinates (since `z_s = 0`), but it is needed for:
- Correct depth buffer values (the mesh has some nonzero extent along the normal)
- Lighting calculations if you want the UI to receive scene lighting

> **Note:** For a purely flat UI element, the normal column can be set to any value since the input z-coordinate is always 0. In practice, using the true surface normal is cleanest.

### Compatible Primitives

The affine `M_sw` matrix generalizes beyond rectangles to any primitive whose surface-space-to-world-space mapping is affine — i.e., the surface is planar and the parameterization is linear. The following are directly compatible with little or no change to the spec:

- **Parallelogram** — identical math; `e_u` and `e_v` need not be orthogonal or equal in length. Rectangles, rhombuses, and oblique quads all fall here.
- **Triangle** — use two edges from a shared vertex as `e_u` and `e_v`. The unit-square parameterization covers the full parallelogram spanned by those edges; trim to the triangle using a single clip plane (or stencil) along the diagonal.
- **Planar convex polygon (n-gon)** — define an affine frame `(P_00, e_u, e_v)` anchored anywhere on the plane. `M_sw` is the same for the entire surface; boundary clipping (gl_ClipDistance or stencil, as in Section 8) constrains rendering to the polygon's outline.
- **Any planar region** — as long as the surface lies on a single plane, the same `M_sw` applies everywhere. Complex outlines are handled entirely by clipping, not by changes to the matrix.

The common requirement is planarity. Non-planar surfaces (cylinders, spheres, etc.) require a per-vertex nonlinear mapping and are outside the scope of this spec.

---

## 5. Stage 3: World Space → Clip Space (M_wc)

This is simply the scene's View-Projection matrix:

```
M_wc = M_projection * M_view
```

This is already computed for the main scene pass. No additional work here.

---

## 6. The Composite Transform

For the rigid rectangular case, the full transform is:

```
M_total = M_wc * M_sw * M_us
```

This is a single 4×4 matrix that maps UI-space homogeneous coordinates directly to clip space. Substituting and multiplying out:

```
M_total = (Proj * View) * M_sw * M_us
```

```
         ┌ e_u.x/W_ui   e_v.x/H_ui   n.x   P_00.x ┐
M_sw *   │ e_u.y/W_ui   e_v.y/H_ui   n.y   P_00.y │
M_us  =  │ e_u.z/W_ui   e_v.z/H_ui   n.z   P_00.z │
         └     0             0          0      1     ┘
```

Then left-multiply by `M_wc` to get the final 4×4.

---

## 7. Depth and Z-Fighting Considerations

Since the UI mesh is coplanar with the target surface, you'll get z-fighting with any geometry at that surface. After computing clip-space position, subtract a small bias from `z_clip`:

```glsl
// Vulkan GLSL (SPIR-V target)
layout(set = 0, binding = 0) uniform Transforms {
    mat4 totalMatrix;
    float depthBias;
};

void main() {
    gl_Position = totalMatrix * vec4(inUIPos.xy, 0.0, 1.0);
    gl_Position.z -= depthBias * gl_Position.w;  // NDC-space bias
}
```

This is view-independent and consistent.

---

## 8. Clipping to Surface Bounds

UI elements near the edges may produce vertices that extend beyond the target surface. You need clipping:

### Hardware Scissor (if axis-aligned in screen space)

If the surface projects to an axis-aligned rectangle on screen, use the GPU scissor rect.

### Stencil-Based Clipping

1. Render the target surface quad into the stencil buffer.
2. Render UI elements with stencil test: pass only where stencil is set.

### Vertex Shader Clipping

Output `gl_ClipDistance[0..3]` to define four clip planes corresponding to the surface edges:

```glsl
// Vulkan GLSL (#version 450) — clip planes in world space (precomputed on CPU)
layout(set = 0, binding = 0) uniform ClipUBO {
    mat4 worldMatrix;   // M_sw * M_us
    mat4 vpMatrix;      // M_wc
    vec4 clipPlanes[4];
};

layout(location = 0) in vec2 inUIPos;

out gl_PerVertex {
    vec4  gl_Position;
    float gl_ClipDistance[4];
};

void main() {
    vec4 worldPos = worldMatrix * vec4(inUIPos.xy, 0.0, 1.0);

    gl_ClipDistance[0] = dot(clipPlanes[0], worldPos);
    gl_ClipDistance[1] = dot(clipPlanes[1], worldPos);
    gl_ClipDistance[2] = dot(clipPlanes[2], worldPos);
    gl_ClipDistance[3] = dot(clipPlanes[3], worldPos);

    gl_Position = vpMatrix * worldPos;
}
```

The four clip planes are derived from the surface edges:

```
Plane_left   = surface plane through P_00 and P_01, normal pointing inward
Plane_right  = surface plane through P_10 and P_11, normal pointing inward
Plane_top    = surface plane through P_00 and P_10, normal pointing inward
Plane_bottom = surface plane through P_01 and P_11, normal pointing inward
```

For a rigid rectangle with edge vectors `e_u` and `e_v` and normal `n`:

```
inward_left   = normalize(cross(e_v, n))     → (nx, ny, nz, -dot(n_left, P_00))
inward_right  = -inward_left                 → (nx, ny, nz, -dot(n_right, P_10))
inward_top    = normalize(cross(n, e_u))     → (nx, ny, nz, -dot(n_top, P_00))
inward_bottom = -inward_top                  → (nx, ny, nz, -dot(n_bottom, P_01))
```

---

## 9. Handling Transparency and Blending

Since UI elements render in the main scene pass rather than to their own RT, blending order matters. Transparent UI elemnts can simply be rendered in a dedicated transparent pass to simplify the pipeline.

---

## 10. Summary: Putting It All Together

### CPU-Side Setup (per frame or when UI/surface changes)

```
1. Compute M_us from UI canvas dimensions and sub-region
2. Compute M_sw from surface corner positions in world space
3. M_wc = Projection * View  (already available)
4. M_total = M_wc * M_sw * M_us
5. Compute clip planes from surface edges (if using gl_ClipDistance)
6. Write M_total, clip planes, depth bias to descriptor set UBO
```

### Vertex Shader

```glsl
#version 450

// Descriptor set 0: per-surface transforms
layout(set = 0, binding = 0) uniform SurfaceUBO {
    mat4 totalMatrix;       // M_wc * M_sw * M_us
    mat4 worldMatrix;       // M_sw * M_us, for clip distances
    vec4 clipPlanes[4];
    float depthBias;
};

// Vertex inputs
layout(location = 0) in vec2 inUIPos;        // UI-space coordinates
layout(location = 1) in vec2 inUITexCoord;   // UV for the UI element's texture

// Outputs
layout(location = 0) out vec2 outTexCoord;

out gl_PerVertex {
    vec4  gl_Position;
    float gl_ClipDistance[4];
};

void main() {
    vec4 uiVertex = vec4(inUIPos, 0.0, 1.0);

    // Clip distances (in world space)
    vec4 worldPos = worldMatrix * uiVertex;
    gl_ClipDistance[0] = dot(clipPlanes[0], worldPos);
    gl_ClipDistance[1] = dot(clipPlanes[1], worldPos);
    gl_ClipDistance[2] = dot(clipPlanes[2], worldPos);
    gl_ClipDistance[3] = dot(clipPlanes[3], worldPos);

    // Final clip-space position
    gl_Position = totalMatrix * uiVertex;
    gl_Position.z -= depthBias * gl_Position.w;

    outTexCoord = inUITexCoord;
}
```

### Fragment Shader

```glsl
#version 450

// Descriptor set 1: per-material resources
layout(set = 1, binding = 0) uniform sampler2D uiTexture;  // glyph atlas, icon atlas, etc.
layout(set = 1, binding = 1) uniform MaterialUBO {
    vec4 color;
};

layout(location = 0) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(uiTexture, inTexCoord);
    outColor = texColor * color;  // pre-multiplied alpha
}
```

---

## 11. Cost Analysis

| Aspect | Offscreen RT Approach | Direct Transform Approach |
|--------|----------------------|--------------------------|
| Memory | Extra RT allocation (W×H×MSAA) | None |
| Bandwidth | RT fill + RT read + composite | Single fill (scene pass) |
| Draw calls | UI draws + 1 composite | UI draws only |
| Overdraw | UI overdraw in RT + scene overdraw | UI overdraw in scene only |
| Vertex cost | Simple 2D transforms | One 4×4 multiply (negligible) |
| Flexibility | Arbitrary post-processing | Limited to planar surfaces |

The primary savings are **memory bandwidth** (no RT readback) and **memory footprint** (no RT allocation), which are the two most precious resources on mobile GPUs with tiled architectures. Additionally, direct UI rendering will benefit from anti-aliasing techniques of the main pass, resulting in higher quality image.
