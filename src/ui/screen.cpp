// SPDX-License-Identifier: GPL-3.0-or-later
// A video-first surface with a transient perimeter HUD and in-place overlays.
#include "ui/screen.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mirror {
namespace {
constexpr ImU32 white = IM_COL32(242, 244, 248, 255);
constexpr ImU32 muted = IM_COL32(169, 178, 191, 255);
constexpr ImU32 accent = IM_COL32(170, 216, 237, 255);
constexpr ImU32 edge = IM_COL32(255, 255, 255, 24);
bool pressed(ImGuiKey key, ImGuiKey pad) {
  return ImGui::IsKeyPressed(key, false) || ImGui::IsKeyPressed(pad, false);
}
} // namespace
bool Screen::initialize() {
  auto &io = ImGui::GetIO();
  io.ConfigNavCursorVisibleAlways = true;
  ImGui::GetStyle().Colors[ImGuiCol_NavHighlight] = ImVec4(1, 1, 1, 0);
  return assets_.initialize();
}
Action Screen::draw(const Snapshot &s, ImTextureID video) {
  Action action = Action::none;
  const auto viewport = ImGui::GetIO().DisplaySize;
  if (viewport.x <= 0 || viewport.y <= 0)
    return action;
  const float scale = std::min(viewport.x / 1920.f, viewport.y / 1080.f);
  const ImVec2 origin((viewport.x - 1920 * scale) / 2,
                      (viewport.y - 1080 * scale) / 2);
  auto p = [&](float x, float y) {
    return ImVec2(origin.x + x * scale, origin.y + y * scale);
  };
  auto *d = ImGui::GetBackgroundDrawList();
  float opacity = 1;
  auto ink = [&](ImU32 color) {
    return (color & 0x00ffffff) | ImU32(((color >> 24) & 255) * opacity) << 24;
  };
  auto font_for = [&](float height, bool bold) {
    auto *f = bold ? assets_.semibold : assets_.regular;
    if (height * scale > 32 && assets_.heading)
      f = assets_.heading;
    return f ? f : ImGui::GetFont();
  };
  auto text = [&](float x, float y, float height, ImU32 color,
                  const char *label, bool bold = false, float wrap = 0.f) {
    d->AddText(font_for(height, bold), height * scale, p(x, y), ink(color),
               label, nullptr, wrap * scale);
  };
  auto centered = [&](float y, float height, ImU32 color,
                      const std::string &label, bool bold = false) {
    const float w = font_for(height, bold)
                        ->CalcTextSizeA(height, FLT_MAX, 0, label.c_str())
                        .x;
    text(960 - w / 2, y, height, color, label.c_str(), bold);
  };
  auto fitted = [&](float x, float y, float height, float width, ImU32 color,
                    std::string label, bool bold = true) {
    auto *f = font_for(height, bold);
    const bool shortened =
        f->CalcTextSizeA(height, FLT_MAX, 0, label.c_str()).x > width;
    while (!label.empty() &&
           f->CalcTextSizeA(height, FLT_MAX, 0,
                            (label + (shortened ? "…" : "")).c_str())
                   .x > width) {
      do {
        label.pop_back();
      } while (!label.empty() &&
               (static_cast<unsigned char>(label.back()) & 0xc0) == 0x80);
      if (!label.empty() &&
          (static_cast<unsigned char>(label.back()) & 0xc0) == 0xc0)
        label.pop_back();
    }
    if (shortened)
      label += "…";
    text(x, y, height, color, label.c_str(), bold);
  };
  auto glyph = [&](Glyph icon, float x, float y, float height,
                   ImU32 color = white) {
    assets_.image(d, p(x, y), p(x + height, y + height), false, icon,
                  ink(color));
  };
  auto panel = [&](float x, float y, float width, float height) {
    d->AddRectFilled(p(x - 8, y + 10), p(x + width + 8, y + height + 18),
                     ink(IM_COL32(0, 0, 0, 60)), 28 * scale);
    d->AddRectFilled(p(x, y), p(x + width, y + height),
                     ink(IM_COL32(26, 30, 39, 250)), 22 * scale);
    d->AddRect(p(x, y), p(x + width, y + height), ink(edge), 22 * scale);
  };
  auto line = [&](float x, float y, float width) {
    d->AddLine(p(x, y), p(x + width, y), ink(edge), scale);
  };
  const bool has_video = s.connected && video && s.width > 0 && s.height > 0;
  const double now = ImGui::GetTime();
  bool input = false;
  const auto delta = ImGui::GetIO().MouseDelta;
  input = delta.x != 0 || delta.y != 0 || ImGui::IsMouseClicked(0) ||
          ImGui::IsMouseClicked(1);
  for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
    input = input || ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), true);
  const bool waking =
      has_video && overlay_ == Overlay::none && hud_opacity_ < .15f;
  if (last_interaction_ < 0 || input || has_video != was_connected_) {
    if ((waking && input) || has_video != was_connected_)
      focus_pending_ = true;
    last_interaction_ = now;
    was_connected_ = has_video;
  }
  auto open = [&](Overlay overlay) {
    overlay_ = overlay;
    focus_pending_ = true;
    last_interaction_ = now;
  };
  if (pressed(ImGuiKey_Escape, ImGuiKey_GamepadFaceRight))
    open(overlay_ == Overlay::quit ? Overlay::settings : Overlay::none);
  if (overlay_ != Overlay::quit) {
    if (pressed(ImGuiKey_I, ImGuiKey_GamepadStart))
      open(overlay_ == Overlay::settings ? Overlay::none : Overlay::settings);
    if (pressed(ImGuiKey_F1, ImGuiKey_GamepadFaceUp))
      open(overlay_ == Overlay::guide ? Overlay::none : Overlay::guide);
    if (has_video && overlay_ == Overlay::none &&
        pressed(ImGuiKey_F, ImGuiKey_GamepadFaceLeft))
      last_interaction_ = now - 10;
  }
  const float target =
      (!has_video || overlay_ != Overlay::none || now - last_interaction_ < 4)
          ? 1.f
          : 0.f;
  hud_opacity_ =
      reduced_motion_
          ? target
          : hud_opacity_ + (target - hud_opacity_) *
                               (1 - std::exp(-10.f * ImGui::GetIO().DeltaTime));

  d->AddRectFilled(ImVec2(0, 0), viewport, IM_COL32(12, 15, 21, 255));
  if (has_video) {
    const float fit = std::min(viewport.x / s.width, viewport.y / s.height);
    const ImVec2 extent(s.width * fit, s.height * fit);
    d->AddImage(
        video, ImVec2((viewport.x - extent.x) / 2, (viewport.y - extent.y) / 2),
        ImVec2((viewport.x + extent.x) / 2, (viewport.y + extent.y) / 2));
  } else {
    assets_.image(d, p(0, 0), p(1920, 1080), true, Glyph::cross,
                  IM_COL32(190, 205, 226, 100));
    d->AddRectFilled(ImVec2(0, 0), viewport, IM_COL32(10, 14, 21, 170));
    if (!s.pin.empty()) {
      glyph(Glyph::screen, 914, 250, 92, accent);
      centered(382, 46, white, "Inserisci questo PIN sul dispositivo", true);
      centered(461, 26, muted, s.name);
      centered(536, 104, white, s.pin, true);
      centered(702, 28, muted, "Il collegamento continuerà dopo la conferma.");
    } else {
      glyph(Glyph::screen, 914, 268, 92, accent);
      centered(406, 58, white,
               !s.error.empty()                 ? "Ricevitore non disponibile"
               : s.media_loading                ? "Caricamento del video"
               : s.media_mode && s.media_paused ? "Video in pausa"
               : s.connected                    ? "Preparazione del video"
               : s.listening                    ? "Pronto a ricevere"
                                                : "Ricezione in pausa",
               true);
      centered(515, 23, muted, "NOME DEL RICEVITORE");
      auto *name_font = font_for(46, true);
      const float measured =
          name_font->CalcTextSizeA(46, FLT_MAX, 0, s.name.c_str()).x;
      if (measured <= 1180)
        centered(558, 46, white, s.name, true);
      else
        fitted(370, 558, 46, 1180, white, s.name);
      if (!s.error.empty())
        text(460, 678, 28, IM_COL32(235, 190, 157, 255), s.error.c_str(), false,
             1000);
      else {
        centered(678, 28, muted,
                 s.media_mode ? "Invia un video da un’app compatibile DLNA."
                 : s.listening
                     ? "Apri Duplica schermo nel Centro di Controllo del "
                       "dispositivo."
                     : "Avvia il ricevitore per renderlo visibile sulla rete.");
        centered(
            726, 25, muted,
            "Dispositivo e ricevitore devono essere sulla stessa rete locale.");
      }
    }
  }

  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(viewport);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("MirrorPSX", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  auto button = [&](const char *id, const char *label, Glyph icon, float x,
                    float y, float width, float height,
                    bool default_focus = false, bool overlay_control = false,
                    bool selected = false) {
    const bool enabled = overlay_control
                             ? overlay_ != Overlay::none && opacity > .08f
                             : overlay_ == Overlay::none &&
                                   overlay_opacity_ < .05f && opacity > .08f;
    ImGui::SetCursorScreenPos(p(x, y));
    if (enabled && focus_pending_ && default_focus) {
      ImGui::SetKeyboardFocusHere();
      focus_pending_ = false;
    }
    ImGui::BeginDisabled(!enabled);
    const bool hit = ImGui::InvisibleButton(
        id, ImVec2(width * scale, height * scale), ImGuiButtonFlags_EnableNav);
    const bool focus = ImGui::IsItemFocused(), hover = ImGui::IsItemHovered();
    d->AddRectFilled(p(x, y), p(x + width, y + height),
                     ink(selected ? IM_COL32(239, 242, 247, 255)
                         : hover  ? IM_COL32(58, 64, 76, 245)
                                  : IM_COL32(34, 39, 49, 235)),
                     12 * scale);
    if (focus && enabled)
      d->AddRect(p(x - 4, y - 4), p(x + width + 4, y + height + 4), ink(white),
                 16 * scale, 0, 2 * scale);
    const ImU32 color = selected ? IM_COL32(18, 22, 28, 255) : white;
    glyph(icon, x + 18, y + (height - 34) / 2, 34, color);
    if (label && *label)
      text(x + 66, y + (height - 25) / 2 - 1, 25, color, label, selected);
    ImGui::EndDisabled();
    return hit && (!waking || overlay_control);
  };
  opacity = hud_opacity_;
  if (has_video) {
    d->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(viewport.x, 210 * scale),
                               ink(IM_COL32(3, 6, 11, 205)),
                               ink(IM_COL32(3, 6, 11, 205)), 0, 0);
    d->AddRectFilledMultiColor(ImVec2(0, viewport.y - 240 * scale), viewport, 0,
                               0, ink(IM_COL32(3, 6, 11, 220)),
                               ink(IM_COL32(3, 6, 11, 220)));
  }
  glyph(Glyph::screen, 96, 70, 40);
  text(150, 75, 29, white, "MirrorPSX", true);
  text(96, 130, 22, s.listening ? accent : muted,
       !s.error.empty() ? "Verifica la rete"
       : s.connected    ? "Dispositivo connesso"
       : s.listening    ? "Ricevitore disponibile"
                        : "Ricevitore in pausa");
  if (button("settings", "Impostazioni", Glyph::settings, 1520, 70, 304, 68))
    open(Overlay::settings);
  text(1586, 152, 20, muted, "Options / I");
  if (!has_video &&
      button("mode", s.media_mode ? "Schermo Apple" : "Video Android",
             Glyph::screen, 744, 70, 432, 68))
    action = Action::toggle_mode;
  if (has_video) {
    const std::string info =
        (s.client.empty() ? "Dispositivo connesso" : s.client) + "  •  " +
        std::to_string(s.width) + " × " + std::to_string(s.height);
    fitted(650, 82, 24, 730, white, info, false);
    if (button("clean", "Solo video", Glyph::square, 826, 938, 268, 68, true))
      last_interaction_ = now - 10;
    text(897, 1020, 20, muted, "Quadrato / F");
  }
  if (button("receiver",
             s.media_mode && (has_video || s.media_paused || s.media_loading)
                 ? (s.media_paused ? "Riprendi video" : "Pausa video")
             : s.listening      ? "Metti in pausa"
             : !s.error.empty() ? "Riprova"
                                : "Avvia ricevitore",
             Glyph::cross, 96, 938, 330, 68, !has_video))
    action = s.media_mode && (has_video || s.media_paused || s.media_loading)
                 ? Action::toggle_media
                 : Action::toggle_receiver;
  text(162, 1020, 20, muted, "Croce / Invio");
  if (button("guide", "Guida", Glyph::triangle, 1624, 938, 200, 68))
    open(Overlay::guide);
  text(1690, 1020, 20, muted, "Triangolo / F1");
  if (!assets_.loaded())
    text(96, 886, 23, IM_COL32(235, 190, 157, 255),
         "Asset mancanti: avvia dalla cartella dell’app.");

  if (overlay_ != Overlay::none && displayed_overlay_ != overlay_) {
    displayed_overlay_ = overlay_;
    overlay_opacity_ = 0;
  }
  const float overlay_target = overlay_ != Overlay::none ? 1.f : 0.f;
  overlay_opacity_ =
      reduced_motion_
          ? overlay_target
          : overlay_opacity_ +
                (overlay_target - overlay_opacity_) *
                    (1 - std::exp(-14.f * ImGui::GetIO().DeltaTime));
  if (overlay_opacity_ > .005f) {
    opacity = overlay_opacity_;
    d->AddRectFilled(ImVec2(0, 0), viewport, ink(IM_COL32(3, 6, 11, 155)));
    if (displayed_overlay_ == Overlay::settings) {
      panel(1100, 120, 724, 836);
      glyph(Glyph::settings, 1140, 165, 38);
      text(1198, 165, 37, white, "Impostazioni", true);
      if (button("close_settings", "", Glyph::close, 1720, 153, 64, 64, false,
                 true))
        open(Overlay::none);
      text(1140, 272, 29, white, "Modalità", true);
      if (button("settings_mode",
                 s.media_mode ? "Passa a schermo Apple"
                              : "Passa a video Android",
                 Glyph::screen, 1140, 317, 644, 62, false, true))
        action = Action::toggle_mode;
      text(1140, 410, 23, muted,
           s.media_mode  ? "Video Android · ricevitore DLNA sulla rete locale."
           : s.listening ? "Visibile ai dispositivi sulla rete locale."
                         : "Il ricevitore non è visibile sulla rete.");
      if (button("settings_receiver",
                 s.listening ? "Arresta ricevitore" : "Avvia ricevitore",
                 s.listening ? Glyph::pause : Glyph::play, 1140, 454, 644, 68,
                 true, true))
        action = Action::toggle_receiver;
      line(1140, 550, 644);
      text(1140, 582, 29, white, "Riduci movimento", true);
      text(1140, 627, 23, muted, "Disattiva le dissolvenze dell’interfaccia.");
      if (button("motion", reduced_motion_ ? "Attivo" : "Disattivato",
                 Glyph::settings, 1140, 670, 644, 62, false, true,
                 reduced_motion_))
        reduced_motion_ = !reduced_motion_;
      line(1140, 768, 644);
      if (button("quit", "Chiudi MirrorPSX", Glyph::close, 1140, 792, 644, 62,
                 false, true))
        open(Overlay::quit);
      glyph(Glyph::cross, 1140, 871, 34);
      text(1186, 877, 22, muted, "Seleziona");
      glyph(Glyph::circle, 1500, 871, 34);
      text(1546, 877, 22, muted, "Chiudi / Esc");
    } else if (displayed_overlay_ == Overlay::guide) {
      panel(348, 160, 1224, 760);
      glyph(Glyph::help, 396, 202, 42);
      text(460, 202, 40, white, "Come collegarsi", true);
      if (button("close_guide", "", Glyph::close, 1464, 190, 64, 64, false,
                 true))
        open(Overlay::none);
      if (button("iphone", s.media_mode ? "Android · DLNA" : "iPhone / iPad",
                 Glyph::screen, 396, 280, 312, 62, true, true,
                 guide_device_ == 0))
        guide_device_ = 0;
      if (!s.media_mode && button("mac", "Mac", Glyph::screen, 728, 280, 196,
                                  62, false, true, guide_device_ == 1))
        guide_device_ = 1;
      const char *titles[] = {"Usa la stessa rete", "Apri Duplica schermo",
                              "Scegli il ricevitore"};
      const char *ios[] = {
          "Collega il dispositivo alla stessa rete locale del ricevitore.",
          "Apri il Centro di Controllo e tocca Duplica schermo.",
          "Seleziona il nome mostrato sulla TV. Se richiesto, inserisci il "
          "PIN."};
      const char *mac[] = {
          "Collega il Mac alla stessa rete locale del ricevitore.",
          "Apri il Centro di Controllo dalla barra dei menu, poi Duplica "
          "schermo.",
          "Seleziona il nome mostrato sulla TV. Se richiesto, inserisci il "
          "PIN."};
      for (int i = 0; i < 3; ++i) {
        const char *android[] = {
            "Collega Android alla stessa rete locale del ricevitore.",
            "Apri un’app compatibile DLNA e scegli un video non protetto.",
            "Scegli MirrorPSX - Video. Il video si riproduce sul ricevitore."};
        const char *android_titles[] = {"Usa la stessa rete", "Scegli il video",
                                        "Invia al ricevitore"};
        const float y = 396 + i * 140.f;
        char number[4];
        std::snprintf(number, sizeof(number), "%d", i + 1);
        text(396, y, 32, accent, number, true);
        text(460, y, 31, white, s.media_mode ? android_titles[i] : titles[i],
             true);
        text(460, y + 47, 25, muted,
             s.media_mode         ? android[i]
             : guide_device_ == 0 ? ios[i]
                                  : mac[i],
             false, 1040);
        if (i < 2)
          line(460, y + 110, 1040);
      }
      text(396, 793, 22, muted,
           s.media_mode
               ? "I pulsanti Cast di YouTube e Netflix non sono supportati."
               : "Per YouTube usa Duplica schermo, poi apri il video sull’iPad.",
           false, 1120);
      glyph(Glyph::circle, 396, 848, 34);
      text(442, 854, 23, muted, "Torna allo schermo / Esc");
    } else {
      panel(600, 360, 720, 360);
      text(644, 404, 37, white, "Chiudere MirrorPSX?", true);
      text(644, 466, 26, muted,
           "La ricezione e la condivisione verranno interrotte.", false, 630);
      if (button("cancel", "Annulla", Glyph::circle, 644, 578, 294, 68, true,
                 true))
        open(Overlay::settings);
      if (button("confirm_quit", "Chiudi", Glyph::cross, 962, 578, 314, 68,
                 false, true))
        action = Action::quit;
    }
  }
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  return action;
}
} // namespace mirror
