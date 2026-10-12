// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screen.hpp"
#include "ui/gamepad_navigation.hpp"
#include <cassert>

int main() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.Fonts->AddFontDefaultVector();
    unsigned char* font_pixels = nullptr;
    int font_width = 0, font_height = 0;
    io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
    assert(font_pixels && font_width > 0 && font_height > 0);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    mirror::apply_screen_style();
    mirror::ScreenUiState ui;
    mirror::Snapshot status;
    const ImVec2 sizes[] = {{1280, 720}, {1920, 1080}, {2560, 1400}, {3840, 2160}};
    for (const auto size : sizes) {
        io.DisplaySize = size;
        io.DeltaTime = 1.f / 60.f;
        ImGui::NewFrame();
        assert(mirror::render_screen(status, ImTextureID_Invalid, 0, 0, ui) == mirror::ScreenAction::none);
        ImGui::Render();
        assert(ImGui::GetDrawData() && ImGui::GetDrawData()->Valid);
        assert(ImGui::GetDrawData()->TotalVtxCount > 0);
    }
    // The actual four-digit callback value, including leading zeroes, must
    // cause a center-screen blocking overlay even before a video frame exists.
    status.listening = true;
    status.connected = true;
    status.pin = "0047";
    for (const auto size : sizes) {
        io.DisplaySize = size;
        io.DeltaTime = 1.f / 60.f;
        ImGui::NewFrame();
        assert(mirror::make_screen_model(status, false).phase == mirror::ScreenPhase::pairing);
        assert(mirror::render_screen(status, ImTextureID_Invalid, 0, 0, ui) == mirror::ScreenAction::none);
        assert(ImGui::IsPopupOpen("Pairing code##mirror"));
        ImGui::Render();
        assert(ImGui::GetDrawData()->Valid);
        assert(ImGui::GetDrawData()->TotalVtxCount > 0);
    }
    status.pin.clear();
    io.DeltaTime = 1.f / 60.f;
    ImGui::NewFrame();
    mirror::render_screen(status, ImTextureID_Invalid, 0, 0, ui);
    assert(!ImGui::IsPopupOpen("Pairing code##mirror"));
    ImGui::Render();
    // The presentation is independent from Vulkan surface creation and remains
    // drawable with a simulated image handle in fullscreen mode.
    status.listening = status.connected = true;
    status.width = 1920;
    status.height = 1080;
    status.frames = 42;
    ui.fullscreen = true;
    io.DeltaTime = 1.f / 60.f;
    ImGui::NewFrame();
    assert(mirror::render_screen(status, static_cast<ImTextureID>(123), 1920, 1080, ui) ==
           mirror::ScreenAction::none);
    ImGui::Render();
    assert(ui.fullscreen && ImGui::GetDrawData()->TotalVtxCount > 0);

    mirror::GamepadNavigation pad;
    pad.connected = true;
    pad.buttons = mirror::pad_confirm | mirror::pad_up | mirror::pad_fullscreen;
    mirror::submit_gamepad_navigation(io, pad);
    assert((io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0);
    io.DeltaTime = 1.f / 60.f;
    ImGui::NewFrame();
    assert(ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown));
    assert(ImGui::IsKeyDown(ImGuiKey_GamepadR1));
    mirror::render_screen(status, static_cast<ImTextureID>(123), 1920, 1080, ui);
    ImGui::Render();
    assert(!ui.fullscreen); // DualSense R1 toggles fullscreen through ImGui input.
    mirror::submit_gamepad_navigation(io, {});
    assert((io.BackendFlags & ImGuiBackendFlags_HasGamepad) == 0);
    ImGui::DestroyContext();
}
