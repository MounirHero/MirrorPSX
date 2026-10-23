// SPDX-License-Identifier: GPL-3.0-or-later
// Export actual ImGui geometry for deterministic layout review without desktop
// focus.
#include "ui/screen.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main(int argc, char **argv) {
  if (argc < 3)
    return 1;
  ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = ImVec2(argc > 4 ? std::atoi(argv[4]) : 1920,
                          argc > 5 ? std::atoi(argv[5]) : 1080);
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  mirror::Screen screen;
  if (!screen.initialize())
    return 2;
  io.Fonts->SetTexID(1);
  mirror::Snapshot s;
  s.name = "MirrorPSX - PC";
  s.listening = std::strcmp(argv[2], "paused") && std::strcmp(argv[2], "error");
  if (!std::strcmp(argv[2], "pin"))
    s.pin = "4826";
  if (!std::strcmp(argv[2], "error"))
    s.error = "Impossibile avviare il ricevitore. Verifica la rete e riprova.";
  if (!std::strcmp(argv[2], "video") || !std::strcmp(argv[2], "portrait")) {
    s.connected = true;
    s.client = "iPad";
    s.width = !std::strcmp(argv[2], "portrait") ? 1080 : 1920;
    s.height = !std::strcmp(argv[2], "portrait") ? 1920 : 1080;
  }
  for (int i = 0; i < 24; ++i) {
    io.DeltaTime = 1.f / 60;
    if (i == 5 && argc > 3) {
      if (!std::strcmp(argv[3], "settings"))
        io.AddKeyEvent(ImGuiKey_I, true);
      if (!std::strcmp(argv[3], "guide"))
        io.AddKeyEvent(ImGuiKey_F1, true);
    }
    if (i == 6) {
      io.AddKeyEvent(ImGuiKey_I, false);
      io.AddKeyEvent(ImGuiKey_F1, false);
    }
    ImGui::NewFrame();
    screen.draw(s, s.connected ? 2 : 0);
    ImGui::Render();
  }
  auto *file = std::fopen(argv[1], "wb");
  if (!file)
    return 3;
  unsigned char *pixels;
  int w, h;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
  auto *data = ImGui::GetDrawData();
  uint32_t count = 0;
  for (int n = 0; n < data->CmdListsCount; ++n)
    for (const auto &cmd : data->CmdLists[n]->CmdBuffer)
      if (!cmd.UserCallback)
        ++count;
  uint32_t header[] = {uint32_t(io.DisplaySize.x), uint32_t(io.DisplaySize.y),
                       uint32_t(w), uint32_t(h), count};
  std::fwrite(header, sizeof(header), 1, file);
  std::fwrite(pixels, 4, size_t(w) * h, file);
  for (int n = 0; n < data->CmdListsCount; ++n) {
    const auto *list = data->CmdLists[n];
    for (const auto &cmd : list->CmdBuffer) {
      if (cmd.UserCallback)
        continue;
      uint32_t texture = uint32_t(cmd.GetTexID()), vertices = cmd.ElemCount;
      std::fwrite(&texture, 4, 1, file);
      std::fwrite(&cmd.ClipRect, 16, 1, file);
      std::fwrite(&vertices, 4, 1, file);
      for (unsigned int i = 0; i < cmd.ElemCount; ++i) {
        const auto &v =
            list->VtxBuffer[cmd.VtxOffset + list->IdxBuffer[cmd.IdxOffset + i]];
        static_assert(sizeof(v) == 20);
        std::fwrite(&v, 20, 1, file);
      }
    }
  }
  std::fclose(file);
  ImGui::DestroyContext();
  return 0;
}
