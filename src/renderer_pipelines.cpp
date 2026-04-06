#include <vk_mem_alloc.h>

#include "renderer.h"
#include "vk_utils.h"
#include "scene.h"
#include "ui_system.h"
#include "msaa_config.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

// ---------------------------------------------------------------------------
// createPipelines — load shaders and create all 6 pipelines
// ---------------------------------------------------------------------------

bool Renderer::createPipelines()
{
    // Helper: read a .spv file and create a VkShaderModule.
    auto loadShaderModule = [&](const char* relName) -> VkShaderModule {
        std::string path = m_shaderDir + relName;
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) {
            fprintf(stderr, "Renderer: cannot open shader: %s\n", path.c_str());
            return VK_NULL_HANDLE;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<char> buf(sz);
        fread(buf.data(), 1, static_cast<size_t>(sz), f);
        fclose(f);

        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = static_cast<size_t>(sz);
        ci.pCode    = reinterpret_cast<const uint32_t*>(buf.data());
        VkShaderModule mod{VK_NULL_HANDLE};
        vkCreateShaderModule(m_device, &ci, nullptr, &mod);
        return mod;
    };

    VkShaderModule vsRoom        = loadShaderModule("room.vert.spv");
    VkShaderModule fsRoom        = loadShaderModule("room.frag.spv");
    VkShaderModule vsUIDirect    = loadShaderModule("ui_direct.vert.spv");
    VkShaderModule fsUIDirect    = loadShaderModule("ui_direct.frag.spv");
    VkShaderModule vsUIOrtho     = loadShaderModule("ui_ortho.vert.spv");
    VkShaderModule fsUI          = loadShaderModule("ui.frag.spv");
    VkShaderModule fsComposite   = loadShaderModule("composite.frag.spv");
    VkShaderModule fsSurface     = loadShaderModule("surface.frag.spv");
    VkShaderModule vsQuad        = loadShaderModule("quad.vert.spv");
    VkShaderModule vsShadow      = loadShaderModule("shadow.vert.spv");

    auto destroyModules = [&]() {
        auto d = [&](VkShaderModule m) { if (m) vkDestroyShaderModule(m_device, m, nullptr); };
        d(vsRoom); d(fsRoom); d(vsUIDirect); d(fsUIDirect); d(vsUIOrtho);
        d(fsUI); d(fsComposite); d(fsSurface); d(vsQuad); d(vsShadow);
    };

    if (!vsRoom || !fsRoom || !vsUIDirect || !fsUIDirect || !vsUIOrtho || !fsUI || !fsComposite || !fsSurface || !vsQuad || !vsShadow) {
        destroyModules();
        return false;
    }

    // Common state shared across all pipelines.
    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates    = dynStates;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Pre-multiplied alpha blend state (used by all UI pipelines).
    VkPipelineColorBlendAttachmentState premulBlend{};
    premulBlend.blendEnable         = VK_TRUE;
    premulBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    premulBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    premulBlend.colorBlendOp        = VK_BLEND_OP_ADD;
    premulBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    premulBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    premulBlend.alphaBlendOp        = VK_BLEND_OP_ADD;
    premulBlend.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo premulBlendState{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    premulBlendState.attachmentCount = 1;
    premulBlendState.pAttachments    = &premulBlend;

    // Opaque blend (room geometry).
    VkPipelineColorBlendAttachmentState opaqueBlend{};
    opaqueBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo opaqueBlendState{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    opaqueBlendState.attachmentCount = 1;
    opaqueBlendState.pAttachments    = &opaqueBlend;

    // Vertex input: room geometry (Vertex: pos:vec3, normal:vec3, uv:vec2, material:vec2, color:vec3 = 40 bytes).
    VkVertexInputBindingDescription roomBinding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription roomAttrs[5]{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Vertex, uv)},
        {4, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Vertex, material)},
        {5, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
    };
    VkPipelineVertexInputStateCreateInfo roomVertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    roomVertexInput.vertexBindingDescriptionCount   = 1;
    roomVertexInput.pVertexBindingDescriptions      = &roomBinding;
    roomVertexInput.vertexAttributeDescriptionCount = 5;
    roomVertexInput.pVertexAttributeDescriptions    = roomAttrs;

    // Vertex input: UI (UIVertex: pos:vec2, uv:vec2 = 16 bytes).
    VkVertexInputBindingDescription uiBinding{0, sizeof(UIVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription uiAttrs[2]{
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, pos)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, uv)},
    };
    VkPipelineVertexInputStateCreateInfo uiVertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    uiVertexInput.vertexBindingDescriptionCount   = 1;
    uiVertexInput.pVertexBindingDescriptions      = &uiBinding;
    uiVertexInput.vertexAttributeDescriptionCount = 2;
    uiVertexInput.pVertexAttributeDescriptions    = uiAttrs;

    // Vertex input: composite quad (pos:vec3, uv:vec2, faceIndex:int = 24 bytes).
    VkVertexInputBindingDescription quadBinding{0, sizeof(QuadVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription quadAttrs[3]{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(QuadVertex, pos)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(QuadVertex, uv)},
        {2, 0, VK_FORMAT_R32_SINT,         offsetof(QuadVertex, faceIndex)},
    };
    VkPipelineVertexInputStateCreateInfo quadVertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    quadVertexInput.vertexBindingDescriptionCount   = 1;
    quadVertexInput.pVertexBindingDescriptions      = &quadBinding;
    quadVertexInput.vertexAttributeDescriptionCount = 3;
    quadVertexInput.pVertexAttributeDescriptions    = quadAttrs;

    // --- 0. pipe_shadow: depth-only, renders room geometry using lightViewProj ---
    {
        VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stage.module = vsShadow;
        stage.pName  = "main";

        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode             = VK_POLYGON_MODE_FILL;
        raster.cullMode                = VK_CULL_MODE_NONE;  // no culling for shadow pass
        raster.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth               = 1.0f;
        raster.depthBiasEnable         = VK_TRUE;
        raster.depthBiasConstantFactor = 1.25f;
        raster.depthBiasSlopeFactor    = 1.75f;

        VkPipelineMultisampleStateCreateInfo msaa{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable  = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        // Depth-only pass: no color attachments
        VkPipelineColorBlendStateCreateInfo colorBlend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlend.attachmentCount = 0;

        VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pci.stageCount          = 1;
        pci.pStages             = &stage;
        pci.pVertexInputState   = &roomVertexInput;
        pci.pInputAssemblyState = &inputAssembly;
        pci.pViewportState      = &viewportState;
        pci.pRasterizationState = &raster;
        pci.pMultisampleState   = &msaa;
        pci.pDepthStencilState  = &depth;
        pci.pColorBlendState    = &colorBlend;
        pci.pDynamicState       = &dynState;
        pci.layout              = m_pipelineLayout;
        pci.renderPass          = m_shadowPass;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeShadow)
                != VK_SUCCESS) {
            destroyModules();
            return false;
        }
    }

    // --- 1. pipe_room: Blinn-Phong room geometry, depth test+write, 4x MSAA ---
    {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_VERTEX_BIT,   vsRoom, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_FRAGMENT_BIT, fsRoom, "main", nullptr};

        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_BACK_BIT;
        raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        msaa.rasterizationSamples = msaaSampleCount();

        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable  = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pci.stageCount          = 2;
        pci.pStages             = stages;
        pci.pVertexInputState   = &roomVertexInput;
        pci.pInputAssemblyState = &inputAssembly;
        pci.pViewportState      = &viewportState;
        pci.pRasterizationState = &raster;
        pci.pMultisampleState   = &msaa;
        pci.pDepthStencilState  = &depth;
        pci.pColorBlendState    = &opaqueBlendState;
        pci.pDynamicState       = &dynState;
        pci.layout              = m_pipelineLayout;
        pci.renderPass          = m_mainPass;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeRoom)
                != VK_SUCCESS) {
            destroyModules();
            return false;
        }
    }

    // --- 2. pipe_ui_direct: UI in world space, clip distances, pre-multiplied alpha, 4x MSAA ---
    {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_VERTEX_BIT,   vsUIDirect,  "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_FRAGMENT_BIT, fsUIDirect,  "main", nullptr};

        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_NONE;
        raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        msaa.rasterizationSamples = msaaSampleCount();

        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable  = VK_TRUE;
        depth.depthWriteEnable = VK_FALSE;
        depth.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pci.stageCount          = 2;
        pci.pStages             = stages;
        pci.pVertexInputState   = &uiVertexInput;
        pci.pInputAssemblyState = &inputAssembly;
        pci.pViewportState      = &viewportState;
        pci.pRasterizationState = &raster;
        pci.pMultisampleState   = &msaa;
        pci.pDepthStencilState  = &depth;
        pci.pColorBlendState    = &premulBlendState;
        pci.pDynamicState       = &dynState;
        pci.layout              = m_pipelineLayout;
        pci.renderPass          = m_mainPass;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeUIDirect)
                != VK_SUCCESS) {
            destroyModules();
            return false;
        }
    }

    // --- 3. pipe_ui_rt: orthographic UI into offscreen RT, 1x MSAA, alpha blend ---
    {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_VERTEX_BIT,   vsUIOrtho, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_FRAGMENT_BIT, fsUI,      "main", nullptr};

        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_NONE;
        raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

        VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pci.stageCount          = 2;
        pci.pStages             = stages;
        pci.pVertexInputState   = &uiVertexInput;
        pci.pInputAssemblyState = &inputAssembly;
        pci.pViewportState      = &viewportState;
        pci.pRasterizationState = &raster;
        pci.pMultisampleState   = &msaa;
        pci.pDepthStencilState  = &depth;
        pci.pColorBlendState    = &premulBlendState;
        pci.pDynamicState       = &dynState;
        pci.layout              = m_pipelineLayout;
        pci.renderPass          = m_uiRTPass;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeUIRT)
                != VK_SUCCESS) {
            destroyModules();
            return false;
        }
    }

    // --- 4. pipe_composite: surface quad; teal base + UI RT blended on top, 4x MSAA ---
    {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_VERTEX_BIT,   vsQuad,      "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_FRAGMENT_BIT, fsComposite, "main", nullptr};

        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_NONE;
        raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        msaa.rasterizationSamples = msaaSampleCount();

        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable  = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;   // opaque — write depth
        depth.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pci.stageCount          = 2;
        pci.pStages             = stages;
        pci.pVertexInputState   = &quadVertexInput;
        pci.pInputAssemblyState = &inputAssembly;
        pci.pViewportState      = &viewportState;
        pci.pRasterizationState = &raster;
        pci.pMultisampleState   = &msaa;
        pci.pDepthStencilState  = &depth;
        pci.pColorBlendState    = &opaqueBlendState;  // shader outputs alpha=1
        pci.pDynamicState       = &dynState;
        pci.layout              = m_pipelineLayout;
        pci.renderPass          = m_mainPass;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeComposite)
                != VK_SUCCESS) {
            destroyModules();
            return false;
        }
    }

    // --- 4b. pipe_surface: opaque teal quad for direct mode (UI renders on top) ---
    {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_VERTEX_BIT,   vsQuad,    "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_FRAGMENT_BIT, fsSurface, "main", nullptr};

        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_NONE;
        raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        msaa.rasterizationSamples = msaaSampleCount();

        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable  = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pci.stageCount          = 2;
        pci.pStages             = stages;
        pci.pVertexInputState   = &quadVertexInput;
        pci.pInputAssemblyState = &inputAssembly;
        pci.pViewportState      = &viewportState;
        pci.pRasterizationState = &raster;
        pci.pMultisampleState   = &msaa;
        pci.pDepthStencilState  = &depth;
        pci.pColorBlendState    = &opaqueBlendState;
        pci.pDynamicState       = &dynState;
        pci.layout              = m_pipelineLayout;
        pci.renderPass          = m_mainPass;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeSurface)
                != VK_SUCCESS) {
            destroyModules();
            return false;
        }
    }

    // --- 5. pipe_metrics: orthographic HUD overlay, 1x MSAA, alpha blend ---
    {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_VERTEX_BIT,   vsUIOrtho, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_FRAGMENT_BIT, fsUI,      "main", nullptr};

        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_NONE;
        raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

        VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pci.stageCount          = 2;
        pci.pStages             = stages;
        pci.pVertexInputState   = &uiVertexInput;
        pci.pInputAssemblyState = &inputAssembly;
        pci.pViewportState      = &viewportState;
        pci.pRasterizationState = &raster;
        pci.pMultisampleState   = &msaa;
        pci.pDepthStencilState  = &depth;
        pci.pColorBlendState    = &premulBlendState;
        pci.pDynamicState       = &dynState;
        pci.layout              = m_pipelineLayout;
        pci.renderPass          = m_metricsPass;

        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeMetrics)
                != VK_SUCCESS) {
            destroyModules();
            return false;
        }
    }

    destroyModules();
    return true;
}
