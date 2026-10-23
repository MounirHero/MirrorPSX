// SPDX-License-Identifier: GPL-3.0-or-later
// Frame submission adapted from Dear ImGui's Vulkan example (MIT).
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "platform/ps5_identity.h"
#include "platform/ps5_input.hpp"
#include "platform/video_texture.hpp"
#include "receiver/airplay.hpp"
#include "receiver/dlna.hpp"
#include "ui/screen.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <fcntl.h>
#include <ps5platform/klog.h>
#include <stdexcept>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <volk.h>
extern "C" {
PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance, const char *);
int sceSystemServiceHideSplashScreen(void);
int sceSystemServiceLoadExec(const char *, const char *const *);
int sceKernelUsleep(unsigned);
}
namespace {
const char *startup_stage = "entry";
void log(const char *format, ...) {
  char message[512];
  va_list args;
  va_start(args, format);
  std::vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  char line[560];
  std::snprintf(line, sizeof(line), "[MirrorPSX] %s\n", message);
  sceKernelDebugOutText(0, line);
}
void stage(const char *name) {
  startup_stage = name;
  log("Startup: %s", name);
}
} // namespace
// Ask the shell to close the title rather than returning into the CRT's exit.
[[noreturn]] void shutdown_to_shell(int status) {
  log("Shell shutdown (%d), last stage: %s", status, startup_stage);
  std::fflush(nullptr);
  sceSystemServiceLoadExec("exit", nullptr);
  for (;;)
    sceKernelUsleep(100000);
}
namespace {
void check(VkResult result) {
  if (result < 0) {
    log("Vulkan failure %d at %s", int(result), startup_stage);
    throw std::runtime_error("Vulkan error " + std::to_string(result));
  }
}
double seconds() {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}
struct Renderer {
  VkInstance instance{};
  VkPhysicalDevice physical{};
  VkDevice device{};
  VkQueue queue{};
  uint32_t family = 0;
  bool context = false, backend = false;
  ImGui_ImplVulkanH_Window window;
  ~Renderer() {
    if (device)
      vkDeviceWaitIdle(device);
    if (backend)
      ImGui_ImplVulkan_Shutdown();
    if (context)
      ImGui::DestroyContext();
    if (device)
      ImGui_ImplVulkanH_DestroyWindow(instance, device, &window, nullptr);
    else if (window.Surface)
      vkDestroySurfaceKHR(instance, window.Surface, nullptr);
    if (device)
      vkDestroyDevice(device, nullptr);
    if (instance)
      vkDestroyInstance(instance, nullptr);
  }
  void initialize() {
    stage("Vulkan entry points");
    volkInitializeCustom(vk_icdGetInstanceProcAddr);
    const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                VK_KHR_DISPLAY_EXTENSION_NAME};
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "MirrorPSX";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &app;
    create.enabledExtensionCount = 2;
    create.ppEnabledExtensionNames = extensions;
    stage("vkCreateInstance");
    check(vkCreateInstance(&create, nullptr, &instance));
    volkLoadInstance(instance);
    uint32_t n = 0;
    stage("enumerate GPU");
    check(vkEnumeratePhysicalDevices(instance, &n, nullptr));
    if (!n)
      throw std::runtime_error("RADV: no GPU");
    std::vector<VkPhysicalDevice> devices(n);
    check(vkEnumeratePhysicalDevices(instance, &n, devices.data()));
    physical = devices[0];
    stage("enumerate VideoOut display");
    check(vkGetPhysicalDeviceDisplayPropertiesKHR(physical, &n, nullptr));
    if (!n)
      throw std::runtime_error("VideoOut: no display");
    std::vector<VkDisplayPropertiesKHR> displays(n);
    check(
        vkGetPhysicalDeviceDisplayPropertiesKHR(physical, &n, displays.data()));
    check(vkGetDisplayModePropertiesKHR(physical, displays[0].display, &n,
                                        nullptr));
    if (!n)
      throw std::runtime_error("VideoOut: no display modes");
    std::vector<VkDisplayModePropertiesKHR> modes(n);
    check(vkGetDisplayModePropertiesKHR(physical, displays[0].display, &n,
                                        modes.data()));
    auto mode = modes[0];
    for (const auto &m : modes)
      if (m.parameters.visibleRegion.width == 1920 &&
          m.parameters.visibleRegion.height == 1080 &&
          m.parameters.refreshRate < 70000) {
        mode = m;
        break;
      }
    VkDisplaySurfaceCreateInfoKHR surface{
        VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR};
    surface.displayMode = mode.displayMode;
    surface.imageExtent = mode.parameters.visibleRegion;
    stage("select display plane");
    check(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physical, &n, nullptr));
    std::vector<VkDisplayPlanePropertiesKHR> planes(n);
    check(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physical, &n,
                                                       planes.data()));
    bool plane_found = false;
    for (uint32_t plane = 0; plane < n; ++plane) {
      uint32_t count = 0;
      check(vkGetDisplayPlaneSupportedDisplaysKHR(physical, plane, &count,
                                                  nullptr));
      std::vector<VkDisplayKHR> supported(count);
      check(vkGetDisplayPlaneSupportedDisplaysKHR(physical, plane, &count,
                                                  supported.data()));
      VkDisplayPlaneCapabilitiesKHR capabilities{};
      check(vkGetDisplayPlaneCapabilitiesKHR(physical, mode.displayMode, plane,
                                             &capabilities));
      if (std::find(supported.begin(), supported.end(), displays[0].display) !=
              supported.end() &&
          (capabilities.supportedAlpha &
           VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR)) {
        surface.planeIndex = plane;
        surface.planeStackIndex = planes[plane].currentStackIndex;
        plane_found = true;
        break;
      }
    }
    if (!plane_found)
      throw std::runtime_error("VideoOut: no compatible display plane");
    surface.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    surface.globalAlpha = 1;
    surface.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    stage("create display surface");
    check(vkCreateDisplayPlaneSurfaceKHR(instance, &surface, nullptr,
                                         &window.Surface));
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, nullptr);
    std::vector<VkQueueFamilyProperties> families(n);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, families.data());
    bool found = false;
    for (uint32_t i = 0; i < n; ++i) {
      VkBool32 supported = false;
      check(vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, window.Surface,
                                                 &supported));
      if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && supported) {
        family = i;
        found = true;
        break;
      }
    }
    if (!found)
      throw std::runtime_error("RADV: no presentation queue");
    float priority = 1;
    VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qc.queueFamilyIndex = family;
    qc.queueCount = 1;
    qc.pQueuePriorities = &priority;
    const char *swap = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dc.queueCreateInfoCount = 1;
    dc.pQueueCreateInfos = &qc;
    dc.enabledExtensionCount = 1;
    dc.ppEnabledExtensionNames = &swap;
    stage("vkCreateDevice");
    check(vkCreateDevice(physical, &dc, nullptr, &device));
    volkLoadDevice(device);
    vkGetDeviceQueue(device, family, 0, &queue);
    const VkFormat formats[] = {VK_FORMAT_B8G8R8A8_UNORM,
                                VK_FORMAT_R8G8B8A8_UNORM};
    window.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        physical, window.Surface, formats, 2,
        VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
    window.PresentMode = VK_PRESENT_MODE_FIFO_KHR;
    stage("create swapchain");
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        instance, physical, device, &window, family, nullptr,
        surface.imageExtent.width, surface.imageExtent.height, 3);
    ImGui::CreateContext();
    context = true;
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendPlatformName = "MirrorPSX DualSense";
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = app.apiVersion;
    info.Instance = instance;
    info.PhysicalDevice = physical;
    info.Device = device;
    info.QueueFamily = family;
    info.Queue = queue;
    info.DescriptorPoolSize = 64;
    info.RenderPass = window.RenderPass;
    info.MinImageCount = 3;
    info.ImageCount = window.ImageCount;
    info.CheckVkResultFn = check;
    stage("initialize ImGui Vulkan");
    if (!ImGui_ImplVulkan_Init(&info))
      throw std::runtime_error("ImGui Vulkan initialization failed");
    backend = true;
    std::fprintf(stderr, "[MirrorPSX] Display %dx%d\n", window.Width,
                 window.Height);
  }
  void render() {
    auto &sem = window.FrameSemaphores[window.SemaphoreIndex];
    check(vkAcquireNextImageKHR(device, window.Swapchain, UINT64_MAX,
                                sem.ImageAcquiredSemaphore, VK_NULL_HANDLE,
                                &window.FrameIndex));
    auto &f = window.Frames[window.FrameIndex];
    check(vkWaitForFences(device, 1, &f.Fence, VK_TRUE, UINT64_MAX));
    check(vkResetCommandPool(device, f.CommandPool, 0));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(f.CommandBuffer, &begin));
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = window.RenderPass;
    pass.framebuffer = f.Framebuffer;
    pass.renderArea.extent = {uint32_t(window.Width), uint32_t(window.Height)};
    pass.clearValueCount = 1;
    pass.pClearValues = &window.ClearValue;
    vkCmdBeginRenderPass(f.CommandBuffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), f.CommandBuffer);
    vkCmdEndRenderPass(f.CommandBuffer);
    check(vkEndCommandBuffer(f.CommandBuffer));
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &sem.ImageAcquiredSemaphore;
    submit.pWaitDstStageMask = &stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &f.CommandBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &sem.RenderCompleteSemaphore;
    check(vkResetFences(device, 1, &f.Fence));
    check(vkQueueSubmit(queue, 1, &submit, f.Fence));
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &sem.RenderCompleteSemaphore;
    present.swapchainCount = 1;
    present.pSwapchains = &window.Swapchain;
    present.pImageIndices = &window.FrameIndex;
    check(vkQueuePresentKHR(queue, &present));
    window.SemaphoreIndex = (window.SemaphoreIndex + 1) % window.SemaphoreCount;
  }
};
} // namespace
int main() {
  log("Build R10: pairing retries and prompt framing");
  try {
    stage("initialize native identity");
    if (mirror_ps5_identity_initialize(reinterpret_cast<const void *>(getpid),
                                       getpid()) != 0)
      throw std::runtime_error("Native identity initialization failed, errno " +
                               std::to_string(errno));
    log("stderr capture result: %d",
        ps5_klog_capture_stderr("[MirrorPSX runtime] "));
    stage("check packaged assets by absolute path");
    const char *files[] = {"/app0/assets/ui/atlas.rgba",
                           "/app0/assets/ui/fonts/Inter-Regular.ttf",
                           "/app0/assets/ui/fonts/Inter-SemiBold.ttf"};
    for (const auto *path : files) {
      const int fd = open(path, O_RDONLY);
      if (fd < 0) {
        log("Cannot open %s: errno %d", path, errno);
        throw std::runtime_error("Packaged asset missing");
      }
      close(fd);
    }
    Renderer renderer;
    renderer.initialize();
    mirror::Screen screen;
    stage("load fonts and UI atlas");
    if (!screen.initialize())
      throw std::runtime_error("UI assets missing");
    mirror::Ps5Input input;
    mirror::State state;
    mirror::AirPlay receiver(state);
    mirror::MediaPlayer media(state);
    mirror::DlnaReceiver dlna(state, media);
    stage("start Apple receiver");
    receiver.start("MirrorPSX - PS5");
    log("Receiver listening: %d, error: %s", int(state.snapshot().listening),
        state.snapshot().error.c_str());
    mirror::VideoTexture texture(renderer.physical, renderer.device,
                                 renderer.queue, renderer.family);
    sceSystemServiceHideSplashScreen();
    double previous = seconds();
    bool running = true;
    bool first_frame = true;
    stage("render first frame");
    while (running) {
      const double now = seconds();
      auto &io = ImGui::GetIO();
      io.DisplaySize = {float(renderer.window.Width),
                        float(renderer.window.Height)};
      io.DeltaTime = float(std::clamp(now - previous, .001, .1));
      previous = now;
      input.update(now);
      texture.update(state);
      ImGui_ImplVulkan_NewFrame();
      ImGui::NewFrame();
      auto action =
          screen.draw(state.snapshot(), (ImTextureID)texture.descriptor());
      if (action == mirror::Action::quit)
        running = false;
      else if (action == mirror::Action::toggle_mode) {
        const bool media_mode = !state.snapshot().media_mode;
        receiver.stop();
        dlna.stop();
        media.stop();
        state.update([&](auto &s) {
          s.media_mode = media_mode;
          s.error.clear();
        });
        if (media_mode)
          dlna.start();
        else
          receiver.start("MirrorPSX - PS5");
      } else if (action == mirror::Action::toggle_media) {
        if (media.status().transport == "PLAYING")
          media.pause();
        else
          media.play();
      } else if (action == mirror::Action::toggle_receiver) {
        const auto s = state.snapshot();
        if (s.media_mode) {
          if (s.listening) {
            dlna.stop();
            media.stop();
          } else
            dlna.start();
        } else {
          if (s.listening)
            receiver.stop();
          else
            receiver.start("MirrorPSX - PS5");
        }
      }
      ImGui::Render();
      renderer.render();
      if (first_frame) {
        first_frame = false;
        stage("render loop active");
      }
    }
    receiver.stop();
    dlna.stop();
    media.stop();
    check(vkDeviceWaitIdle(renderer.device));
  } catch (const std::exception &e) {
    log("Startup/runtime error at %s: %s", startup_stage, e.what());
    shutdown_to_shell(1);
  }
  shutdown_to_shell(0);
}





