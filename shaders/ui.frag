#version 450

layout(set = 2, binding = 0) uniform sampler2D uiAtlas;

layout(location = 0) in  vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
#ifdef UI_TEST_COLOR
    outColor = vec4(1.0, 0.0, 1.0, 1.0);  // Solid magenta for render tests
#else
    outColor = texture(uiAtlas, inTexCoord);
    // Pre-multiplied alpha assumed in atlas
#endif
}
