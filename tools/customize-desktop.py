from pathlib import Path
src = Path('third_party/imgui/examples/example_glfw_vulkan/main.cpp').read_text()
src = '// Adapted from Dear ImGui GLFW/Vulkan example (MIT).\n' + src
src = src.replace('#include "imgui.h"', '#include "imgui.h"\n#include "ui/screen.hpp"\n#include "receiver/airplay.hpp"\n#include "platform/video_texture.hpp"\n#include <memory>')
src = src.replace('glfwCreateWindow(1280, 720, "Dear ImGui GLFW+Vulkan example"', 'glfwCreateWindow(1440, 810, "MirrorPSX | Vulkan"')
src = src.replace('bool show_demo_window = true;', 'mirror::State state;\n    mirror::AirPlay receiver(state);\n    mirror::Screen screen;\n    auto texture=std::make_unique<mirror::VideoTexture>(g_PhysicalDevice,g_Device,g_Queue,g_QueueFamily);\n    receiver.start("MirrorPSX - PC");\n    io.IniFilename=nullptr;\n    io.Fonts->AddFontFromFileTTF("assets/Roboto-Medium.ttf",24.0f);\n    ImGui::GetStyle().FontScaleDpi=1.0f;\n    bool show_demo_window = false;')
# Use a small descriptor reserve for live video, beyond the font atlas.
src = src.replace('IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE', '32')
start=src.index('        // 1. Show the big demo window')
end=src.index('        // Rendering',start)
src=src[:start]+'''        texture->update(state);
        auto action=screen.draw(state.snapshot(),(ImTextureID)texture->descriptor());
        if(action==mirror::Action::quit)glfwSetWindowShouldClose(window,GLFW_TRUE);
        if(action==mirror::Action::toggle_receiver){
            if(state.snapshot().listening)receiver.stop();else receiver.start("MirrorPSX - PC");
        }

'''+src[end:]
src=src.replace('    ImGui_ImplVulkan_Shutdown();', '    receiver.stop();\n    texture.reset();\n    ImGui_ImplVulkan_Shutdown();')
src=src.replace('    ImGui::GetStyle().FontScaleDpi=1.0f;\n','')
Path('src/platform/desktop.cpp').write_text(src)
