// MirrorPSX native PlayStation 5 Vulkan UI host.
// RADV ICD, VK_KHR_display and scePad follow the OpenProspero/Hyperion
// integration; MirrorPSX continues to render its own Dear ImGui UI.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <volk.h>
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "receiver/state.hpp"
#include "ui/screen.hpp"
#include "ui/icons.hpp"
#include "ui/gamepad_navigation.hpp"
#ifdef MIRRORPSX_PS5_AIRPLAY
#include "receiver/airplay.hpp"
#include "platform/video_texture.hpp"
#include <memory>
#endif
#include <openprospero/input.hpp>
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>
#include <unistd.h>

extern "C" {
    VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance, const char*);
    int sceSystemServiceHideSplashScreen(void);
    // OpenProspero application-profile identity bridge. Hyperion observed
    // nullable POSIX identity imports on PS5 firmware; validate the libkernel
    // syscall gateway before starting RADV or any worker threads.
    int hyperion_ps5_identity_initialize(const void* getpid_stub, pid_t expected_pid);
    // The SDK's libkernel stub is optional in this application profile.
    // Keep stderr for environments where the debug channel is unavailable.
    int sceKernelDebugOutText(int channel, const char* text) __attribute__((weak));
}

namespace {
void ps5_log(const char* format, ...) {
    char message[512];
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (written < 0) return;
    std::fputs(message, stderr);
    std::fflush(stderr);
    if (sceKernelDebugOutText) sceKernelDebugOutText(0, message);
}

struct Ps5Vulkan {
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    uint32_t family{};
    VkDescriptorPool descriptors{};
    ImGui_ImplVulkanH_Window window{};
    bool initialized{};
    bool swapchain_dirty{};
    bool last_frame_presented{};

    static bool ok(VkResult code, const char* what) {
        if (code == VK_SUCCESS || code == VK_SUBOPTIMAL_KHR) return true;
        ps5_log("MirrorPSX PS5: %s failed: VkResult %d\n", what, int(code));
        return false;
    }

    bool create_surface() {
        if (!vkGetPhysicalDeviceDisplayPropertiesKHR ||
            !vkGetPhysicalDeviceDisplayPlanePropertiesKHR ||
            !vkGetDisplayModePropertiesKHR ||
            !vkGetDisplayPlaneSupportedDisplaysKHR ||
            !vkGetDisplayPlaneCapabilitiesKHR ||
            !vkCreateDisplayPlaneSurfaceKHR) {
            ps5_log("MirrorPSX PS5: VK_KHR_display entrypoints unavailable\n");
            return false;
        }
        uint32_t count = 0;
        if (!ok(vkEnumeratePhysicalDevices(instance, &count, nullptr), "enumerate GPUs") || !count)
            return false;
        std::vector<VkPhysicalDevice> devices(count);
        if (!ok(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "get GPUs")) return false;
        for (auto gpu : devices) {
            uint32_t display_count = 0, plane_count = 0;
            if (!ok(vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &display_count, nullptr),
                    "display query") ||
                !ok(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(gpu, &plane_count, nullptr),
                    "display plane query")) continue;
            if (!display_count || !plane_count) continue;
            std::vector<VkDisplayPropertiesKHR> displays(display_count);
            std::vector<VkDisplayPlanePropertiesKHR> planes(plane_count);
            if (!ok(vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &display_count, displays.data()),
                    "display enumeration") ||
                !ok(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(gpu, &plane_count, planes.data()),
                    "display plane enumeration")) continue;
            for (const auto& display : displays) {
                uint32_t mode_count = 0;
                if (!ok(vkGetDisplayModePropertiesKHR(gpu, display.display, &mode_count, nullptr),
                        "display mode query")) continue;
                if (!mode_count) continue;
                std::vector<VkDisplayModePropertiesKHR> modes(mode_count);
                if (!ok(vkGetDisplayModePropertiesKHR(gpu, display.display, &mode_count, modes.data()),
                        "display mode enumeration")) continue;
                // Prefer the physical display's 4K mode, but accept 1080p/720p.
                std::stable_sort(modes.begin(), modes.end(), [](const auto& a, const auto& b) {
                    const auto pa = a.parameters.visibleRegion, pb = b.parameters.visibleRegion;
                    return uint64_t(pa.width) * pa.height > uint64_t(pb.width) * pb.height;
                });
                for (const auto& mode : modes) {
                    // ImGui TV layout and the current render target are capped at 4K.
                    // In particular, never auto-select a possible 8K display mode.
                    if (!mode.parameters.visibleRegion.width ||
                        !mode.parameters.visibleRegion.height ||
                        mode.parameters.visibleRegion.width > 3840 ||
                        mode.parameters.visibleRegion.height > 2160) continue;
                    for (uint32_t plane = 0; plane < plane_count; ++plane) {
                        uint32_t available = 0;
                        if (!ok(vkGetDisplayPlaneSupportedDisplaysKHR(gpu, plane, &available, nullptr),
                                "supported display query")) continue;
                        std::vector<VkDisplayKHR> supported(available);
                        if (available &&
                            !ok(vkGetDisplayPlaneSupportedDisplaysKHR(gpu, plane, &available, supported.data()),
                                "supported display enumeration")) continue;
                        if (std::find(supported.begin(), supported.end(), display.display) == supported.end())
                            continue;
                        VkDisplayPlaneCapabilitiesKHR caps{};
                        if (!ok(vkGetDisplayPlaneCapabilitiesKHR(gpu, mode.displayMode, plane, &caps),
                                "plane capabilities")) continue;
                        if (!(caps.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR)) continue;
                        VkDisplaySurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR};
                        sci.displayMode = mode.displayMode;
                        sci.planeIndex = plane;
                        sci.planeStackIndex = planes[plane].currentStackIndex;
                        sci.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
                        sci.globalAlpha = 1.0f;
                        sci.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
                        sci.imageExtent = mode.parameters.visibleRegion;
                        VkSurfaceKHR surface{};
                        if (!ok(vkCreateDisplayPlaneSurfaceKHR(instance, &sci, nullptr, &surface),
                                "create display surface")) continue;
                        // Presentation support must be checked for the chosen GPU,
                        // not ImGui's first enumerated physical device.
                        uint32_t families = 0;
                        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, nullptr);
                        if (!families) {
                            vkDestroySurfaceKHR(instance, surface, nullptr);
                            continue;
                        }
                        std::vector<VkQueueFamilyProperties> props(families);
                        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, props.data());
                        for (uint32_t i = 0; i < families; ++i) {
                            VkBool32 present = VK_FALSE;
                            if (!ok(vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present),
                                    "surface present support")) continue;
                            if (present && (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                                physical = gpu;
                                family = i;
                                window.Surface = surface;
                                window.Width = int(sci.imageExtent.width);
                                window.Height = int(sci.imageExtent.height);
                                VkPhysicalDeviceProperties gpu_props{};
                                vkGetPhysicalDeviceProperties(gpu, &gpu_props);
                                ps5_log("MirrorPSX PS5: selected GPU vendor=%04x device=%04x queue=%u display=%dx%d\n",
                                        gpu_props.vendorID, gpu_props.deviceID, family,
                                        window.Width, window.Height);
                                return true;
                            }
                        }
                        vkDestroySurfaceKHR(instance, surface, nullptr);
                    }
                }
            }
        }
        ps5_log("MirrorPSX PS5: no supported Vulkan display plane <=3840x2160\n");
        return false;
    }

    bool start() {
        // PS5 does not provide a host Vulkan loader. RADV supplies the ICD.
        ps5_log("MirrorPSX PS5: initializing RADV/Volk ICD\n");
        volkInitializeCustom(vk_icdGetInstanceProcAddr);
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "MirrorPSX";
        app.apiVersion = VK_API_VERSION_1_1;
        const char* instance_extensions[] = {
            VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME
        };
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = 2;
        ci.ppEnabledExtensionNames = instance_extensions;
        if (!ok(vkCreateInstance(&ci, nullptr, &instance), "instance")) return false;
        volkLoadInstance(instance);
        ps5_log("MirrorPSX PS5: Vulkan instance initialized\n");
        if (!create_surface()) return false;
        const float priority = 1;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex = family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        const char* swap = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        di.queueCreateInfoCount = 1;
        di.pQueueCreateInfos = &qi;
        di.enabledExtensionCount = 1;
        di.ppEnabledExtensionNames = &swap;
        if (!ok(vkCreateDevice(physical, &di, nullptr, &device), "device")) return false;
        volkLoadDevice(device);
        vkGetDeviceQueue(device, family, 0, &queue);
        ps5_log("MirrorPSX PS5: graphics device/queue initialized\n");
        VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool.maxSets = 256;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = pool_sizes;
        if (!ok(vkCreateDescriptorPool(device, &pool, nullptr, &descriptors), "descriptor pool"))
            return false;
        const VkFormat formats[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
        window.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
            physical, window.Surface, formats, 2, VK_COLORSPACE_SRGB_NONLINEAR_KHR);
        const VkPresentModeKHR fifo[] = {VK_PRESENT_MODE_FIFO_KHR};
        window.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(physical, window.Surface, fifo, 1);
        ImGui_ImplVulkanH_CreateOrResizeWindow(instance, physical, device, &window, family,
                                               nullptr, window.Width, window.Height, 2,
                                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        if (!window.RenderPass || !window.Swapchain) {
            ps5_log("MirrorPSX PS5: failed to create Vulkan swapchain\n");
            return false;
        }
        ps5_log("MirrorPSX PS5: Vulkan swapchain initialized, images=%u\n", window.ImageCount);
        initialized = true;
        return true;
    }

    bool present(ImDrawData* draw) {
        // Dear ImGui's Vulkan helper owns per-frame fences, command pools and
        // present semaphores, avoiding duplicated Hyperion graphics state.
        auto& wd = window;
        last_frame_presented = false;
        const auto acquired = wd.FrameSemaphores[wd.SemaphoreIndex].ImageAcquiredSemaphore;
        const auto completed = wd.FrameSemaphores[wd.SemaphoreIndex].RenderCompleteSemaphore;
        VkResult result = vkAcquireNextImageKHR(device, wd.Swapchain, 5000000000ULL,
                                               acquired, VK_NULL_HANDLE, &wd.FrameIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchain_dirty = true;
            return true;
        }
        if (!ok(result, "acquire")) return false;
        if (result == VK_SUBOPTIMAL_KHR) swapchain_dirty = true;
        auto& frame = wd.Frames[wd.FrameIndex];
        if (!ok(vkWaitForFences(device, 1, &frame.Fence, VK_TRUE, 5000000000ULL), "frame wait"))
            return false;
        if (!ok(vkResetFences(device, 1, &frame.Fence), "fence reset")) return false;
        if (!ok(vkResetCommandPool(device, frame.CommandPool, 0), "command pool reset"))
            return false;
        VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        cb.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!ok(vkBeginCommandBuffer(frame.CommandBuffer, &cb), "begin commands")) return false;
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = wd.RenderPass;
        rp.framebuffer = frame.Framebuffer;
        rp.renderArea.extent = {static_cast<uint32_t>(wd.Width), static_cast<uint32_t>(wd.Height)};
        VkClearValue clear{};
        clear.color = {{0.035f, 0.052f, 0.10f, 1.0f}};
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(frame.CommandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(draw, frame.CommandBuffer);
        vkCmdEndRenderPass(frame.CommandBuffer);
        if (!ok(vkEndCommandBuffer(frame.CommandBuffer), "end commands")) return false;
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &acquired;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &frame.CommandBuffer;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &completed;
        if (!ok(vkQueueSubmit(queue, 1, &submit, frame.Fence), "submit")) return false;
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &completed;
        present.swapchainCount = 1;
        present.pSwapchains = &wd.Swapchain;
        present.pImageIndices = &wd.FrameIndex;
        result = vkQueuePresentKHR(queue, &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchain_dirty = true;
            return true;
        }
        if (!ok(result, "present")) return false;
        if (result == VK_SUBOPTIMAL_KHR) swapchain_dirty = true;
        wd.SemaphoreIndex = (wd.SemaphoreIndex + 1) % wd.SemaphoreCount;
        last_frame_presented = true;
        return true;
    }

    bool rebuild() {
        if (!swapchain_dirty) return true;
        ps5_log("MirrorPSX PS5: recreating Vulkan swapchain\n");
        if (!ok(vkDeviceWaitIdle(device), "swapchain device wait")) return false;
        ImGui_ImplVulkan_SetMinImageCount(2);
        ImGui_ImplVulkanH_CreateOrResizeWindow(instance, physical, device, &window,
            family, nullptr, window.Width, window.Height, 2,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        if (!window.Swapchain || !window.RenderPass) return false;
        ImGui_ImplVulkan_PipelineInfo pipeline{};
        pipeline.RenderPass = window.RenderPass;
        pipeline.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ImGui_ImplVulkan_CreateMainPipeline(&pipeline);
        window.FrameIndex = 0;
        swapchain_dirty = false;
        return true;
    }
    void shutdown() {
        if (device) vkDeviceWaitIdle(device);
        if (device && (initialized || window.Swapchain || window.RenderPass))
            ImGui_ImplVulkanH_DestroyWindow(instance, device, &window, nullptr);
        if (descriptors) vkDestroyDescriptorPool(device, descriptors, nullptr);
        if (device) vkDestroyDevice(device, nullptr);
        if (window.Surface) vkDestroySurfaceKHR(instance, window.Surface, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
    }
};

bool load_ui_fonts(ImFont*& regular, ImFont*& strong, ImFont*& icons) {
    auto& atlas = ImGui::GetIO().Fonts;
    regular = atlas->AddFontFromFileTTF("/app0/assets/fonts/Figtree-400.ttf", 26.0f);
    strong = atlas->AddFontFromFileTTF("/app0/assets/fonts/Figtree-650.ttf", 26.0f);
    icons = atlas->AddFontFromFileTTF("/app0/assets/fonts/fa-solid-900.ttf", 30.0f,
                                      nullptr, mirror::icons::font_ranges);
    if (!regular || !strong || !icons) {
        ps5_log("MirrorPSX PS5: font assets missing or unreadable\n");
        return false;
    }
    return true;
}
} // namespace

extern "C" int main() {
#ifdef MIRRORPSX_PS5_AIRPLAY
    ps5_log("MirrorPSX PS5: experimental Vulkan / AirPlay milestone\n");
#else
    ps5_log("MirrorPSX PS5: starting Vulkan UI-only milestone\n");
#endif
    if (hyperion_ps5_identity_initialize(reinterpret_cast<const void*>(getpid), getpid()) != 0) {
        ps5_log("MirrorPSX PS5: libkernel identity initialization failed\n");
        return 1;
    }
    ps5_log("MirrorPSX PS5: libkernel identity initialized\n");
    Ps5Vulkan graphics;
    if (!graphics.start()) {
        graphics.shutdown();
        return 1;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImFont *regular = nullptr, *bold = nullptr, *icons = nullptr;
    if (!load_ui_fonts(regular, bold, icons)) {
        ImGui::DestroyContext();
        graphics.shutdown();
        return 2;
    }
    mirror::apply_screen_style(regular, bold, icons);
    ps5_log("MirrorPSX PS5: UI fonts and style ready\n");
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_1;
    info.Instance = graphics.instance;
    info.PhysicalDevice = graphics.physical;
    info.Device = graphics.device;
    info.QueueFamily = graphics.family;
    info.Queue = graphics.queue;
    info.DescriptorPool = graphics.descriptors;
    info.PipelineInfoMain.RenderPass = graphics.window.RenderPass;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.MinImageCount = 2;
    info.ImageCount = graphics.window.ImageCount;
    if (!ImGui_ImplVulkan_Init(&info)) {
        ps5_log("MirrorPSX PS5: ImGui Vulkan backend initialization failed\n");
        ImGui::DestroyContext();
        graphics.shutdown();
        return 3;
    }
    ps5_log("MirrorPSX PS5: ImGui Vulkan backend initialized\n");
    mirror::Snapshot snapshot;
    snapshot.name = "MirrorPSX on PS5";
    snapshot.listening = false;
    snapshot.error.clear();
#ifdef MIRRORPSX_PS5_AIRPLAY
    mirror::State airplay_state;
    mirror::AirPlay receiver(airplay_state);
    auto video_texture = std::make_unique<mirror::VideoTexture>(graphics.physical,graphics.device);
    uint64_t last_video_frame = 0;
#endif
    mirror::ScreenUiState screen{};
    openprospero::ps5::GamePad controller;
    int pad_status = controller.open();
    ps5_log("MirrorPSX PS5: scePad open result %d\n", pad_status);
    auto previous = std::chrono::steady_clock::now();
    float exit_hold = 0.0f;
    bool first_frame = true;
    unsigned reconnect_frames = 0;
    bool run = true;
    while (run) {
        if (!graphics.rebuild()) {
            ps5_log("MirrorPSX PS5: swapchain recovery failed\n");
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        io.DeltaTime = std::clamp(std::chrono::duration<float>(now - previous).count(),
                                  1.0f / 240.0f, .05f);
        previous = now;
        io.DisplaySize = {float(graphics.window.Width), float(graphics.window.Height)};
        io.DisplayFramebufferScale = {1, 1};
        openprospero::ps5::GamePadState buttons{};
        const int pad_read = controller.read(buttons);
        if (pad_read < 0) {
            // Reacquire a disconnected/late-starting DualSense periodically,
            // without busy-spinning the user service on every video frame.
            if (++reconnect_frames >= 120) {
                controller.close();
                pad_status = controller.open();
                if (pad_status < 0)
                    ps5_log("MirrorPSX PS5: controller reopen failed %d\n", pad_status);
                reconnect_frames = 0;
            }
        } else {
            reconnect_frames = 0;
        }
        // PS5 controller-only sessions need an exit path independent of the
        // disconnected network runtime. Require a deliberate OPTIONS hold.
        if (buttons.connected && (buttons.buttons & SCE_PAD_BUTTON_OPTIONS))
            exit_hold += io.DeltaTime;
        else
            exit_hold = 0.0f;
        if (exit_hold >= 1.2f) {
            ps5_log("MirrorPSX PS5: exit requested by OPTIONS hold\n");
            break;
        }
        mirror::GamepadNavigation pad{};
        pad.connected = buttons.connected;
        if (buttons.buttons & SCE_PAD_BUTTON_CROSS) pad.buttons |= mirror::pad_confirm;
        if (buttons.buttons & SCE_PAD_BUTTON_CIRCLE) pad.buttons |= mirror::pad_cancel;
        if (buttons.buttons & SCE_PAD_BUTTON_UP) pad.buttons |= mirror::pad_up;
        if (buttons.buttons & SCE_PAD_BUTTON_DOWN) pad.buttons |= mirror::pad_down;
        if (buttons.buttons & SCE_PAD_BUTTON_LEFT) pad.buttons |= mirror::pad_left;
        if (buttons.buttons & SCE_PAD_BUTTON_RIGHT) pad.buttons |= mirror::pad_right;
        if (buttons.buttons & SCE_PAD_BUTTON_R1) pad.buttons |= mirror::pad_fullscreen;
        mirror::submit_gamepad_navigation(io, pad);
        // Controller-specific help shortcut mirrors the Windows H binding.
        io.AddKeyEvent(ImGuiKey_H, buttons.connected &&
                        (buttons.buttons & SCE_PAD_BUTTON_TRIANGLE));
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();
        ImTextureID video_handle = ImTextureID_Invalid;
        int video_width = 0, video_height = 0;
#ifdef MIRRORPSX_PS5_AIRPLAY
        mirror::VideoFrame frame;
        if (airplay_state.frame_after(last_video_frame, frame)) {
            last_video_frame = frame.sequence;
            if (!video_texture->upload(graphics.queue, graphics.family, frame))
                airplay_state.update([](mirror::Snapshot& s) {
                    s.error = "Vulkan video texture upload failed";
                });
        }
        snapshot = airplay_state.snapshot();
        if (!snapshot.width || !snapshot.height) video_texture->clear();
        video_handle = snapshot.width && snapshot.height
            ? static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(video_texture->descriptor()))
            : ImTextureID_Invalid;
        video_width = video_texture->width();
        video_height = video_texture->height();
#endif
        switch (mirror::render_screen(snapshot, video_handle, video_width, video_height, screen)) {
            case mirror::ScreenAction::start:
#ifdef MIRRORPSX_PS5_AIRPLAY
                receiver.start("MirrorPSX PS5");
#else
                snapshot.error = "AirPlay networking is not included in this UI test build";
#endif
                break;
            case mirror::ScreenAction::stop:
#ifdef MIRRORPSX_PS5_AIRPLAY
                receiver.stop();
#endif
                break;
            case mirror::ScreenAction::none: break;
        }
        // UI remains fully inside the display's television safe area. The
        // native Exit hint is platform-specific and does not affect Windows.
        const float scale = std::min(io.DisplaySize.x / 1920.0f, io.DisplaySize.y / 1080.0f);
        const char* hint = "Hold OPTIONS to exit";
        ImGui::GetForegroundDrawList()->AddText(bold, 18.0f * scale,
            {io.DisplaySize.x - 290.0f * scale, 1048.0f * scale},
            IM_COL32(170, 185, 205, 240), hint);
        ImGui::Render();
        if (!graphics.present(ImGui::GetDrawData())) {
            ps5_log("MirrorPSX PS5: presentation failed\n");
            run = false;
        }
        if (first_frame && graphics.last_frame_presented) {
            sceSystemServiceHideSplashScreen();
            ps5_log("MirrorPSX PS5: first Vulkan frame submitted for presentation\n");
            first_frame = false;
        }
    }
#ifdef MIRRORPSX_PS5_AIRPLAY
    receiver.stop();
    video_texture.reset(); // Must destroy Vulkan textures before ImGui backend.
#endif
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
    graphics.shutdown();
    return 0;
}
