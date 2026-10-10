// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screen.hpp"
#include "ui/screen_model.hpp"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cstdio>

namespace mirror {
namespace {
constexpr ImU32 bg = IM_COL32(8, 13, 22, 255);
constexpr ImU32 panel = IM_COL32(17, 25, 40, 255);
constexpr ImU32 panel_top = IM_COL32(23, 34, 52, 255);
constexpr ImU32 line = IM_COL32(52, 68, 91, 190);
constexpr ImU32 white = IM_COL32(245, 248, 255, 255);
constexpr ImU32 subtext = IM_COL32(161, 178, 200, 255);
constexpr ImU32 faint = IM_COL32(113, 135, 163, 255);
constexpr ImU32 accent = IM_COL32(91, 185, 255, 255);
constexpr ImU32 accent_dark = IM_COL32(36, 105, 162, 255);
constexpr ImU32 good = IM_COL32(83, 220, 164, 255);
constexpr ImU32 amber = IM_COL32(255, 197, 112, 255);
constexpr ImU32 danger = IM_COL32(255, 116, 130, 255);

float ui_scale(ImVec2 area) {
    return std::clamp(std::min(area.x / 1600.0f, area.y / 900.0f), 0.65f, 2.5f);
}
void text(ImDrawList* dl, ImVec2 position, float size, ImU32 color,
          const char* value, float wrap = 0.0f) {
    dl->AddText(ImGui::GetFont(), size, position, color, value, nullptr, wrap);
}
ImVec2 text_size(float size, const char* value) {
    return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, value);
}
void label(ImDrawList* dl, ImVec2 pos, float s, const char* value) {
    text(dl, pos, 15 * s, faint, value);
}
void corner(ImDrawList* dl, ImVec2 p, float radius, ImU32 color) {
    dl->AddCircleFilled(p, radius, color, 24);
}

bool action_button(ImDrawList* dl, const char* id, const char* caption,
                   ImVec2 min, ImVec2 size, float s, bool primary,
                   bool enabled, bool focus) {
    ImGui::SetCursorScreenPos(min);
    if (focus) ImGui::SetKeyboardFocusHere();
    if (!enabled) ImGui::BeginDisabled();
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool active = enabled && (ImGui::IsItemFocused() || ImGui::IsItemHovered());
    if (!enabled) ImGui::EndDisabled();
    const ImU32 fill = !enabled ? IM_COL32(45, 55, 73, 255)
                           : primary ? (active ? IM_COL32(145, 218, 255, 255) : accent)
                           : (active ? IM_COL32(55, 76, 102, 255) : IM_COL32(35, 48, 68, 255));
    dl->AddRectFilled(min, {min.x + size.x, min.y + size.y}, fill, 12 * s);
    if (active) dl->AddRect({min.x - 3 * s, min.y - 3 * s},
                            {min.x + size.x + 3 * s, min.y + size.y + 3 * s},
                            accent, 14 * s, 0, 2 * s);
    const float font = 20 * s;
    const auto bounds = text_size(font, caption);
    text(dl, {min.x + (size.x - bounds.x) / 2, min.y + (size.y - bounds.y) / 2},
         font, primary ? bg : white, caption);
    return pressed && enabled;
}

void status_pill(ImDrawList* dl, ImVec2 min, float s, const ScreenModel& model) {
    const ImU32 tint = model.phase == ScreenPhase::error ? danger
                       : model.phase == ScreenPhase::pairing ? amber
                       : model.phase == ScreenPhase::streaming ? good
                       : model.phase == ScreenPhase::stopped ? faint : accent;
    const float font = 15 * s;
    const float w = text_size(font, model.status_label).x + 52 * s;
    dl->AddRectFilled(min, {min.x + w, min.y + 36 * s}, panel_top, 18 * s);
    corner(dl, {min.x + 18 * s, min.y + 18 * s}, 5 * s, tint);
    text(dl, {min.x + 33 * s, min.y + 9 * s}, font, white, model.status_label);
}

void video_placeholder(ImDrawList* dl, ImVec2 min, ImVec2 size, float s,
                       const ScreenModel& model, const Snapshot& snapshot) {
    const ImVec2 center{min.x + size.x * 0.5f, min.y + size.y * 0.46f};
    const float radius = 51 * s;
    dl->AddCircle(center, radius + 20 * s, IM_COL32(75, 113, 156, 60), 64, 1.5f * s);
    dl->AddCircle(center, radius, IM_COL32(111, 167, 222, 130), 64, 2.0f * s);
    dl->AddRect({center.x - 28 * s, center.y - 19 * s},
                {center.x + 28 * s, center.y + 16 * s},
                accent, 6 * s, 0, 3 * s);
    dl->AddLine({center.x, center.y + 17 * s}, {center.x, center.y + 28 * s}, accent, 3 * s);
    dl->AddLine({center.x - 14 * s, center.y + 28 * s}, {center.x + 14 * s, center.y + 28 * s}, accent, 3 * s);
    const char* headline = model.phase == ScreenPhase::pairing ? "Pairing in progress"
                            : model.phase == ScreenPhase::connected ? "Preparing your stream"
                            : model.phase == ScreenPhase::error ? "Receiver unavailable"
                            : model.phase == ScreenPhase::stopped ? "Receiver is offline"
                            : "Waiting for a device";
    const auto title_bounds = text_size(28 * s, headline);
    text(dl, {center.x - title_bounds.x * .5f, center.y + 91 * s}, 28 * s, white, headline);
    const char* detail = model.phase == ScreenPhase::pairing ? "Confirm the PIN on your Apple device"
                         : model.phase == ScreenPhase::connected ? "Video will appear here automatically"
                         : "Your mirrored display will appear here";
    const auto desc_bounds = text_size(18 * s, detail);
    text(dl, {center.x - desc_bounds.x * .5f, center.y + 133 * s}, 18 * s, subtext, detail);
    if (model.phase == ScreenPhase::pairing && !snapshot.pin.empty()) {
        const float pin_size = 50 * s;
        const auto pin_bounds = text_size(pin_size, snapshot.pin.c_str());
        text(dl, {center.x - pin_bounds.x * .5f, center.y - 140 * s},
             pin_size, amber, snapshot.pin.c_str());
    }
}

void step(ImDrawList* dl, float x, float y, float s, const char* number,
          const char* heading, const char* description) {
    dl->AddRectFilled({x, y}, {x + 36 * s, y + 36 * s}, panel_top, 11 * s);
    const auto number_bounds = text_size(17 * s, number);
    text(dl, {x + 18 * s - number_bounds.x * .5f, y + 18 * s - number_bounds.y * .5f},
         17 * s, accent, number);
    text(dl, {x + 51 * s, y - 1 * s}, 19 * s, white, heading);
    text(dl, {x + 51 * s, y + 25 * s}, 16 * s, subtext, description);
}
}

void apply_screen_style() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    style.WindowPadding = ImVec2(0, 0);
    style.WindowBorderSize = 0;
    style.WindowRounding = 0;
    style.FrameRounding = 12;
    style.PopupRounding = 16;
    style.FramePadding = ImVec2(12, 8);
    style.ItemSpacing = ImVec2(12, 12);
    style.Colors[ImGuiCol_NavCursor] = ImVec4(.36f, .73f, 1.f, 1.f);
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.01f, .02f, .04f, .78f);
}

ScreenAction render_screen(const Snapshot& status, ImTextureID video,
                           int video_width, int video_height, ScreenUiState& ui) {
    const auto model = make_screen_model(status, video != ImTextureID_Invalid &&
                                        video_width > 0 && video_height > 0);
    reconcile_screen_state(ui, model);
    if (!ui.confirm_stop && model.allow_fullscreen &&
        (ImGui::IsKeyPressed(ImGuiKey_F) || ImGui::IsKeyPressed(ImGuiKey_GamepadR1))) {
        ui.fullscreen = !ui.fullscreen;
        ui.focus_fullscreen = ui.fullscreen;
        ui.focus_primary = !ui.fullscreen;
    }
    if (ui.fullscreen && (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                          ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight))) {
        ui.fullscreen = false;
        ui.focus_primary = true;
    }

    const ImVec2 area = ImGui::GetIO().DisplaySize;
    const float s = ui_scale(area);
    const float margin = 54 * s;
    const float header = 112 * s;
    const float footer = 67 * s;
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize(area, ImGuiCond_Always);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##mirrorpsx-canvas", nullptr, flags);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor({0,0}, area, IM_COL32(11,22,37,255),
         bg, IM_COL32(8,14,24,255), IM_COL32(11,19,32,255));

    if (ui.fullscreen && model.allow_fullscreen) {
        const float ratio = std::min(area.x / video_width, area.y / video_height);
        const ImVec2 size{video_width * ratio, video_height * ratio};
        const ImVec2 pos{(area.x - size.x) * .5f, (area.y - size.y) * .5f};
        dl->AddImage(video, pos, {pos.x + size.x, pos.y + size.y});
        dl->AddRectFilledMultiColor({0,0}, {area.x, 100*s},
            IM_COL32(5,10,19,210), IM_COL32(5,10,19,210),
            IM_COL32(5,10,19,0), IM_COL32(5,10,19,0));
        text(dl, {margin, 26*s}, 23*s, white, "MIRRORPSX");
        status_pill(dl, {margin + 202*s, 24*s}, s, model);
        if (action_button(dl,"##exitfullscreen","Exit fullscreen",
                          {area.x-margin-212*s,23*s},{212*s,51*s},s,false,true,ui.focus_fullscreen)) {
            ui.fullscreen = false;
            ui.focus_primary = true;
        }
        ui.focus_fullscreen = false;
        text(dl, {margin, area.y-44*s}, 16*s, subtext,
             "R1 / F  Fullscreen    O / Esc  Back");
        ImGui::End();
        return ScreenAction::none;
    }

    // Header: clear product identity and always-visible receiver status.
    dl->AddRectFilled({margin, 38*s}, {margin + 5*s, 76*s}, accent, 3*s);
    text(dl, {margin + 22*s, 33*s}, 36*s, white, "MIRRORPSX");
    text(dl, {margin + 245*s, 47*s}, 16*s, faint, "WIRELESS DISPLAY");
    status_pill(dl, {area.x - margin - 210*s, 38*s}, s, model);
    dl->AddLine({margin, header-8*s}, {area.x-margin, header-8*s}, line, 1*s);

    const float usable_h = std::max(180*s, area.y-header-footer);
    const float usable_w = area.x-2*margin;
    const float side_w = std::clamp(usable_w * .32f, 360*s, 475*s);
    const float gap = 24*s;
    const ImVec2 side{margin,header+10*s};
    const ImVec2 display{side.x+side_w+gap,side.y};
    const ImVec2 display_size{std::max(240*s,usable_w-side_w-gap), usable_h};

    // Left-hand controller: meaningful status, instructions and a single
    // primary action. No decorative toggles without real behavior.
    dl->AddRectFilled(side, {side.x+side_w,side.y+usable_h}, panel, 20*s);
    const float sx = side.x + 32*s;
    label(dl, {sx,side.y+31*s},s,"RECEIVER");
    text(dl,{sx,side.y+60*s},30*s,white,model.headline,side_w-64*s);
    text(dl,{sx,side.y+112*s},18*s,subtext,model.detail,side_w-64*s);

    const float info_y = side.y+185*s;
    dl->AddLine({sx,info_y},{side.x+side_w-32*s,info_y},line,1*s);
    label(dl, {sx,info_y+22*s},s,"DEVICE NAME");
    text(dl,{sx,info_y+49*s},21*s,white,status.name.c_str(),side_w-64*s);
    if (!status.client.empty()) {
        label(dl,{sx,info_y+92*s},s,"CONNECTED DEVICE");
        text(dl,{sx,info_y+118*s},20*s,white,status.client.c_str(),side_w-64*s);
    } else if (model.phase == ScreenPhase::waiting) {
        label(dl,{sx,info_y+92*s},s,"HOW TO CONNECT");
        step(dl,sx,info_y+126*s,s,"1","Same network","Connect both devices to Wi-Fi");
        step(dl,sx,info_y+193*s,s,"2","Screen Mirroring","Open it in Control Center");
        step(dl,sx,info_y+260*s,s,"3","Select MirrorPSX","Choose this receiver");
    }
    if (model.phase == ScreenPhase::pairing && !status.pin.empty()) {
        label(dl,{sx,info_y+100*s},s,"PAIRING PIN");
        text(dl,{sx,info_y+128*s},49*s,amber,status.pin.c_str());
        text(dl,{sx,info_y+190*s},17*s,subtext,"Enter this code on your device.");
    }
    if (model.phase == ScreenPhase::streaming) {
        label(dl,{sx,info_y+100*s},s,"VIDEO SIGNAL");
        char metrics[90];
        std::snprintf(metrics,sizeof(metrics),"%d x %d   |   %llu frames",
                      status.width,status.height,
                      static_cast<unsigned long long>(status.frames));
        text(dl,{sx,info_y+132*s},19*s,white,metrics);
    }
    if (!status.error.empty()) {
        const float warn_y = std::min(info_y+210*s,side.y+usable_h-167*s);
        dl->AddRectFilled({sx,warn_y},{side.x+side_w-32*s,warn_y+83*s},
                          IM_COL32(71,37,53,255),12*s);
        text(dl,{sx+15*s,warn_y+12*s},17*s,danger,"ATTENTION");
        text(dl,{sx+15*s,warn_y+37*s},16*s,white,status.error.c_str(),side_w-95*s);
    }
    const float btn_h = 60*s;
    const float btn_y = side.y+usable_h-93*s;
    ScreenAction action=ScreenAction::none;
    const bool started = status.listening;
    if (action_button(dl,"##receiver-action",started?"Stop receiver":"Start receiver",
                      {sx,btn_y},{side_w-64*s,btn_h},s,!started,true,ui.focus_primary)) {
        if (started) ui.confirm_stop = true;
        else action = ScreenAction::start;
    }
    ui.focus_primary = false;

    // Right hand side: video fills the available safe area, preserving aspect.
    dl->AddRectFilled(display,{display.x+display_size.x,display.y+display_size.y},
                      IM_COL32(12,21,35,255),20*s);
    const float inset=17*s;
    const ImVec2 picture{display.x+inset,display.y+inset};
    const ImVec2 picture_size{display_size.x-2*inset,display_size.y-2*inset-62*s};
    dl->AddRectFilled(picture,{picture.x+picture_size.x,picture.y+picture_size.y},
                      bg,14*s);
    if (model.has_video) {
        const float ratio=std::min(picture_size.x/video_width,picture_size.y/video_height);
        const ImVec2 size{video_width*ratio,video_height*ratio};
        const ImVec2 min{picture.x+(picture_size.x-size.x)/2,
                         picture.y+(picture_size.y-size.y)/2};
        dl->PushClipRect(picture,{picture.x+picture_size.x,picture.y+picture_size.y},true);
        dl->AddImage(video,min,{min.x+size.x,min.y+size.y});
        dl->PopClipRect();
    } else {
        video_placeholder(dl,picture,picture_size,s,model,status);
    }
    dl->AddLine({display.x+inset,display.y+display_size.y-55*s},
                {display.x+display_size.x-inset,display.y+display_size.y-55*s},
                line,1*s);
    corner(dl,{display.x+35*s,display.y+display_size.y-27*s},5*s,
           model.has_video?good:faint);
    text(dl,{display.x+50*s,display.y+display_size.y-37*s},16*s,subtext,
         model.has_video?"LIVE PREVIEW":"DISPLAY PREVIEW");
    if (model.allow_fullscreen) {
        if (action_button(dl,"##fullscreen","Fullscreen",
                          {display.x+display_size.x-193*s,display.y+display_size.y-47*s},
                          {173*s,39*s},s,false,true,false)) {
            ui.fullscreen=true;
            ui.focus_fullscreen=true;
        }
    }

    const char* footer_text = "D-PAD  Navigate     X  Select     R1 / F  Fullscreen     O / Esc  Back";
    text(dl,{margin,area.y-37*s},16*s,faint,footer_text);
    ImGui::End();

    // Stopping a live connection is destructive: require a second deliberate
    // action, especially when input comes from a controller.
    if (ui.confirm_stop) ImGui::OpenPopup("Stop receiver?");
    ImGui::SetNextWindowSize({450*s, 220*s}, ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26*s, 20*s));
    if (ImGui::BeginPopupModal("Stop receiver?", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::PushFont(nullptr, 22*s);
        ImGui::TextUnformatted("Disconnect this device?");
        ImGui::PopFont();
        ImGui::PushFont(nullptr, 17*s);
        ImGui::TextWrapped("The current AirPlay session will end. You can start receiving again at any time.");
        ImGui::Spacing();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
            ui.confirm_stop = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        if (ImGui::Button("Keep receiving", {183*s, 52*s})) {
            ui.confirm_stop = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Disconnect", {150*s, 52*s})) {
            ui.confirm_stop = false;
            action = ScreenAction::stop;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    return action;
}
}
