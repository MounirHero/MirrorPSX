// MirrorPSX television UI: controller-first composition inspired by the
// publicly documented design principles of blackbearreloaded/ps5-homebrew-ui.
// Original draw code; actual rendering remains Dear ImGui + Vulkan.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screen.hpp"
#include "ui/icons.hpp"
#include "ui/theme.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace mirror {
namespace {
constexpr ImU32 backdrop = theme::base;
constexpr ImU32 card = theme::card;
constexpr ImU32 bright = theme::white;
constexpr ImU32 secondary = theme::secondary;
constexpr ImU32 tertiary = theme::muted;
constexpr ImU32 blue = theme::blue;
constexpr ImU32 cyan = theme::cyan;
constexpr ImU32 violet = theme::purple;
constexpr ImU32 green = theme::green;
constexpr ImU32 orange = theme::amber;
constexpr ImU32 red = theme::coral;
constexpr ImU32 stroke = theme::border;
ImFont* regular_font = nullptr;
ImFont* semibold_font = nullptr;
ImFont* icons_font = nullptr;

float clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
ImU32 opacity(ImU32 color, float alpha) {
    return (color & 0x00FFFFFFu) | (static_cast<ImU32>(static_cast<float>((color >> 24) & 255u) * clamp01(alpha)) << 24);
}

// All visible coordinates are authored in a 1920x1080 television canvas.
// One uniform transform preserves safe areas and aspect ratio on any host.
struct Canvas {
    ImVec2 origin{};
    float scale = 1.0f;
    ImVec2 at(float x, float y) const {
        return {origin.x + x * scale, origin.y + y * scale};
    }
    ImVec2 dims(float x, float y) const { return {x * scale, y * scale}; }
    void rect(ImDrawList* dl, float x, float y, float w, float h, ImU32 fill, float radius = 0) const {
        dl->AddRectFilled(at(x, y), at(x + w, y + h), fill, radius * scale);
    }
    void border(ImDrawList* dl, float x, float y, float w, float h, ImU32 color, float radius = 0, float thickness = 1) const {
        dl->AddRect(at(x, y), at(x + w, y + h), color, radius * scale, 0, thickness * scale);
    }
    void line(ImDrawList* dl, float x1, float y1, float x2, float y2, ImU32 color, float thickness = 1) const {
        dl->AddLine(at(x1, y1), at(x2, y2), color, thickness * scale);
    }
    void circle(ImDrawList* dl, float x, float y, float r, ImU32 color, int segments = 48) const {
        dl->AddCircleFilled(at(x, y), r * scale, color, segments);
    }
    void ring(ImDrawList* dl, float x, float y, float r, ImU32 color, float thickness = 1) const {
        dl->AddCircle(at(x, y), r * scale, color, 80, thickness * scale);
    }
    ImFont* font(bool bold = false) const {
        return (bold ? semibold_font : regular_font) ? (bold ? semibold_font : regular_font) : ImGui::GetFont();
    }
    ImVec2 measure(const char* value, float size, bool bold = false) const {
        return font(bold)->CalcTextSizeA(size * scale, 100000, 0, value);
    }
    void text(ImDrawList* dl, const char* value, float x, float y, float size, ImU32 color, bool bold = false) const {
        dl->AddText(font(bold), size * scale, at(x, y), color, value);
    }
    void icon(ImDrawList* dl, icons::Id id, float x, float y, float size, ImU32 color) const {
        if (icons_font) dl->AddText(icons_font, size * scale, at(x, y), color, icons::glyph(id));
        else dl->AddText(font(true), size * scale, at(x, y), color, "*");
    }
    void fit(ImDrawList* dl, const char* value, float x, float y, float size, float max_width,
             ImU32 color, bool bold = false) const {
        if (max_width <= 0) return;
        std::string clipped = value;
        const float width = max_width * scale;
        if (measure(clipped.c_str(), size, bold).x > width) {
            const std::string ellipsis = "...";
            while (!clipped.empty() && measure((clipped + ellipsis).c_str(), size, bold).x > width)
                clipped.pop_back();
            clipped += ellipsis;
        }
        text(dl, clipped.c_str(), x, y, size, color, bold);
    }
    void center(ImDrawList* dl, const char* value, float cx, float y, float size, ImU32 color, bool bold = false) const {
        text(dl, value, cx - measure(value, size, bold).x / (2 * scale), y, size, color, bold);
    }
};

ImU32 status_color(const ScreenModel& m) {
    if (m.phase == ScreenPhase::error) return red;
    if (m.phase == ScreenPhase::pairing) return orange;
    if (m.phase == ScreenPhase::streaming) return green;
    if (m.phase == ScreenPhase::stopped) return tertiary;
    return cyan;
}

void backdrop_art(ImDrawList* dl, const Canvas& c, float time, bool reduced_motion) {
    c.rect(dl, 0, 0, 1920, 1080, backdrop);
    const float drift = reduced_motion ? 0.0f : std::sin(time * 0.19f) * 26.0f;
    // The translucent layered aurora is made of Vulkan-drawn geometry,
    // not a prerendered screenshot, expensive blur or WebView.
    c.circle(dl, 1595 + drift, 260, 540, IM_COL32(40, 67, 125, 29), 112);
    c.circle(dl, 1480 - drift, 430, 390, IM_COL32(82, 59, 162, 24), 112);
    c.circle(dl, 600 + drift, 1030, 450, IM_COL32(23, 112, 131, 17), 112);
    dl->AddRectFilledMultiColor(c.at(0, 0), c.at(1920, 1080),
        IM_COL32(14, 32, 56, 55), IM_COL32(27, 24, 61, 40),
        IM_COL32(8, 14, 30, 0), IM_COL32(11, 37, 60, 30));
    for (int i = 0; i < 5; ++i)
        c.ring(dl, 1500 + drift * .3f, 488, 340.0f + i * 65.0f,
            IM_COL32(91, 142, 224, 15), 1.0f);
    // An intentionally understated horizon separates content and system hints.
    c.line(dl, 92, 1007, 1828, 1007, IM_COL32(125, 158, 205, 35));
}

void cast_logo(ImDrawList* dl, const Canvas& c, float x, float y, float sz, ImU32 color) {
    c.border(dl, x, y, sz, sz * .64f, color, sz * .095f, 3.0f);
    c.line(dl, x + sz * .52f, y + sz * .67f, x + sz * .52f, y + sz * .87f, color, 3);
    c.line(dl, x + sz * .26f, y + sz * .88f, x + sz * .78f, y + sz * .88f, color, 3);
    c.ring(dl, x + sz * .06f, y + sz * .61f, sz * .13f, color, 2.5f);
}

void brand(ImDrawList* dl, const Canvas& c) {
    c.rect(dl, 94, 61, 70, 70, IM_COL32(43, 85, 134, 177), 23);
    c.border(dl, 94, 61, 70, 70, IM_COL32(130, 190, 255, 120), 23);
    c.icon(dl, icons::Id::display, 111, 77, 36, cyan);
    c.text(dl, "MirrorPSX", 182, 61, 43, bright, true);
    c.text(dl, "WIRELESS DISPLAY", 185, 111, 17, tertiary, true);
}

void state_chip(ImDrawList* dl, const Canvas& c, const ScreenModel& model, float x, float y) {
    const ImU32 fill = model.phase == ScreenPhase::error ? IM_COL32(72, 34, 53, 215)
        : model.phase == ScreenPhase::pairing ? IM_COL32(81, 61, 42, 215)
        : model.phase == ScreenPhase::streaming ? IM_COL32(31, 69, 59, 215)
        : IM_COL32(27, 42, 68, 227);
    c.rect(dl, x, y, 240, 57, fill, 28);
    c.border(dl, x, y, 240, 57, stroke, 28);
    c.icon(dl, model.phase == ScreenPhase::error ? icons::Id::warning :
        model.phase == ScreenPhase::streaming ? icons::Id::check :
        model.phase == ScreenPhase::pairing ? icons::Id::lock : icons::Id::wifi,
        x + 17, y + 14, 27, status_color(model));
    c.fit(dl, model.status_label, x + 51, y + 17, 19, 171, bright, true);
}

// Draws a genuinely focusable ImGui item while animating its custom Vulkan
// visuals. Hover and gamepad focus share the same feedback.
bool action_button(ImDrawList* dl, const Canvas& c, const char* id, const char* title,
                   float x, float y, float width, float height, bool primary,
                   float& focus_strength, float dt, bool grab_focus = false,
                   icons::Id icon = icons::Id::chevron) {
    ImGui::SetCursorScreenPos(c.at(x, y));
    if (grab_focus) ImGui::SetKeyboardFocusHere();
    const bool clicked = ImGui::InvisibleButton(id, c.dims(width, height));
    const bool active = ImGui::IsItemFocused() || ImGui::IsItemHovered();
    focus_strength += ((active ? 1.f : 0.f) - focus_strength) * clamp01(dt * 13.0f);
    const float glow = clamp01(focus_strength);
    if (glow > .001f) {
        c.rect(dl, x - 8, y - 8, width + 16, height + 16, opacity(blue, glow * .12f), 24);
        c.border(dl, x - 4, y - 4, width + 8, height + 8, opacity(cyan, glow * .93f), 20, 2);
    }
    c.rect(dl, x, y, width, height, primary ? IM_COL32(183, 218, 255, 255)
                                        : IM_COL32(38, 53, 83, 235), 17);
    if (primary) {
        dl->AddRectFilledMultiColor(c.at(x, y + height - 14),
            c.at(x + width, y + height), IM_COL32(133, 178, 245, 40),
            IM_COL32(119, 152, 241, 60), IM_COL32(91, 122, 191, 110),
            IM_COL32(128, 175, 237, 50));
    } else {
        c.border(dl, x, y, width, height, IM_COL32(119, 150, 204, 85), 17);
    }
    c.icon(dl, icon, x + 24, y + 22, 24, primary ? backdrop : cyan);
    c.fit(dl, title, x + 68, y + 22, 23, width - 114, primary ? backdrop : bright, true);
    c.icon(dl, icons::Id::chevron, x + width - 37, y + 25, 19, primary ? backdrop : cyan);
    return clicked;
}

void draw_status_info(ImDrawList* dl, const Canvas& c, const ScreenModel& model,
                      const Snapshot& state) {
    constexpr float x = 96, y = 632, w = 598;
    // Two small, intentionally distinct status cards; the third row carries
    // actionable context without burying it in a dashboard table.
    c.rect(dl, x, y, 285, 128, IM_COL32(42, 42, 81, 231), 22);
    c.border(dl, x, y, 285, 128, IM_COL32(172, 153, 235, 90), 22);
    c.rect(dl, x + 19, y + 22, 44, 44, IM_COL32(94, 72, 142, 115), 13);
    c.icon(dl, icons::Id::display, x + 30, y + 32, 25, violet);
    c.text(dl, "RECEIVER", x + 74, y + 24, 16, tertiary, true);
    c.fit(dl, state.name.c_str(), x + 21, y + 79, 28, 251, bright, true);

    c.rect(dl, x + 303, y, 295, 128, IM_COL32(24, 66, 72, 207), 22);
    c.border(dl, x + 303, y, 295, 128, IM_COL32(87, 214, 198, 80), 22);
    c.rect(dl, x + 323, y + 22, 44, 44, IM_COL32(45, 128, 124, 105), 13);
    c.icon(dl, model.phase == ScreenPhase::error ? icons::Id::warning : icons::Id::wifi,
           x + 333, y + 31, 25, model.phase == ScreenPhase::error ? red : cyan);
    c.text(dl, "CONNECTION", x + 378, y + 25, 16, tertiary, true);
    const char* state_label = model.phase == ScreenPhase::error ? "Needs attention" :
          model.phase == ScreenPhase::streaming ? "Streaming live" :
          model.phase == ScreenPhase::pairing ? "Pairing" :
          model.phase == ScreenPhase::connected ? "Connected" :
          model.phase == ScreenPhase::waiting ? "Discoverable" : "Not connected";
    c.fit(dl, state_label, x + 323, y + 79, 25, 257, bright, true);

    const ImU32 body = model.phase == ScreenPhase::error ? IM_COL32(81, 41, 61, 221) :
        model.phase == ScreenPhase::pairing ? IM_COL32(76, 56, 43, 221) :
        model.phase == ScreenPhase::streaming ? IM_COL32(31, 69, 57, 221) :
        IM_COL32(26, 38, 66, 239);
    c.rect(dl, x, y + 147, w, 104, body, 22);
    c.border(dl, x, y + 147, w, 104, stroke, 22);
    if (model.phase == ScreenPhase::error) {
        c.icon(dl, icons::Id::warning, x + 23, y + 168, 30, red);
        c.text(dl, "CONNECTION ISSUE", x + 68, y + 162, 18, red, true);
        c.fit(dl, state.error.c_str(), x + 68, y + 204, 21, w - 100, bright);
    } else if (model.phase == ScreenPhase::pairing && !state.pin.empty()) {
        c.icon(dl, icons::Id::lock, x + 27, y + 168, 27, orange);
        c.text(dl, "ENTER THIS PIN", x + 74, y + 157, 17, orange, true);
        c.fit(dl, state.pin.c_str(), x + 74, y + 183, 46, w - 94, bright, true);
    } else if (model.phase == ScreenPhase::streaming) {
        char stats[100];
        std::snprintf(stats, sizeof(stats), "%d x %d   /   %llu frames",
            state.width, state.height, static_cast<unsigned long long>(state.frames));
        c.icon(dl, icons::Id::signal, x + 24, y + 176, 29, green);
        c.text(dl, "LIVE VIDEO SIGNAL", x + 75, y + 157, 18, green, true);
        c.fit(dl, stats, x + 75, y + 198, 22, w - 97, bright);
    } else if (model.phase == ScreenPhase::connected) {
        c.icon(dl, icons::Id::check, x + 26, y + 178, 28, green);
        c.text(dl, "CONNECTED", x + 74, y + 155, 17, green, true);
        c.fit(dl, state.client.empty() ? "Waiting for video frames..." : state.client.c_str(),
              x + 74, y + 197, 21, w - 95, secondary);
    } else {
        c.icon(dl, icons::Id::mobile, x + 28, y + 174, 31, cyan);
        c.text(dl, "SCREEN MIRRORING", x + 75, y + 158, 17, cyan, true);
        c.fit(dl, "Same Wi-Fi, then select MirrorPSX on your device.",
              x + 75, y + 200, 20, w - 98, secondary);
    }
}

void draw_placeholder(ImDrawList* dl, const Canvas& c, const ScreenModel& model,
                      const Snapshot& status, float time, bool reduced_motion) {
    const float breathe = reduced_motion ? 0.0f : std::sin(time * .7f) * 4.0f;
    constexpr float center_x = 1339;
    const float center_y = 475 + breathe;
    // Three depth layers plus the radiating AirPlay glyph form a deliberate
    // empty state, not a false screenshot or simulated connection.
    for (int i = 5; i >= 1; --i)
        c.circle(dl, center_x, center_y, 132 + i * 34,
            IM_COL32(60, 103, 173, static_cast<int>(14 - i * 2)), 96);
    c.ring(dl, center_x, center_y, 153, IM_COL32(116, 172, 245, 75), 1.4f);
    c.ring(dl, center_x, center_y, 116, IM_COL32(116, 172, 245, 100), 1.6f);
    c.circle(dl, center_x, center_y, 88, IM_COL32(29, 60, 101, 205), 72);
    c.icon(dl, model.phase == ScreenPhase::error ? icons::Id::warning :
        model.phase == ScreenPhase::pairing ? icons::Id::lock :
        model.phase == ScreenPhase::connected ? icons::Id::link :
        icons::Id::television, center_x - 39, center_y - 43, 79,
        model.phase == ScreenPhase::error ? red :
        model.phase == ScreenPhase::pairing ? orange : cyan);

    const char* headline = model.phase == ScreenPhase::error ? "No signal" :
                            model.phase == ScreenPhase::pairing ? "Pairing requested" :
                            model.phase == ScreenPhase::connected ? "Preparing your video" :
                            model.phase == ScreenPhase::stopped ? "Receiver is offline" :
                            "Waiting for your screen";
    c.center(dl, headline, center_x, 667, 32, bright, true);
    const char* description = model.phase == ScreenPhase::pairing ? "Enter the PIN to continue" :
                              model.phase == ScreenPhase::connected ? "Your stream will appear automatically" :
                              model.phase == ScreenPhase::error ? "Check the connection details on the left" :
                              "Open Screen Mirroring on your Apple device";
    c.center(dl, description, center_x, 720, 21, secondary);
    if (model.phase == ScreenPhase::pairing && !status.pin.empty())
        c.center(dl, status.pin.c_str(), center_x, 785, 45, orange, true);
}

void draw_preview(ImDrawList* dl, const Canvas& c, const ScreenModel& model,
                  const Snapshot& status, ImTextureID video, int video_w, int video_h,
                  float time, bool reduced_motion) {
    constexpr float x = 766, y = 207, w = 1062, h = 709;
    c.rect(dl, x, y + 17, w, h, IM_COL32(0, 0, 0, 49), 34);
    c.rect(dl, x, y, w, h, IM_COL32(19, 32, 57, 225), 34);
    c.border(dl, x, y, w, h, IM_COL32(138, 168, 219, 73), 34);
    // Perceptual focus of the entire screen: real decoded video or true empty
    // state, with accent colours belonging to the content state.
    dl->AddRectFilledMultiColor(c.at(x + 40, y + 5), c.at(x + w - 40, y + 9),
        violet, cyan, cyan, violet);
    constexpr float px = x + 20, py = y + 20, pw = w - 40, ph = h - 104;
    c.rect(dl, px, py, pw, ph, IM_COL32(8, 16, 34, 255), 22);
    c.border(dl, px, py, pw, ph, IM_COL32(118, 155, 210, 37), 22);
    if (model.has_video) {
        const float scale = std::min(pw / video_w, ph / video_h);
        const float imgw = video_w * scale;
        const float imgh = video_h * scale;
        const float left = px + (pw - imgw) * .5f;
        const float top = py + (ph - imgh) * .5f;
        dl->PushClipRect(c.at(px, py), c.at(px + pw, py + ph), true);
        dl->AddImage(video, c.at(left, top), c.at(left + imgw, top + imgh));
        dl->PopClipRect();
    } else {
        dl->PushClipRect(c.at(px, py), c.at(px + pw, py + ph), true);
        draw_placeholder(dl, c, model, status, time, reduced_motion);
        dl->PopClipRect();
    }
    c.rect(dl, px + 24, py + 22, 155, 40, IM_COL32(27, 44, 72, 209), 18);
    c.circle(dl, px + 43, py + 42, 5, model.has_video ? green : tertiary);
    c.text(dl, model.has_video ? "LIVE VIEW" : "PREVIEW", px + 59, py + 32, 17, bright, true);
    c.icon(dl, model.has_video ? icons::Id::play : icons::Id::display,
        x + 41, y + h - 69, 25, model.has_video ? green : blue);
    c.text(dl, model.has_video ? "AIRPLAY STREAM" : "NO ACTIVE STREAM",
           x + 78, y + h - 66, 18, secondary, true);
    c.rect(dl, x + w - 163, y + h - 76, 120, 38, IM_COL32(50, 41, 89, 189), 15);
    c.icon(dl, icons::Id::signal, x + w - 150, y + h - 69, 20, violet);
    c.text(dl, "VULKAN", x + w - 118, y + h - 69, 16, violet, true);
}

// Authentic geometric DualSense button glyphs (not letters X/O and no
// unlicensed Sony artwork): shape identities distinguish actions in greyscale.
enum class PadGlyph { cross, circle, triangle, square, right_bumper, dpad };
void hint(ImDrawList* dl, const Canvas& c, float x, float y,
          PadGlyph glyph, const char* description, ImU32 symbol_color) {
    c.rect(dl, x, y, 40, 38, IM_COL32(34, 47, 77, 235), 13);
    c.border(dl, x, y, 40, 38, IM_COL32(119, 156, 207, 93), 13);
    const float cx = x + 20, cy = y + 19;
    if (glyph == PadGlyph::cross) {
        c.line(dl, cx - 9, cy - 9, cx + 9, cy + 9, symbol_color, 3);
        c.line(dl, cx - 9, cy + 9, cx + 9, cy - 9, symbol_color, 3);
    } else if (glyph == PadGlyph::circle) {
        c.ring(dl, cx, cy, 11, symbol_color, 3);
    } else if (glyph == PadGlyph::square) {
        c.border(dl, cx - 10, cy - 10, 20, 20, symbol_color, 3, 3);
    } else if (glyph == PadGlyph::triangle) {
        c.line(dl, cx, cy - 12, cx + 12, cy + 9, symbol_color, 3);
        c.line(dl, cx + 12, cy + 9, cx - 12, cy + 9, symbol_color, 3);
        c.line(dl, cx - 12, cy + 9, cx, cy - 12, symbol_color, 3);
    } else if (glyph == PadGlyph::dpad) {
        c.rect(dl, cx - 4, cy - 13, 8, 26, symbol_color, 2);
        c.rect(dl, cx - 13, cy - 4, 26, 8, symbol_color, 2);
    } else {
        c.center(dl, "R1", cx, y + 10, 17, symbol_color, true);
    }
    c.text(dl, description, x + 54, y + 9, 20, secondary);
}

void modal_look(float s) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(33 * s, 28 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 24 * s);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(.07f, .12f, .22f, .99f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.16f, .28f, .45f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.25f, .42f, .63f, 1.0f));
}
void modal_end() { ImGui::PopStyleColor(3); ImGui::PopStyleVar(2); }

// Genuine RAOP callback PIN is rendered in a top-level blocking Vulkan/ImGui
// overlay rather than at the far-right of the video panel. This keeps the PIN
// visible at all supported DPI settings and prevents accidental background
// actions while an Apple client is awaiting authentication.
ScreenAction pairing_overlay(const Snapshot& status, const Canvas& c, bool pairing) {
    if (pairing && !ImGui::IsPopupOpen("Pairing code##mirror"))
        ImGui::OpenPopup("Pairing code##mirror");
    const float s = c.scale;
    ScreenAction result = ScreenAction::none;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize(c.dims(795, 540), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 30 * s);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(.075f, .104f, .184f, 1.f));
    if (ImGui::BeginPopupModal("Pairing code##mirror", nullptr,
                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        if (!pairing) {
            ImGui::CloseCurrentPopup();
        } else {
            auto* dl = ImGui::GetWindowDrawList();
            const auto at = [&](float x, float y) -> ImVec2 {
                const auto o = ImGui::GetWindowPos();
                return {o.x + x * s, o.y + y * s};
            };
            const auto put = [&](const char* content, float x, float y, float size,
                                 ImU32 color, bool weight = false) {
                auto* font = weight && semibold_font ? semibold_font :
                             regular_font ? regular_font : ImGui::GetFont();
                dl->AddText(font, size * s, at(x, y), color, content);
            };
            dl->AddRectFilled(at(0, 0), at(795, 8), orange, 4 * s);
            dl->AddRectFilled(at(48, 43), at(122, 117), IM_COL32(95, 65, 46, 230), 19 * s);
            const Canvas local{{ImGui::GetWindowPos().x, ImGui::GetWindowPos().y}, s};
            local.icon(dl, icons::Id::lock, 71, 63, 33, orange);
            put("PAIRING REQUEST", 152, 57, 21, orange, true);
            put("Connect to MirrorPSX", 48, 142, 39, bright, true);
            put("Enter this code on your Apple device", 48, 206, 25, secondary);

            // Four separate glyph cells provide legibility from a sofa and
            // preserve leading zeroes in the PIN.
            const float first_x = 67;
            for (int i = 0; i < 4; ++i) {
                const float x = first_x + i * 172;
                dl->AddRectFilled(at(x, 273), at(x + 145, 389),
                                  IM_COL32(39, 49, 77, 255), 22 * s);
                dl->AddRect(at(x, 273), at(x + 145, 389),
                            IM_COL32(250, 194, 113, 142), 22 * s, 0, 2 * s);
                const char digit[2] = {i < static_cast<int>(status.pin.size()) ? status.pin[i] : ' ', '\0'};
                auto* font = semibold_font ? semibold_font : ImGui::GetFont();
                const auto ext = font->CalcTextSizeA(76 * s, 100000.f, 0, digit);
                dl->AddText(font, 76 * s, at(x + 72.5f - ext.x / (2 * s), 287), bright, digit);
            }
            put("The code will remain visible until the pairing completes.", 66, 416, 19, secondary);
            ImGui::SetCursorScreenPos(at(270, 463));
            ImGui::PushFont(semibold_font ? semibold_font : ImGui::GetFont(), 22 * s);
            if (ImGui::Button("Stop receiver", c.dims(255, 58)))
                result = ScreenAction::stop;
            ImGui::PopFont();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    return result;
}
} // namespace

void apply_screen_style(ImFont* regular, ImFont* semibold, ImFont* icon_font) {
    regular_font = regular;
    semibold_font = semibold;
    icons_font = icon_font;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0, 0);
    style.WindowBorderSize = 0;
    style.WindowRounding = 0;
    style.FrameRounding = 13;
    style.PopupRounding = 22;
    style.FramePadding = ImVec2(14, 10);
    style.ItemSpacing = ImVec2(14, 14);
    style.Colors[ImGuiCol_NavCursor] = ImVec4(.42f, .78f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(.014f, .025f, .051f, .83f);
}

ScreenAction render_screen(const Snapshot& status, ImTextureID video,
                           int video_width, int video_height, ScreenUiState& ui) {
    const ScreenModel model = make_screen_model(status,
        video != ImTextureID_Invalid && video_width > 0 && video_height > 0);
    reconcile_screen_state(ui, model);
    const bool back = ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                      ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight);
    if (!ui.confirm_stop && model.phase != ScreenPhase::pairing && !ui.fullscreen &&
        (ImGui::IsKeyPressed(ImGuiKey_H) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp)))
        ui.help_open = true;
    if (!ui.confirm_stop && !ui.help_open && model.allow_fullscreen &&
        (ImGui::IsKeyPressed(ImGuiKey_F) || ImGui::IsKeyPressed(ImGuiKey_GamepadR1))) {
        ui.fullscreen = !ui.fullscreen;
        ui.focus_fullscreen = ui.fullscreen;
        ui.focus_primary = !ui.fullscreen;
    }
    if (ui.fullscreen && back) {
        ui.fullscreen = false;
        ui.focus_primary = true;
    }
    const ImVec2 area = ImGui::GetIO().DisplaySize;
    const float s = std::min(area.x / 1920.0f, area.y / 1080.0f);
    const Canvas c{{(area.x - 1920 * s) * .5f, (area.y - 1080 * s) * .5f}, s};
    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.f, .05f);
    const float time = static_cast<float>(ImGui::GetTime());
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize(area, ImGuiCond_Always);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##mirrorpsx-canvas", nullptr, flags);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    backdrop_art(dl, c, time, ui.reduced_motion);
    ScreenAction action = ScreenAction::none;

    if (ui.fullscreen && model.allow_fullscreen) {
        const float video_s = std::min(1920.0f / video_width, 1080.0f / video_height);
        const float w = video_width * video_s, h = video_height * video_s;
        dl->AddImage(video, c.at((1920 - w) * .5f, (1080 - h) * .5f),
            c.at((1920 + w) * .5f, (1080 + h) * .5f));
        dl->AddRectFilledMultiColor(c.at(0, 0), c.at(1920, 160),
            IM_COL32(7, 13, 27, 224), IM_COL32(7, 13, 27, 224),
            IM_COL32(7, 13, 27, 0), IM_COL32(7, 13, 27, 0));
        c.text(dl, "MirrorPSX", 100, 63, 33, bright, true);
        state_chip(dl, c, model, 1390, 64);
        if (action_button(dl, c, "##exit-fullscreen", "Exit fullscreen",
            1626, 918, 260, 70, false, ui.fullscreen_glow, dt,
            ui.focus_fullscreen, icons::Id::expand)) {
            ui.fullscreen = false;
            ui.focus_primary = true;
        }
        ui.focus_fullscreen = false;
        ImGui::End();
        return action;
    }

    brand(dl, c);
    state_chip(dl, c, model, 1588, 66);
    c.line(dl, 96, 163, 1824, 163, stroke);

    // Hero typography and guiding sentence; statuses always come from the
    // actual receiver model (never fabricated streaming or network success).
    c.rect(dl, 97, 214, 44, 5, cyan, 3);
    c.text(dl, "AIRPLAY RECEIVER", 152, 198, 19, cyan, true);
    const char* first = "Your screen.";
    const char* second = "On the big screen.";
    if (model.phase == ScreenPhase::error) { first = "Almost there."; second = "Let's reconnect."; }
    else if (model.phase == ScreenPhase::pairing) { first = "One more step."; second = "Enter your PIN."; }
    else if (model.phase == ScreenPhase::connected) { first = "You're connected."; second = "Preparing video."; }
    else if (model.phase == ScreenPhase::streaming) { first = "Now streaming."; second = "Enjoy the view."; }
    else if (model.phase == ScreenPhase::waiting) { first = "Ready to mirror."; second = "Make it bigger."; }
    c.fit(dl, first, 95, 269, 61, 660, bright, true);
    c.fit(dl, second, 95, 347, 54, 660, blue, true);
    c.fit(dl, model.detail, 99, 447, 23, 633, secondary);
    if (model.phase == ScreenPhase::error)
        c.text(dl, "The display is ready; the network service needs attention.", 99, 486, 20, tertiary);
    else
        c.text(dl, "Designed for the living room, powered by Vulkan.", 99, 487, 20, tertiary);
    draw_status_info(dl, c, model, status);

    const bool started = status.listening;
    if (action_button(dl, c, "##receiver-action",
                      started ? "Stop receiver" : model.phase == ScreenPhase::error ? "Try again" : "Start receiver",
                      96, 908, 365, 76, !started, ui.primary_glow, dt, ui.focus_primary,
                      started ? icons::Id::power : icons::Id::play)) {
        if (started) ui.confirm_stop = true;
        else action = ScreenAction::start;
    }
    ui.focus_primary = false;
    if (action_button(dl, c, "##show-help", "Connection guide",
                      481, 908, 282, 76, false, ui.help_glow, dt, false,
                      icons::Id::help)) {
        ui.help_open = true;
    }

    draw_preview(dl, c, model, status, video, video_width, video_height, time, ui.reduced_motion);
    if (model.allow_fullscreen && action_button(dl, c, "##player-fullscreen", "Fullscreen",
        1500, 925, 255, 63, false, ui.fullscreen_glow, dt, false,
        icons::Id::expand)) {
        ui.fullscreen = true;
        ui.focus_fullscreen = true;
    }
    if ((ImGui::GetIO().BackendFlags & ImGuiBackendFlags_HasGamepad) != 0) {
        hint(dl, c, 97, 1020, PadGlyph::cross, "Select", blue);
        hint(dl, c, 282, 1020, PadGlyph::circle, "Back", red);
        hint(dl, c, 450, 1020, PadGlyph::triangle, "Help", green);
        if (model.allow_fullscreen)
            hint(dl, c, 622, 1020, PadGlyph::right_bumper, "Fullscreen", cyan);
    } else {
        c.text(dl, "ENTER  Select", 99, 1028, 19, secondary);
        c.text(dl, "ESC  Back", 308, 1028, 19, secondary);
        c.text(dl, "H  Connection guide", 467, 1028, 19, secondary);
        if (model.allow_fullscreen) c.text(dl, "F  Fullscreen", 771, 1028, 19, secondary);
    }
    c.text(dl, "MIRRORPSX  /  VULKAN", 1536, 1031, 15, tertiary, true);
    ImGui::End();

    if (model.phase == ScreenPhase::pairing) {
        // Do not allow a help or disconnect dialog to obscure the pairing PIN.
        ui.help_open = false;
        ui.confirm_stop = false;
        return pairing_overlay(status, c, true);
    }
    // Close a prior pairing modal after authentication, without leaking an
    // obsolete PIN into any subsequent connection.
    pairing_overlay(status, c, false);

    if (ui.help_open && !ImGui::IsPopupOpen("Connection guide##mirror"))
        ImGui::OpenPopup("Connection guide##mirror");
    ImGui::SetNextWindowSize(c.dims(650, 520), ImGuiCond_Appearing);
    modal_look(s);
    if (ImGui::BeginPopupModal("Connection guide##mirror", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::PushFont(semibold_font ? semibold_font : ImGui::GetFont(), 32 * s);
        ImGui::TextUnformatted("Connect your Apple device");
        ImGui::PopFont();
        ImGui::PushFont(regular_font ? regular_font : ImGui::GetFont(), 23 * s);
        ImGui::Spacing();
        ImGui::TextWrapped("1. Connect your Apple device and the receiver to the same network.");
        ImGui::Spacing();
        ImGui::TextWrapped("2. Open Control Center and select Screen Mirroring.");
        ImGui::Spacing();
        ImGui::TextWrapped("3. Choose MirrorPSX. If a PIN appears, enter it on your device.");
        ImGui::Spacing();
        ImGui::Checkbox("Reduce background animation", &ui.reduced_motion);
        ImGui::Spacing();
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        if (back || ImGui::Button("Got it", ImVec2(193 * s, 60 * s))) {
            ui.help_open = false;
            ImGui::CloseCurrentPopup();
            ui.focus_primary = true;
        }
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    modal_end();

    if (ui.confirm_stop && !ImGui::IsPopupOpen("Stop receiver?##mirror"))
        ImGui::OpenPopup("Stop receiver?##mirror");
    ImGui::SetNextWindowSize(c.dims(620, 300), ImGuiCond_Appearing);
    modal_look(s);
    if (ImGui::BeginPopupModal("Stop receiver?##mirror", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::PushFont(semibold_font ? semibold_font : ImGui::GetFont(), 31 * s);
        ImGui::TextUnformatted("Disconnect your device?");
        ImGui::PopFont();
        ImGui::PushFont(regular_font ? regular_font : ImGui::GetFont(), 23 * s);
        ImGui::TextWrapped("The current mirroring session will end. You can restart the receiver at any time.");
        ImGui::Spacing();
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        if (back || ImGui::Button("Keep receiving", ImVec2(248 * s, 63 * s))) {
            ui.confirm_stop = false;
            ImGui::CloseCurrentPopup();
            ui.focus_primary = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disconnect", ImVec2(218 * s, 63 * s))) {
            ui.confirm_stop = false;
            action = ScreenAction::stop;
            ImGui::CloseCurrentPopup();
            ui.focus_primary = true;
        }
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    modal_end();
    return action;
}
} // namespace mirror
