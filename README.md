# MirrorPSX

Experimental desktop AirPlay screen-mirroring receiver. The application receives
H.264/HEVC video using a vendored UxPlay RAOP implementation, decodes it using
FFmpeg, and presents frames with a GLFW/Vulkan/Dear ImGui desktop interface.

## Project status

The desktop application builds on Windows with MSYS2 MinGW64. Its actual UI
draws through Vulkan (Dear ImGui's Vulkan backend), **not a WebView or a mocked
console screen**. It features a TV-readable, responsive player, controller
focus navigation, receiver controls, connection status, pairing PIN, fullscreen
presentation, and confirmation before disconnecting.
Decoder/state/UI tests run under CTest. Real device-to-device streaming and
pairing require validation with Apple clients on a LAN.
The GLFW desktop host is **not** a PS5 application. The UI and Vulkan renderer
are compiled separately so a native PS5 display/input host can reuse them;
there is no verified native PS5 ELF or package yet. See
[PS5 porting status](docs/PS5_PORT.md).
The design uses bundled Figtree typography, Font Awesome Free vector icon
glyphs, and native DualSense controller symbols, with licenses documented in
[UI assets](docs/ASSETS.md).

## Build on Windows (MSYS2 MinGW64)

Install the MinGW64 development packages for GCC, CMake, make, pkgconf,
GLFW, Vulkan headers/loader, FFmpeg (libavcodec, libavutil, libswscale),
OpenSSL, and libplist 2.0. Git/network access is needed to fetch pinned
Dear ImGui once, or pass `-DMIRRORPSX_IMGUI_DIR=/path/to/imgui`.

```sh
cmake -S . -B build -G "MinGW Makefiles" \
  -DCMAKE_PREFIX_PATH=C:/msys64/mingw64
cmake --build build -j 8
ctest --test-dir build --output-on-failure
./build/MirrorPSX.exe
```

When using PowerShell instead of the MinGW64 shell, add
`C:\msys64\mingw64\bin` to PATH before configuring, building, testing, or
launching, and select the MinGW64 GCC toolchain. Do not mix MSVC and MinGW64
dependencies. Linux/macOS are intended by the imported UxPlay and cross-platform
desktop dependencies, but their complete builds have not yet been verified.

## Usage

Launch MirrorPSX, then use an Apple AirPlay mirroring client on the same LAN.
The program advertises the name `MirrorPSX` using mDNS and displays the
pairing PIN when requested. Stop/Start controls manage the receiver. Pairing
state is written to `mirrorpsx.key` in the working directory; keep that file
private and persist it across restarts when you want to retain the identity.
The receiver asks the OS for free TCP/UDP ports, then advertises the selected
AirPlay service port over mDNS. Earlier builds hard-coded ports 7100–7104,
which overlapped a Windows excluded-port range and produced socket error
`10013`. The dynamic-port fix has been confirmed to initialize the listener
on the test Windows machine. Allow the advertised AirPlay port, negotiated
stream ports and multicast DNS through any host/network firewall.

For a new connection requesting PIN authentication, **the four-digit code is
displayed in a centered modal overlay** on the receiving computer. Keep this
window visible and enter the digits (including leading zeros) on the Apple
device. The PIN persists across temporary RTSP socket closes and disappears
when pairing/streaming succeeds or the receiver is stopped. If the code never
appears, consult [pairing troubleshooting](docs/PAIRING.md).

The Windows desktop host requests Per-Monitor V2 DPI awareness before creating
its GLFW window. This avoids the Windows 150%-scaling mismatch that previously
caused the right half of the UI (and PIN) to render outside the visible window.

## Architecture

* `src/receiver/airplay.*`: RAOP callback bridge, advertisements and session
  lifecycle; imported UxPlay code is confined to `third_party/uxplay`.
* `src/receiver/decoder.*`: FFmpeg codec selection and decoded RGBA output.
* `src/receiver/state.hpp`: mutex-protected connection state and latest frame.
* `src/platform/video_texture.*`: Vulkan staging buffer, optimal-tiled GPU
  image, and sampled descriptor for the live stream.
* `src/ui/screen_model.hpp`: platform-independent state machine and UI actions.
* `src/ui/screen.*`: TV-responsive receiver controls and video presentation,
  rendered through Dear ImGui; no GLFW/Win32 headers in these files.
* `src/ui/gamepad_navigation.hpp`: native gamepad-to-ImGui input bridge.
* `src/platform/desktop.cpp`: GLFW window, Vulkan swapchain and application loop.

Video upload uses a host-visible staging buffer with
`vkCmdCopyBufferToImage` into an optimal-tiled GPU sampled image. Upload still
waits for GPU completion on each new frame; asynchronous transfer and frame
latency measurements remain future performance work. Decode runs on the UxPlay
callback thread; the UI reads a snapshot of the latest available frame.

For UI integration independently of desktop or console networking use
`cmake -S . -B build-ui -DMIRRORPSX_UI_ONLY=ON` to compile `mirror_screen`.
For both the screen and real ImGui Vulkan backend, without GLFW or UxPlay,
use `-DMIRRORPSX_VULKAN_UI_ONLY=ON` to compile `mirror_ui`. Both modes
also build the UI tests and accept `-DMIRRORPSX_IMGUI_DIR=...` for offline
builds. These modes are **components**, not PS5 executable targets.

## Known limitations / next steps

* Further exercise device pairing, password failure/retry, reconnect cycles,
  prolonged H.264/HEVC sessions and multi-client contention on physical devices.
* Improve frame upload with GPU fences and asynchronous frame staging to avoid
  the current queue/device idle waits.
* Build and verify a separate native PS5 host and full AirPlay runtime.
* Implement supported audio handling if the scope expands to audio reception.
* Complete platform builds, Windows packaging, automated CI and lifecycle tests.
* Replace the fixed receiver identity and port scheme with configurable settings
  after the desired user-facing configuration is established.

The repository includes GPL-3.0-or-later application sources and vendored
UxPlay/third-party code with their own license notices. See `LICENSE` and
`third_party/` before redistribution.
