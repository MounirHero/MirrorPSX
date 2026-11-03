<div align="center">

# MirrorPSX

**Bring your screen and videos to a native PS5 receiver.**

Apple screen mirroring · DLNA video · Vulkan interface · Windows preview

[![Platform](https://img.shields.io/badge/platform-PS5%20%7C%20Windows-1668c7?style=flat-square)](#requirements)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599c?style=flat-square)](docs/BUILDING.md)
[![Vulkan](https://img.shields.io/badge/renderer-Vulkan-ac162c?style=flat-square)](#features)
[![Verify](https://github.com/seregonwar/MirrorPSX/actions/workflows/verify.yml/badge.svg)](https://github.com/seregonwar/MirrorPSX/actions/workflows/verify.yml)
[![Status](https://img.shields.io/badge/status-experimental-e8a317?style=flat-square)](#project-status)
[![License](https://img.shields.io/badge/license-GPL--3.0--or--later-3d8b69?style=flat-square)](LICENSE)

[Downloads](https://github.com/seregonwar/MirrorPSX/releases) · [Install](#installation) · [Connect](#connecting-a-device) · [Build](docs/BUILDING.md) · [Report an issue](https://github.com/seregonwar/MirrorPSX/issues)

</div>

---

## Overview

MirrorPSX is a native C++20 receiver for PlayStation 5, built with OpenProspero and a RADV/Vulkan renderer. It receives an Apple device's screen or an unprotected video sent by a compatible DLNA controller. A Windows build provides a local receiver and development preview.

The interface keeps the content in focus: controls appear when you interact, fade during playback, and open settings and connection guides over the video.

> **Experimental:** console launch, external companion elevation, network discovery and incoming Apple pairing requests have been tested on hardware. The latest pairing fixes, successful PS5 video playback and console audio still need device testing. See [project status](#project-status) before installing.

## Features

- **Apple screen reception:** discover the receiver from iPhone, iPad or Mac and pair with the PIN shown on the receiver.
- **DLNA video reception:** send unprotected HTTP(S) video from compatible controllers, including Android apps. Playback controls include pause, seek when available, volume and mute.
- **A controller-first interface:** contextual PlayStation button prompts, an auto-hiding HUD, settings and connection guides.
- **Native rendering:** aspect-preserving video, reusable GPU upload resources and bounded frame/audio queues.
- **One external companion:** the shared OpenProspero broker prepares the PS5 process permissions. Compatible apps can use the same running broker.
- **Two PS5 distributions:** a native PKG and a folder ZIP for ShadowMountPlus, produced from the same validated title files.

## Downloads

Get the files from [GitHub Releases](https://github.com/seregonwar/MirrorPSX/releases). A PS5 release contains:

| File | Purpose |
| --- | --- |
| `MirrorPSX-PS5-vNN.NN.pkg` | Install through a compatible native PS5 PKG workflow. |
| `MirrorPSX-PS5-vNN.NN.zip` | Extract the title folder for ShadowMountPlus. |
| `OpenProspero-SharedBroker.elf` | Required external companion for either format. |
| `OpenProspero-SharedBroker-LICENSE.txt` | License accompanying the companion payload. |
| `INSTALL.txt` | Installation and update instructions. |
| `SHA256SUMS.txt` / `release.json` | File integrity and release metadata. |

Use the app and companion from the same release. The ZIP contains the application, its assets and runtime; it is **not** a standalone payload ELF. No automatic installer or updater is included.

The Release workflow follows MemDBG's official/nightly flow, with optional
version and nightly/draft/prerelease controls. PS5 files retain the native package
version from `PS5_VERSION`; public release tags use `VERSION` or the selected
version. Releases also include `MirrorPSX-Windows-preview.zip`.

## Requirements

**PS5**

- A homebrew-enabled PS5 with a compatible native application environment and ELF loader.
- The matching **external OpenProspero companion**, loaded once per console boot.
- A compatible PKG installation workflow, or **[ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus)** for the folder distribution.
- The sender and receiver on the same local network, with multicast discovery available.

**Windows**

- Windows with Vulkan-capable graphics and the bundled application dependencies, or the [development environment](docs/BUILDING.md).
- Access through Windows Firewall on the local network. The PS5 companion is not needed on Windows.

MirrorPSX does not currently publish a tested firmware compatibility matrix.

## Installation

### 1. Load the external companion

Load `OpenProspero-SharedBroker.elf` using your console's ELF loader. It stays separate from the application and can serve multiple compatible apps. MirrorPSX neither starts another instance nor stops the shared service when it closes.

If a different companion is already running, check its configuration before replacing it. Sharing the protocol alone is not enough: the client and broker must use the same configuration. See [Shared companion](docs/SHARED_BROKER.md).

### 2. Choose one application format

**PKG**

1. Install `MirrorPSX-PS5-vNN.NN.pkg` through your compatible PKG workflow.
2. Open **MirrorPSX** from the home screen.
3. Wait for the receiver to become available.

**Folder ZIP / ShadowMountPlus**

1. Extract `MirrorPSX-PS5-vNN.NN.zip` on your computer.
2. Copy the complete `PPSA99221` directory to `/data/homebrew/PPSA99221/` on the console.
3. Load ShadowMountPlus and let it register/mount the title according to its instructions.
4. Open **MirrorPSX** from the home screen.

The directory must contain `eboot.bin`, `sce_sys/`, `sce_module/` and `assets/` directly. Avoid an extra nested `PPSA99221` folder. Folder launch compatibility still needs a real console test.

**Use one installation of title `PPSA99221` at a time.** Do not leave both the PKG and folder copy active. Close the app before updating, preserve `/data/mirrorpsx/`, and reload the companion after a console reboot.

## Connecting a device

### iPhone, iPad and Mac

1. Select Apple screen reception in MirrorPSX.
2. Open **Control Center → Screen Mirroring** on the sending device.
3. Select **MirrorPSX - PS5**, or **MirrorPSX - PC** for the Windows receiver.
4. Enter the PIN shown in MirrorPSX when prompted on the sender.

### Android and other DLNA controllers

1. Select **Video Android** in MirrorPSX.
2. Open a compatible DLNA controller on the same network.
3. Select **MirrorPSX - Video** and send an unprotected video.

This mode receives video content; it does not mirror the Android screen. YouTube/Netflix's native Cast buttons and DRM-protected playback are unsupported. MirrorPSX does not implement Google Cast or Miracast.

## Controls

| Action | DualSense | Keyboard |
| --- | --- | --- |
| Navigate | D-pad / left stick | Arrow keys |
| Select | Cross | Enter |
| Back / close overlay | Circle | Escape |
| Settings | Options | I |
| Connection guide | Triangle | F1 |
| Hide the video HUD | Square | F |

Interaction reveals the HUD again. Settings include reduced motion and a confirmation before closing the app.

## Project status

| Area | Current evidence |
| --- | --- |
| PS5 app launch, Vulkan UI and assets | Confirmed in console runs. |
| External broker, native random source and discovery | Confirmed in console logs. |
| Apple pairing | Device requests reach the console. Retry/proof fixes pass the independent desktop protocol test; current device interoperability needs retesting. |
| PS5 screen video and HDMI audio | Not yet confirmed. |
| DLNA | Desktop discovery/control/playback tests exist. Real Android and PS5 playback remain to be verified. |
| Folder ZIP | Packaging and content checks; launch through ShadowMountPlus is not yet verified. |

Apple mirroring audio output is not implemented. Subtitles, playlists and next-track URI are not implemented. Test results and source-level checks do not substitute for a device run.

## Troubleshooting

- **Receiver unavailable:** start the matching companion and choose **Riprova / Retry**. Keep the companion running.
- **Receiver missing on the sender:** confirm the same LAN, disable guest-network isolation and check multicast/firewall access. If discovery is stale, reconnect the sender's Wi-Fi.
- **PIN rejected or connection closes:** report the app version, sender OS and the corresponding receiver log. Do not share the PIN, pairing keys or broker configuration.
- **ZIP does not appear on the home screen:** check `/data/homebrew/PPSA99221/`, the complete folder structure and ShadowMountPlus's logs. Ensure another installation of the same title is not active.

Use the [issue forms](https://github.com/seregonwar/MirrorPSX/issues/new/choose)
to report a bug, request a feature or share a real-device compatibility test.
Include the release, console firmware, installation format, sender model/OS,
steps to reproduce and relevant `[MirrorPSX]`, `[AirPlay]` or `[mDNS]` log lines.

## Development

- [Build and test](docs/BUILDING.md)
- [Create a PKG + ZIP release](docs/RELEASING.md)
- [Private SDK checkout and CI setup](docs/CI.md)
- [Shared companion integration](docs/SHARED_BROKER.md)
- [Console validation history](docs/CONSOLE_TESTING.md)

## Credits and license

MirrorPSX uses OpenProspero, RADV/Mesa, Dear ImGui, UxPlay, FFmpeg, OpenSSL and libxml2. Its UI includes artwork from `ps5-homebrew-ui`, Inter fonts, Lucide icons and Kenney input prompts.

The project is licensed under **GPL-3.0-or-later**. Dependencies retain their respective licenses and notices. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
