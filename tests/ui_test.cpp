// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screen.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
static void require(bool ok, const char *message) {
  if (!ok) {
    std::fprintf(stderr, "UI: %s\n", message);
    std::exit(1);
  }
}
int main() {
  ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  mirror::Screen screen;
  require(screen.initialize(), "fonts and artwork load");
  io.Fonts->SetTexID(1);
  mirror::Snapshot status;
  status.listening = true;
  io.DisplaySize = ImVec2(1920, 1080);
  auto frame = [&](ImTextureID video = 0) {
    io.DeltaTime = 1.f / 60;
    ImGui::NewFrame();
    auto action = screen.draw(status, video);
    ImGui::Render();
    auto *data = ImGui::GetDrawData();
    require(data->TotalVtxCount > 0, "surface renders");
    for (int n = 0; n < data->CmdListsCount; ++n)
      for (const auto &v : data->CmdLists[n]->VtxBuffer)
        require(std::isfinite(v.pos.x) && std::isfinite(v.pos.y),
                "finite layout coordinates");
    return action;
  };
  auto click = [&](float x, float y, ImTextureID video = 0) {
    io.AddMousePosEvent(x, y);
    frame(video);
    io.AddMouseButtonEvent(0, true);
    frame(video);
    io.AddMouseButtonEvent(0, false);
    return frame(video);
  };
  auto key = [&](ImGuiKey key, ImTextureID video = 0) {
    io.AddKeyEvent(key, true);
    auto a = frame(video);
    io.AddKeyEvent(key, false);
    auto b = frame(video);
    return a != mirror::Action::none ? a : b;
  };
  for (int i = 0; i < 10; ++i)
    require(frame() == mirror::Action::none,
            "focus must never activate receiver");
  require(click(240, 970) == mirror::Action::toggle_receiver,
          "HUD receiver action");
  require(key(ImGuiKey_GamepadFaceUp) == mirror::Action::none,
          "triangle opens guide without receiver side effects");
  click(820, 310);
  key(ImGuiKey_GamepadFaceRight);
  for (int i = 0; i < 24; ++i)
    frame();
  require(click(240, 970) == mirror::Action::toggle_receiver,
          "circle closes guide");
  key(ImGuiKey_GamepadStart);
  frame();
  require(click(1400, 490) == mirror::Action::toggle_receiver,
          "settings receiver control");
  require(click(1400, 702) == mirror::Action::none,
          "reduced motion has no receiver side effect");
  require(click(1400, 822) == mirror::Action::none,
          "closing app requires explicit confirmation");
  key(ImGuiKey_Escape);
  frame();
  require(click(1400, 490) == mirror::Action::toggle_receiver,
          "cancel quit restores settings");
  click(1400, 822);
  frame();
  require(click(1130, 612) == mirror::Action::quit, "confirmed app closure");
  key(ImGuiKey_Escape);
  key(ImGuiKey_Escape);
  frame();
  status.connected = true;
  status.width = 1080;
  status.height = 1920;
  frame(2);
  for (int i = 0; i < 360; ++i)
    require(frame(2) == mirror::Action::none, "idle HUD has no actions");
  require(key(ImGuiKey_GamepadFaceDown, 2) == mirror::Action::none,
          "first confirm wakes HUD without pausing");
  require(click(240, 970, 2) == mirror::Action::toggle_receiver,
          "visible HUD remains interactive");
  key(ImGuiKey_F, 2);
  for (int i = 0; i < 60; ++i)
    frame(2);
  key(ImGuiKey_I, 2);
  frame(2);
  require(click(1400, 490, 2) == mirror::Action::toggle_receiver,
          "settings open over video");
  key(ImGuiKey_Escape, 2);
  status.connected = false;
  frame();
  frame();
  require(click(240, 970) == mirror::Action::toggle_receiver,
          "disconnect restores waiting controls");
  status.pin = "4826";
  frame();
  require(click(900, 102) == mirror::Action::toggle_mode,
          "waiting mode selector");
  status.media_mode = true;
  status.name = "MirrorPSX - Video";
  status.pin.clear();
  status.connected = true;
  status.width = 1920;
  status.height = 1080;
  frame(2);
  require(click(240, 970, 2) == mirror::Action::toggle_media,
          "DLNA content pause uses media action");
  status.connected = false;
  status.media_mode = false;
  status.pin.clear();
  status.error = "Verifica la rete locale e riprova.";
  status.name = std::string(300, 'W');
  frame();
  status.name = "Ricevitore èàù con UTF-8 molto lungo " + std::string(200, 'W');
  for (const auto size :
       {ImVec2(1280, 720), ImVec2(2560, 1080), ImVec2(3840, 2160)}) {
    io.DisplaySize = size;
    frame();
    key(ImGuiKey_I);
    frame();
    key(ImGuiKey_Escape);
    key(ImGuiKey_F1);
    frame();
    key(ImGuiKey_Escape);
  }
  ImGui::DestroyContext();
  std::puts("UI assets, HUD wake, overlays, actions and layouts: passed");
}
