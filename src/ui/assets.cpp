// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/assets.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace mirror {
bool UiAssets::initialize() {
  auto *atlas = ImGui::GetIO().Fonts;
#ifdef MIRROR_PS5
  const std::string root = "/app0/assets/ui/";
#else
  const std::string root = "assets/ui/";
#endif
  auto font = [&](const char *path, float size) -> ImFont * {
    if (auto *file = std::fopen(path, "rb")) {
      std::fclose(file);
      static const ImWchar ranges[] = {0x20, 0xff, 0x2000, 0x206f, 0};
      return atlas->AddFontFromFileTTF(path, size, nullptr, ranges);
    }
    return atlas->AddFontDefault();
  };
  regular = font((root + "fonts/Inter-Regular.ttf").c_str(), 32.f);
  semibold = font((root + "fonts/Inter-SemiBold.ttf").c_str(), 32.f);
  heading = font((root + "fonts/Inter-SemiBold.ttf").c_str(), 80.f);
  ImGui::GetIO().FontDefault = regular;
  auto *file = std::fopen((root + "atlas.rgba").c_str(), "rb");
  if (!file)
    return false;
  std::vector<unsigned char> pixels(1024 * 824 * 4);
  const auto count = std::fread(pixels.data(), 1, pixels.size(), file);
  const bool exact = count == pixels.size() && std::fgetc(file) == EOF;
  std::fclose(file);
  if (!exact)
    return false;
  atlas->TexDesiredWidth = 2048;
  rectangle_ = atlas->AddCustomRectRegular(1024, 824);
  unsigned char *target;
  int width, height;
  atlas->GetTexDataAsRGBA32(&target, &width, &height);
  const auto *rect = atlas->GetCustomRectByIndex(rectangle_);
  for (int y = 0; y < 824; ++y)
    std::memcpy(target + ((rect->Y + y) * width + rect->X) * 4,
                pixels.data() + y * 1024 * 4, 1024 * 4);
  loaded_ = true;
  return true;
}
void UiAssets::image(ImDrawList *draw, ImVec2 min, ImVec2 max, bool backdrop,
                     Glyph glyph, ImU32 tint, float rounding) const {
  if (!loaded_)
    return;
  auto *atlas = ImGui::GetIO().Fonts;
  const auto *rect = atlas->GetCustomRectByIndex(rectangle_);
  const float x = backdrop ? 0.f : (static_cast<int>(glyph) % 8) * 128.f;
  const float y =
      backdrop ? 0.f : 552.f + (static_cast<int>(glyph) / 8) * 128.f;
  const float w = backdrop ? 960.f : 128.f, h = backdrop ? 540.f : 128.f;
  const ImVec2 uv0((rect->X + x + .5f) / atlas->TexWidth,
                   (rect->Y + y + .5f) / atlas->TexHeight);
  const ImVec2 uv1((rect->X + x + w - .5f) / atlas->TexWidth,
                   (rect->Y + y + h - .5f) / atlas->TexHeight);
  draw->AddImageRounded(atlas->TexID, min, max, uv0, uv1, tint, rounding);
}
} // namespace mirror
