// MirrorPSX PS5 decoder ABI smoke probe.
// Compiled independently from the UI-only PKG; no AirPlay/network listener.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver/decoder.hpp"

extern "C" int main() {
    mirror::State state;
    mirror::Decoder decoder(state);
    if (!decoder.open(false)) return 1; // H.264
    decoder.flush();
    if (!decoder.open(true)) return 2;  // HEVC
    decoder.flush();
    return 0;
}
