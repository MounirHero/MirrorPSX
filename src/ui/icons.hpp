// SPDX-License-Identifier: GPL-3.0-or-later
// Font Awesome Free 6.x icon identities. Font asset: assets/fonts/fa-solid-900.ttf
// Copyright Fonticons, Inc. - SIL Open Font License 1.1, see assets/licenses.
#pragma once
#include "imgui.h"

namespace mirror::icons {
enum class Id {
    display, wifi, warning, expand, mobile, help, check, link, power,
    refresh, lock, play, signal, info, chevron, shield, television
};
inline const char* glyph(Id id) {
    switch (id) {
        case Id::display:    return "\uF108";
        case Id::wifi:       return "\uF1EB";
        case Id::warning:    return "\uF071";
        case Id::expand:     return "\uF065";
        case Id::mobile:     return "\uF3CD";
        case Id::help:       return "\uF128";
        case Id::check:      return "\uF00C";
        case Id::link:       return "\uF0C1";
        case Id::power:      return "\uF011";
        case Id::refresh:    return "\uF2F1";
        case Id::lock:       return "\uF023";
        case Id::play:       return "\uF04B";
        case Id::signal:     return "\uF0E7";
        case Id::info:       return "\uF129";
        case Id::chevron:    return "\uF054";
        case Id::shield:     return "\uF132";
        case Id::television: return "\uF26C";
    }
    return "?";
}
inline constexpr ImWchar font_ranges[] = {
    0xF000,0xF0FF, 0xF100,0xF1FF, 0xF200,0xF2FF,
    0xF300,0xF3FF, 0
};
} // namespace mirror::icons
