# MirrorPSX

Experimental desktop AirPlay screen-mirroring receiver. The application receives
H.264/HEVC video using a vendored UxPlay RAOP implementation, decodes it using
FFmpeg, and presents frames with a GLFW/Vulkan/Dear ImGui desktop interface.

## Project status

The desktop application builds on Windows with MSYS2 MinGW64 and exposes a
receiver start/stop control, status, pairing PIN and live video surface.
Decoder/state tests run under CTest. Device-to-device streaming and pairing
still require validation with physical Apple clients on a LAN. No audio
playback or PS4/PS5 application target is implemented in this tree.

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
The receiver currently attempts fixed TCP/UDP ports 7100–7104; allow the
relevant traffic and mDNS through the host firewall. Windows socket error
`10013` means access was denied to a socket, commonly because of endpoint
protection, firewall policy or a reserved/excluded port range. It must be
investigated on the target host rather than treated as successful discovery.

## Architecture

* `src/receiver/airplay.*`: RAOP callback bridge, advertisements and session
  lifecycle; imported UxPlay code is confined to `third_party/uxplay`.
* `src/receiver/decoder.*`: FFmpeg codec selection and decoded RGBA output.
* `src/receiver/state.hpp`: mutex-protected connection state and latest frame.
* `src/platform/video_texture.*`: host-visible Vulkan image, sampled by ImGui.
* `src/ui/screen.*`: basic controls, connection status and stream presentation.
* `src/platform/desktop.cpp`: GLFW window, Vulkan swapchain and application loop.

Video upload is currently synchronous and relies on host-visible linear-tiled
Vulkan images. This is a compatibility/performance limitation; devices without
the required linear image format features may need a staging-buffer and
optimal-tiling implementation. Decode runs on the UxPlay callback thread;
the UI reads a snapshot of the latest available frame, not a queued history.

## Known limitations / next steps

* Verify real-world AirPlay discovery, pairing, reconnection, H.264 and HEVC.
* Improve frame upload with persistent staging buffers and GPU synchronization.
* Implement supported audio handling if the scope expands to audio reception.
* Complete platform builds, Windows packaging, automated CI and lifecycle tests.
* Replace the fixed receiver identity and port scheme with configurable settings
  after the desired user-facing configuration is established.

The repository includes GPL-3.0-or-later application sources and vendored
UxPlay/third-party code with their own license notices. See `LICENSE` and
`third_party/` before redistribution.
