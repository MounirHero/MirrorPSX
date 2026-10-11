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
#include <openprospero/input.hpp>
#include <algorithm>
#include <chrono>
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
}

namespace {
struct Ps5Vulkan {
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    uint32_t family{};
    VkDescriptorPool descriptors{};
    ImGui_ImplVulkanH_Window window{};
    bool initialized{};

    static bool ok(VkResult code, const char* what) {
        if (code == VK_SUCCESS || code == VK_SUBOPTIMAL_KHR) return true;
        std::fprintf(stderr, "MirrorPSX PS5: %s failed: VkResult %d\n", what, int(code));
        return false;
    }

    bool create_surface() {
        uint32_t count = 0;
        if (!ok(vkEnumeratePhysicalDevices(instance, &count, nullptr), "enumerate GPUs") || !count)
            return false;
        std::vector<VkPhysicalDevice> devices(count);
        if (!ok(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "get GPUs")) return false;
        for (auto gpu : devices) {
            uint32_t display_count = 0, plane_count = 0;
            vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &display_count, nullptr);
            vkGetPhysicalDeviceDisplayPlanePropertiesKHR(gpu, &plane_count, nullptr);
            if (!display_count || !plane_count) continue;
            std::vector<VkDisplayPropertiesKHR> displays(display_count);
            std::vector<VkDisplayPlanePropertiesKHR> planes(plane_count);
            vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &display_count, displays.data());
            vkGetPhysicalDeviceDisplayPlanePropertiesKHR(gpu, &plane_count, planes.data());
            for (const auto& display : displays) {
                uint32_t mode_count = 0;
                vkGetDisplayModePropertiesKHR(gpu, display.display, &mode_count, nullptr);
                if (!mode_count) continue;
                std::vector<VkDisplayModePropertiesKHR> modes(mode_count);
                vkGetDisplayModePropertiesKHR(gpu, display.display, &mode_count, modes.data());
                // Prefer the physical display's 4K mode, but accept 1080p/720p.
                std::stable_sort(modes.begin(), modes.end(), [](const auto& a, const auto& b) {
                    const auto pa = a.parameters.visibleRegion, pb = b.parameters.visibleRegion;
                    return uint64_t(pa.width) * pa.height > uint64_t(pb.width) * pb.height;
                });
                for (const auto& mode : modes) {
                    for (uint32_t plane = 0; plane < plane_count; ++plane) {
                        uint32_t available = 0;
                        vkGetDisplayPlaneSupportedDisplaysKHR(gpu, plane, &available, nullptr);
                        std::vector<VkDisplayKHR> supported(available);
                        if (available) vkGetDisplayPlaneSupportedDisplaysKHR(gpu, plane, &available, supported.data());
                        if (std::find(supported.begin(), supported.end(), display.display) == supported.end())
                            continue;
                        VkDisplayPlaneCapabilitiesKHR caps{};
                        vkGetDisplayPlaneCapabilitiesKHR(gpu, mode.displayMode, plane, &caps);
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
                        std::vector<VkQueueFamilyProperties> props(families);
                        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, props.data());
                        for (uint32_t i = 0; i < families; ++i) {
                            VkBool32 present = VK_FALSE;
                            vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present);
                            if (present && (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                                physical = gpu;
                                family = i;
                                window.Surface = surface;
                                window.Width = int(sci.imageExtent.width);
                                window.Height = int(sci.imageExtent.height);
                                std::fprintf(stderr, "MirrorPSX PS5: display %dx%d\n",
                                             window.Width, window.Height);
                                return true;
                            }
                        }
                        vkDestroySurfaceKHR(instance, surface, nullptr);
                    }
                }
            }
        }
        std::fprintf(stderr, "MirrorPSX PS5: no supported Vulkan display plane\n");
        return false;
    }

    bool start() {
        // PS5 does not provide a host Vulkan loader. RADV supplies the ICD.
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
            std::fprintf(stderr, "MirrorPSX PS5: failed to create Vulkan swapchain\n");
            return false;
        }
        initialized = true;
        return true;
    }

    bool present(ImDrawData* draw) {
        // Dear ImGui's Vulkan helper owns per-frame fences, command pools and
        // present semaphores, avoiding duplicated Hyperion graphics state.
        auto& wd = window;
        const auto acquired = wd.FrameSemaphores[wd.SemaphoreIndex].ImageAcquiredSemaphore;
        const auto completed = wd.FrameSemaphores[wd.SemaphoreIndex].RenderCompleteSemaphore;
        VkResult result = vkAcquireNextImageKHR(device, wd.Swapchain, 5000000000ULL,
                                               acquired, VK_NULL_HANDLE, &wd.FrameIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) return false;
        if (!ok(result, "acquire")) return false;
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
        if (!ok(result, "present")) return false;
        wd.SemaphoreIndex = (wd.SemaphoreIndex + 1) % wd.SemaphoreCount;
        return true;
    }
    void shutdown() {
        if (device) vkDeviceWaitIdle(device);
        if (initialized) ImGui_ImplVulkanH_DestroyWindow(instance, device, &window, nullptr);
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
        std::fprintf(stderr, "MirrorPSX PS5: font assets missing or unreadable\n");
        return false;
    }
    return true;
}
} // namespace

extern "C" int main() {
    std::fprintf(stderr, "MirrorPSX PS5: starting Vulkan UI-only milestone\n");
    if (hyperion_ps5_identity_initialize(reinterpret_cast<const void*>(getpid), getpid()) != 0) {
        std::fprintf(stderr, "MirrorPSX PS5: libkernel identity initialization failed\n");
        return 1;
    }
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
        std::fprintf(stderr, "MirrorPSX PS5: ImGui Vulkan backend initialization failed\n");
        ImGui::DestroyContext();
        graphics.shutdown();
        return 3;
    }
    mirror::Snapshot snapshot;
    snapshot.name = "MirrorPSX on PS5";
    snapshot.listening = false;  // Never pretend AirPlay is live in UI-only PKG.
    snapshot.error.clear();
    mirror::ScreenUiState screen{};
    openprospero::ps5::GamePad controller;
    const int pad_status = controller.open();
    std::fprintf(stderr, "MirrorPSX PS5: scePad open result %d\n", pad_status);
    auto previous = std::chrono::steady_clock::now();
    float exit_hold = 0.0f;
    bool first_frame = true;
    bool run = true;
    while (run) {
        const auto now = std::chrono::steady_clock::now();
        io.DeltaTime = std::clamp(std::chrono::duration<float>(now - previous).count(),
                                  1.0f / 240.0f, .05f);
        previous = now;
        io.DisplaySize = {float(graphics.window.Width), float(graphics.window.Height)};
        io.DisplayFramebufferScale = {1, 1};
        openprospero::ps5::GamePadState buttons{};
        controller.read(buttons);
        // PS5 controller-only sessions need an exit path independent of the
        // disconnected network runtime. Require a deliberate OPTIONS hold.
        if (buttons.connected && (buttons.buttons & SCE_PAD_BUTTON_OPTIONS))
            exit_hold += io.DeltaTime;
        else
            exit_hold = 0.0f;
        if (exit_hold >= 1.2f) {
            std::fprintf(stderr, "MirrorPSX PS5: exit requested by OPTIONS hold\n");
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
        switch (mirror::render_screen(snapshot, ImTextureID_Invalid, 0, 0, screen)) {
            case mirror::ScreenAction::start:
                snapshot.error = "AirPlay networking is not included in this UI test build";
                break;
            case mirror::ScreenAction::stop: break;
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
            std::fprintf(stderr, "MirrorPSX PS5: presentation failed\n");
            run = false;
        }
        if (first_frame) {
            sceSystemServiceHideSplashScreen();
            std::fprintf(stderr, "MirrorPSX PS5: first Vulkan frame presented\n");
            first_frame = false;
        }
    }
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
    graphics.shutdown();
    return 0;
}
