// SPDX-License-Identifier: GPL-3.0-or-later
// Pad sample ABI follows PS5 Vulkan Template, Copyright (C) 2026 Mihawk (MIT).
#include "platform/ps5_input.hpp"
#include "imgui.h"
#include <cstddef>
#include <cstdint>
#include <ps5/userservice.h>
extern "C" {
int scePadInit(void);
int scePadOpen(int, int, int, const void *);
int scePadClose(int);
int scePadRead(int, void *, int);
}
namespace mirror {
struct PadSample {
  uint32_t buttons;
  uint8_t lx, ly, rx, ry, l2, r2;
  uint8_t reserved[66];
  int32_t connected;
  uint64_t timestamp;
  uint8_t extension[16], count, remaining[15];
};
static_assert(sizeof(PadSample) == 120);
static_assert(offsetof(PadSample, connected) == 0x4c);
Ps5Input::Ps5Input() {
  sceUserServiceInitialize(nullptr);
  scePadInit();
}
Ps5Input::~Ps5Input() {
  if (handle_ >= 0)
    scePadClose(handle_);
}
void Ps5Input::update(double now) {
  auto &io = ImGui::GetIO();
  if (handle_ < 0 && now >= retry_at_) {
    retry_at_ = now + 1;
    int user = -1;
    if (sceUserServiceGetInitialUser(&user) >= 0)
      handle_ = scePadOpen(user, 0, 0, nullptr);
  }
  PadSample samples[64]{};
  const int count = handle_ >= 0 ? scePadRead(handle_, samples, 64) : 0;
  const bool valid = count > 0 && count <= 64 && samples[count - 1].connected &&
                     !(samples[count - 1].buttons & 0x80000000u);
  const auto b = valid ? samples[count - 1].buttons : 0u;
  if (valid)
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  else
    io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
  const struct {
    ImGuiKey key;
    uint32_t mask;
  } keys[] = {
      {ImGuiKey_GamepadFaceDown, 0x4000}, {ImGuiKey_GamepadFaceRight, 0x2000},
      {ImGuiKey_GamepadFaceLeft, 0x8000}, {ImGuiKey_GamepadFaceUp, 0x1000},
      {ImGuiKey_GamepadDpadUp, 0x10},     {ImGuiKey_GamepadDpadRight, 0x20},
      {ImGuiKey_GamepadDpadDown, 0x40},   {ImGuiKey_GamepadDpadLeft, 0x80},
      {ImGuiKey_GamepadStart, 0x08},      {ImGuiKey_GamepadL1, 0x400},
      {ImGuiKey_GamepadR1, 0x800}};
  for (const auto &k : keys)
    io.AddKeyEvent(k.key, (b & k.mask) != 0);
  const float x = valid ? (int(samples[count - 1].lx) - 128) / 127.f : 0;
  const float y = valid ? (int(samples[count - 1].ly) - 128) / 127.f : 0;
  auto axis = [&](ImGuiKey key, float v) {
    v = (v - .25f) / .75f;
    io.AddKeyAnalogEvent(key, v > 0, v > 1 ? 1 : v > 0 ? v : 0);
  };
  axis(ImGuiKey_GamepadLStickLeft, -x);
  axis(ImGuiKey_GamepadLStickRight, x);
  axis(ImGuiKey_GamepadLStickUp, -y);
  axis(ImGuiKey_GamepadLStickDown, y);
}
} // namespace mirror
