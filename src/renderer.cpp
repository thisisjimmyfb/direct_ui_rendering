#include "renderer.h"

// Thin orchestration layer — actual implementations are split into:
// - renderer_init.cpp: device/pipeline/swapchain setup
// - renderer_recording.cpp: per-frame command-buffer recording
// - renderer_resources.cpp: GPU resource management
