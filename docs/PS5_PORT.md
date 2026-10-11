# PS5 compatibility and Vulkan contract

## Verified versus planned

### Native Vulkan UI milestone (October 2026)

The native `src/platform/ps5_main.cpp` now **cross-compiles and links as an
OpenProspero x86-64 PS5 application ELF** using the RADV and platform archives
already built for Hyperion. This is MirrorPSX's real Dear ImGui Vulkan
renderer and screen composition, not a web frontend and not Hyperion's
unrelated UI renderer. It uses `VK_KHR_display` to obtain a console surface,
creates the graphics queue and swapchain, initializes Dear ImGui's Vulkan
backend, reads DualSense via `openprospero::ps5::GamePad`, and renders the
existing UI with bundled Figtree/Font Awesome fonts. Hold OPTIONS for 1.2
seconds to exit.

An experimental OpenProspero PKG was produced with an `eboot.bin`, the fonts
and a locally verified source-built `sce_module/libc.prx`. `prospero validate`
reported zero structural errors and warnings; `prospero extract` confirmed
the payload and asset inventory. **The console has not yet verified that it
installs, starts, displays a frame or responds to DualSense.** Package
structure, ELF linking and working GPU execution are separate milestones.

The first PKG deliberately runs in **UI-only mode**; no AirPlay service is
advertised and there is no decoder or audio pipeline on the console. Pressing
Start receiver reports that networking is not built into this version. The
Windows application's manually tested iPad pairing and Vulkan mirroring are
unchanged.

The existing `src/platform/desktop.cpp` owns a GLFW window and MUST NOT be
compiled as the PS5 host. `src/platform/ps5_main.cpp` is the separate PS5
host and deliberately does not link GLFW, Win32 WSI, FFmpeg or UxPlay. Its
compile/link success is not a physical-console validation.

The automated tests performed so far are Windows/MinGW64. The project owner
has manually confirmed iPad PIN pairing and streaming into the Windows Vulkan
host, but **no PS5-specific runtime validation** has been performed.
The standalone `mirror_screen`
and `mirror_ui` targets compile and the UI simulation passes at 1280x720,
1920x1080 and 3840x2160; this does *not* establish successful execution on PS5.

## Graphical architecture

```text
AirPlay / RAOP callbacks --> FFmpeg --> State::VideoFrame
                                       |
           host input ---> ImGui IO <--+-- ScreenModel / ScreenUiState
                            |          |
                     render_screen()   |
                            |          |
               Dear ImGui draw data    |
                            |          |
               imgui_impl_vulkan       |
                            |          |
                PS5 Vulkan driver <--- VideoTexture staging/optimal GPU image
                            |
              native PS5 display surface and swapchain
```

`mirror_screen` contains the reusable ImGui presentation and state model,
without GLFW, Win32, platform networking or RAOP. `mirror_ui` additionally
builds the **real** Dear ImGui Vulkan backend and `VideoTexture`. No OpenGL
or WebView renderer is used for the UI. The native PS5 host must provide its
own window/display lifecycle and swapchain instead of using the desktop GLFW
backend.

## Buildable component checkpoints

```sh
# UI-independent of native platform and GPU
cmake -S . -B build-ui -DMIRRORPSX_UI_ONLY=ON
cmake --build build-ui
ctest --test-dir build-ui --output-on-failure

# Screen plus Vulkan UI/video sources, without GLFW or FFmpeg/UxPlay
cmake -S . -B build-vulkan-ui -DMIRRORPSX_VULKAN_UI_ONLY=ON
cmake --build build-vulkan-ui
ctest --test-dir build-vulkan-ui --output-on-failure
```

When cross-compiling with OpenProspero, supply the **actual toolchain/sysroot**
and a usable Vulkan loader import/headers for the console build. These commands
do not automatically install a PS5 toolchain or link an ELF. Set
`-DBUILD_TESTING=OFF` when compiling these libraries for a console sysroot;
the host-only CTest executables are not console payloads.

## Native PS5 host responsibilities

1. Initialize the PS5 Vulkan loader/dispatch (for example the RADV-based
   VK_KHR_display path), physical device, present-capable graphics queue,
   render pass and swapchain. Do not link a GLFW or Win32 WSI backend.
2. Initialize Dear ImGui, enable keyboard/gamepad navigation, use a scalable
   bundled/default vector font, and initialize `ImGui_ImplVulkan` with that
   device/queue/render pass.
3. Read DualSense input from the native controller service and map its Cross,
   Circle, D-pad and R1 buttons to the platform-neutral `GamepadNavigation`
   bit flags. Call `submit_gamepad_navigation(io, sample)` **before**
   `ImGui::NewFrame()`. Never feed the same events twice through GLFW.
4. Each frame, call `render_screen(snapshot, texture_id, width, height, ui)`,
   dispatch the returned `ScreenAction` to the receiver, record the ImGui
   Vulkan draw data in the swapchain command buffer, then present.
5. On video updates, upload the decoded RGBA frame via `VideoTexture`. A native
   host must ensure descriptor and GPU command lifetime is synchronized, and
   choose a valid Vulkan queue family for uploads.
6. Bring UxPlay networking, mDNS, OpenSSL, libplist, H.264/HEVC FFmpeg decode
   and PS5-native threading to the console toolchain. These dependencies are
   **not** supplied by the UI-only builds.
7. Use an app-private persistent directory for the pairing identity rather
   than assuming the desktop working directory exists or is writable.
8. Test on an actual PS5: first visible frame, navigation, PIN pairing,
   mirroring, reconnection, texture resize, presentation after suspend,
   reliable shutdown and GPU validation/performance.

The Hyperion development environment supplied reference-tested
`VK_KHR_display`, RADV ICD initialization, OpenProspero pad mapping and
identity-syscall guards. The build currently reuses Hyperion's pinned RADV and
platform archives plus its SDK-compatible identity wrapper, without copying
Hyperion's GUI or moving unrelated runtime code into MirrorPSX.
These references are not proof of this application's console compatibility.

### PacBrew dependency boundary

PacBrew's PS5 ports include FFmpeg, OpenSSL and networking/codec libraries
useful for a future AirPlay-capable runtime. A PacBrew package compiled for
ps5-payload-sdk must not be assumed ABI-compatible with the OpenProspero
*application-profile* ELF. Import it only into a separate, version-pinned
dependency prefix, inspect symbol dependencies, and link a minimal RTSP/codec
probe before enabling a live receiver. OS-level multicast DNS, UDP socket
binding, writable identity storage and companion privileges all require
actual-console tests. The companion is not needed to draw the Vulkan UI.
As a concrete step, the pinned PacBrew v0.40.2 H.264/HEVC decoder probe now
cross-compiles and links as an application-profile ELF. It uses a **test-only**
libiconv ABI shim and has not run on console. Details and provenance:
[PacBrew bring-up](PACBREW.md).

## UX and navigation specification

The UI is deliberately designed for a 16:9 television at 720p, 1080p and 4K.
The picture receives most of the space; the remaining control column explains
how to start mirroring without requiring a manual. Waiting, pairing, connected,
live and error states use distinct labels and clear actions. The PIN is
prominent only when one is active. The image is letterboxed without cropping,
and fullscreen has a visible exit. The stopped connection is never described
as live. A stop request opens a confirmation before disconnecting.

| Control | Expected behavior |
| --- | --- |
| D-pad | Move focus between available actions |
| Cross (X) | Confirm focused action |
| Circle (O) | Exit fullscreen or cancel a dialog |
| R1 | Toggle fullscreen while video is available |
| Keyboard arrows / Enter | Navigate / confirm |
| F | Toggle fullscreen while video is available |
| Escape | Exit fullscreen or cancel |

The native host must apply the console's appropriate accessibility and input
conventions. Verify button focus and readable sizing on a TV, not only with
mouse input on a monitor. If a format is not supported by the device's Vulkan
optimal tiling, the uploader reports an error instead of silently drawing
corrupted video.

## Port completion gates

**Completed off-device:** native host and UI cross-compilation, PS5 ELF link,
experimental PKG generation and extraction, package structural verification,
Windows AirPlay/iPad mirroring and Windows UI regression suite.

**Not yet satisfied:** physical PS5 PKG install/first-frame test, Vulkan device
validation, controller usability test, console-native UxPlay/FFmpeg/OpenSSL
integration, companion network broker where required, actual AirPlay pairing/
streaming from the PS5 target and low-latency frame synchronization on-console.
