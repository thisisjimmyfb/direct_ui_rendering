#include "app.h"
#include "vk_utils.h"

#include <glm/gtc/matrix_transform.hpp>
#include <vk_mem_alloc.h>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
static std::string exeDir()
{
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len == 0) return ".";
    std::string path(buf, len);
    auto sep = path.find_last_of("\\/");
    return (sep == std::string::npos) ? "." : path.substr(0, sep);
}
#else
#  include <unistd.h>
#  include <climits>
static std::string exeDir()
{
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return ".";
    buf[len] = '\0';
    std::string path(buf);
    auto sep = path.find_last_of('/');
    return (sep == std::string::npos) ? "." : path.substr(0, sep);
}
#endif

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int App::run()
{
    if (!initWindow())      return 1;
    if (!initSubsystems())  return 1;

    // Record start time for timeout calculation and first-frame delta time
    m_startTime     = std::chrono::steady_clock::now();
    m_lastFrameTime = m_startTime;

    mainLoop();
    cleanup();
    return 0;
}

void App::setTimeout(int seconds)
{
    m_timeoutSeconds = seconds;
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

bool App::initWindow()
{
    if (!glfwInit()) return false;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    m_window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT,
                                "Direct UI Rendering", nullptr, nullptr);
    if (!m_window) return false;

    glfwSetWindowUserPointer(m_window, this);
    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetCursorPosCallback(m_window, cursorPosCallback);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
    return true;
}

bool App::initSubsystems()
{
    if (!m_renderer.init(/*headless=*/false, m_window)) return false;

    m_scene.init();

    std::string atlasPath = exeDir() + "/assets/atlas.png";
    if (!m_ui.init(m_renderer.getAllocator(),
                   m_renderer.getDevice(),
                   m_renderer.getCommandPool(),
                   m_renderer.getGraphicsQueue(),
                   atlasPath.c_str())) {
        return false;
    }

    // Upload room geometry to GPU
    if (!m_renderer.uploadSceneGeometry(m_scene)) return false;

    // Bind the glyph atlas into descriptor set 2, binding 0
    m_renderer.bindAtlasDescriptor(m_ui.atlasView(), m_ui.atlasSampler());

    // Allocate host-visible HUD vertex buffer (max 1024 UIVertex)
    {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size        = sizeof(UIVertex) * 1024;
        bci.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        if (vmaCreateBuffer(m_renderer.getAllocator(), &bci, &aci,
                            &m_hudVtxBuf, &m_hudVtxAlloc, nullptr) != VK_SUCCESS)
            return false;
    }

    // Allocate the per-frame command buffer
    {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool        = m_renderer.getCommandPool();
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(m_renderer.getDevice(), &ai, &m_cmd) != VK_SUCCESS)
            return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------

void App::mainLoop()
{
    while (!glfwWindowShouldClose(m_window)) {
        // Check timeout if enabled
        if (m_timeoutSeconds > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - m_startTime).count();
            if (elapsed >= m_timeoutSeconds) {
                printf("Timeout reached: %d seconds. Exiting.\n", m_timeoutSeconds);
                glfwSetWindowShouldClose(m_window, GLFW_TRUE);
                break;
            }
        }

        glfwPollEvents();
        drawFrame();
    }
}

void App::drawFrame()
{
    m_metrics.beginFrame();

    // Apply pending mode toggle at frame start.
    if (m_pendingModeToggle) {
        m_mode = (m_mode == RenderMode::Direct) ? RenderMode::Traditional : RenderMode::Direct;
        m_pendingModeToggle = false;
    }

    // Compute delta time for smooth camera movement
    auto now = std::chrono::steady_clock::now();
    float dt  = std::chrono::duration<float>(now - m_lastFrameTime).count();
    if (dt > 0.1f) dt = 0.1f;  // clamp first-frame spike
    m_lastFrameTime = now;

    m_time += dt;

    // WASD camera movement
    glm::vec3 camFront(
        std::cos(m_camYaw) * std::cos(m_camPitch),
        std::sin(m_camPitch),
        std::sin(m_camYaw) * std::cos(m_camPitch));
    glm::vec3 camRight = glm::normalize(glm::cross(camFront, glm::vec3(0, 1, 0)));
    const float camSpeed = 3.0f * dt;
    if (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS) m_camPos += camFront * camSpeed;
    if (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS) m_camPos -= camFront * camSpeed;
    if (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS) m_camPos -= camRight * camSpeed;
    if (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS) m_camPos += camRight * camSpeed;

    // Compute world-space surface corners for this frame.
    glm::vec3 P_00, P_10, P_01, P_11;
    m_scene.worldCorners(m_time, P_00, P_10, P_01, P_11);

    // Camera look-at from current position and orientation
    glm::mat4 view = glm::lookAt(m_camPos, m_camPos + camFront, glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                      static_cast<float>(WINDOW_WIDTH) / WINDOW_HEIGHT,
                                      0.1f, 100.0f);
    proj[1][1] *= -1.0f;  // Flip Y for Vulkan clip space

    // SceneUBO
    SceneUBO sceneUBO{};
    sceneUBO.view        = view;
    sceneUBO.proj        = proj;
    sceneUBO.lightViewProj = m_scene.lightViewProj();
    sceneUBO.lightDir    = glm::vec4(m_scene.light().direction, 0.0f);
    sceneUBO.lightColor  = glm::vec4(m_scene.light().color, 1.0f);
    sceneUBO.ambientColor = glm::vec4(m_scene.light().ambient, 1.0f);
    m_renderer.updateSceneUBO(sceneUBO);

    // SurfaceUBO — always computed, only consumed by direct mode shaders
    auto transforms = computeSurfaceTransforms(P_00, P_10, P_01,
                                               W_UI, H_UI,
                                               proj * view);
    auto clipPlanes  = computeClipPlanes(P_00, P_10, P_01);

    SurfaceUBO surfaceUBO{};
    surfaceUBO.totalMatrix = transforms.M_total;
    surfaceUBO.worldMatrix = transforms.M_world;
    for (int i = 0; i < 4; ++i) surfaceUBO.clipPlanes[i] = clipPlanes[i];
    surfaceUBO.depthBias   = m_depthBias;
    m_renderer.updateSurfaceUBO(surfaceUBO);

    // Update animated surface quad for traditional (composite) mode.
    m_renderer.updateSurfaceQuad(P_00, P_10, P_01, P_11);

    // Tessellate HUD and upload to GPU buffer.
    m_hudVerts.clear();
    uint32_t hudVtxCount = m_metrics.tessellateHUD(m_ui, m_mode, 4, m_hudVerts);
    if (hudVtxCount > 0 && m_hudVtxBuf != VK_NULL_HANDLE) {
        void* mapped = nullptr;
        vmaMapMemory(m_renderer.getAllocator(), m_hudVtxAlloc, &mapped);
        memcpy(mapped, m_hudVerts.data(), sizeof(UIVertex) * hudVtxCount);
        vmaUnmapMemory(m_renderer.getAllocator(), m_hudVtxAlloc);
    }

    // Acquire next swapchain image (blocks until previous frame finishes).
    uint32_t imgIdx = 0;
    if (!m_renderer.acquireSwapchainImage(imgIdx)) {
        m_metrics.endFrame();
        return;
    }

    // Reset and begin the per-frame command buffer.
    vkResetCommandBuffer(m_cmd, 0);
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_cmd, &beginInfo);

    // Shadow pre-pass.
    m_renderer.recordShadowPass(m_cmd);

    // UI RT pass (traditional mode only).
    if (m_mode == RenderMode::Traditional) {
        glm::mat4 uiOrtho = glm::ortho(0.0f, (float)W_UI, 0.0f, (float)H_UI, -1.0f, 1.0f);
        m_renderer.recordUIRTPass(m_cmd,
                                  m_ui.helloVertBuffer(), m_ui.helloVertCount(),
                                  uiOrtho);
    }

    // Main scene pass: room + UI (direct) or surface composite (traditional).
    auto& rt = m_renderer.getSwapchainRT(imgIdx);
    m_renderer.recordMainPass(m_cmd, rt,
                              m_mode == RenderMode::Direct,
                              m_ui.helloVertBuffer(), m_ui.helloVertCount());

    // Pipeline barrier: ensure main pass color writes are visible to the metrics pass.
    vku::imageBarrier(m_cmd, rt.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    // Metrics overlay pass.
    glm::mat4 hudOrtho = glm::ortho(0.0f, (float)WINDOW_WIDTH,
                                    0.0f, (float)WINDOW_HEIGHT, -1.0f, 1.0f);
    m_renderer.recordMetricsPass(m_cmd, rt,
                                 m_hudVtxBuf, hudVtxCount,
                                 hudOrtho);

    vkEndCommandBuffer(m_cmd);

    // Submit with semaphore synchronisation.
    VkSemaphore          waitSem   = m_renderer.getImageAvailableSemaphore();
    VkSemaphore          signalSem = m_renderer.getRenderFinishedSemaphore(imgIdx);
    VkFence              fence     = m_renderer.getInFlightFence();
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &waitSem;
    submit.pWaitDstStageMask    = &waitStage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &m_cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &signalSem;

    vkQueueSubmit(m_renderer.getGraphicsQueue(), 1, &submit, fence);

    // Present.
    m_renderer.presentSwapchainImage(imgIdx);

    m_metrics.endFrame();
    m_metrics.updateGPUMem(m_renderer.getAllocator());
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void App::keyCallback(GLFWwindow* win, int key, int scancode, int action, int mods)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(win));
    app->onKey(key, action);
}

void App::onKey(int key, int action)
{
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    if (key == GLFW_KEY_SPACE) {
        m_pendingModeToggle = true;
    } else if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) {
        m_depthBias += 0.0001f;
        printf("depthBias = %.5f\n", m_depthBias);
    } else if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
        m_depthBias -= 0.0001f;
        printf("depthBias = %.5f\n", m_depthBias);
    } else if (key == GLFW_KEY_ESCAPE && m_mouseCapture) {
        m_mouseCapture = false;
        glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

void App::cursorPosCallback(GLFWwindow* win, double x, double y)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(win));
    app->onMouseMove(x, y);
}

void App::mouseButtonCallback(GLFWwindow* win, int button, int action, int /*mods*/)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(win));
    app->onMouseButton(button, action);
}

void App::onMouseMove(double x, double y)
{
    if (!m_mouseCapture) return;
    if (m_firstMouse) {
        m_lastMouseX = x;
        m_lastMouseY = y;
        m_firstMouse = false;
    }
    double dx = x - m_lastMouseX;
    double dy = m_lastMouseY - y;  // reversed: y increases downward
    m_lastMouseX = x;
    m_lastMouseY = y;

    const float sensitivity = 0.002f;
    m_camYaw   += static_cast<float>(dx) * sensitivity;
    m_camPitch += static_cast<float>(dy) * sensitivity;
    m_camPitch  = glm::clamp(m_camPitch, -glm::radians(89.0f), glm::radians(89.0f));
}

void App::onMouseButton(int button, int action)
{
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        m_mouseCapture = !m_mouseCapture;
        m_firstMouse   = true;
        glfwSetInputMode(m_window,
                         GLFW_CURSOR,
                         m_mouseCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void App::cleanup()
{
    if (m_renderer.getDevice() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_renderer.getDevice());
        if (m_cmd != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(m_renderer.getDevice(), m_renderer.getCommandPool(), 1, &m_cmd);
            m_cmd = VK_NULL_HANDLE;
        }
    }
    if (m_hudVtxBuf != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_renderer.getAllocator(), m_hudVtxBuf, m_hudVtxAlloc);
        m_hudVtxBuf = VK_NULL_HANDLE;
    }

    m_ui.cleanup();
    m_renderer.cleanup();

    if (m_window) glfwDestroyWindow(m_window);
    glfwTerminate();
}
