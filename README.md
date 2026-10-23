# MirrorPSX

Native C++20 receiver with a Vulkan UI and Windows preview.

## Desktop

From the repository root, run `tools/build-desktop.ps1 -Run`.
This builds the app, bundles its runtime, runs decoder/UI/receiver checks and opens
the real desktop receiver. Use `-BuildDirectory build/desktop-live` to select a
separate output directory when another preview is already open.
Assets resolve relative to the working directory; include the complete `assets/ui`
folder when packaging the application.

The receiver allocates available ports instead of assuming 7100-7104 are free:
Windows may reserve that range. DNS-SD advertises the actual allocated port.
On this PC the live receiver is named **MirrorPSX - PC**. Pairing PINs remain on
screen across the short control-connection transition and clear when video starts
or the receiver stops. Pairing keys are local ignored files.

`tools/probe-discovery.py <LAN-IP>` checks a local multicast DNS-SD reply and its
advertised address/port. This verifies local publication, not reachability from an
iPad. The iPad must discover the receiver and complete pairing for that check.

## Interface

- **Main screen**: receiver name and a direct connection instruction while waiting;
  aspect-preserving video uses the entire surface when connected.
- **HUD**: receiver control at lower left, guide at lower right, settings at upper
  right and video-only control at lower center. It appears on mouse/keyboard/pad
  interaction and fades after four seconds during video. The first confirmation
  input while hidden reveals controls without triggering a receiver action.
- **Guide overlay**: separate instructions for iPhone/iPad and Mac.
- **Settings overlay**: receiver pause/start, reduced motion and confirmed closure.
- Arrow keys/stick/D-pad navigate; Enter/cross selects; Escape/circle goes back.
- I/Options opens settings; F1/triangle opens the guide.
- F/square immediately hides the HUD during video. Interaction reveals it again.
- Escape/circle closes overlays. PIN entry appears only when requested.

## Android video (desktop DLNA)

Choose **Video Android** on the waiting screen or switch modes in Settings.
The PC advertises **MirrorPSX - Video** as a UPnP AV MediaRenderer. Use a DLNA
controller on the same LAN to send a non-protected video (for example, a local
MP4). `MirrorPSX.exe --dlna` starts directly in this mode.

The desktop receiver handles SSDP discovery, device/service descriptions,
AVTransport, RenderingControl, ConnectionManager and event subscriptions.
Supported controls include play, pause, stop, seek when the source permits it,
volume and mute. HTTP/HTTPS media is demuxed with FFmpeg; audio plays through SDL.
The video HUD's lower-left button pauses/resumes the content. Settings can stop
the receiver or return to Apple screen mirroring; modes do not share a session.

This is **DLNA**, not a Google Cast or Miracast receiver. It does not make the
native Cast buttons in YouTube/Netflix work and does not implement protected
service playback/DRM. Android screen mirroring is not implemented. Subtitles,
playlists and next-track URI are not implemented. This implementation is desktop
and PS5; console networking and audio require verification on hardware.

## Performance and verification

- The renderer reads an immutable latest-frame mailbox without copying RGBA under
  the state lock. It keeps no growing video queue.
- Upload buffer, mapped memory and command buffer are reused. A fence prevents
  reuse while busy; the renderer picks the latest frame when ready. Full GPU waits
  are limited to resource recreation/disconnect/shutdown, not every frame.
- Shared conversion preserves limited/full range and color matrix metadata.
  YUVJ plane layouts are normalized with explicit range; conversion buffers have
  padding for SIMD writes. Tiny-frame, color and concurrency regressions are tested.
- Apple mirroring uses slice threading to avoid buffering future frames. This
  prioritizes first-frame latency over maximum offline decoding throughput.
- DLNA I/O can be interrupted and has timeouts. Requests, subscribers and audio
  backlog have explicit bounds (64 KiB body, 16 subscribers, 0.5 s audio queue).
- `--gpu-smoke` uses synthetic frames and exits after resolution/portrait and
  disconnect/reconnect transitions. It does not start a receiver.
- `decoder_benchmark.exe <H264-file>` measures decoding/conversion throughput,
  not end-to-end network latency. No percentage improvement is assumed.

Build dependencies add libavformat, libswresample, SDL2 and libxml2. When FFmpeg's
executable is available, the build generates a local MP4/audio test fixture and
runs the real SSDP/SOAP/playback integration test (audio uses SDL's dummy driver).
Speaker output and Android controller interoperability require a device run.

The visual design reuses ps5-homebrew-ui's original artwork, Inter fonts, safe-area
guidance and contextual controller hints. Actual PlayStation-style PNG prompts are
from Kenney Input Prompts. Sources and licenses are in `THIRD_PARTY_NOTICES.md`.
No external asset downloads are needed at runtime.

## Isolated UI preview

Run `build/desktop/MirrorPSX.exe --ui-preview ready` from the repo root.
Other states: `paused`, `pin`, `error`, `video`, `portrait`.
These use local sample state and a synthetic video gradient, do not start network
services, and show **Anteprima UI** in the window title. They verify presentation,
not receiver interoperability or console behavior.

The PS5 does not announce a receiver until an application is installed and running.
The PS5 build uses OpenProspero's application profile and static RADV. It includes
DualSense navigation, a 1080p/60 display preference, Apple screen reception and
DLNA video playback with a bounded native PCM audio worker. Pairing keys are kept
in `/data/mirrorpsx/pairing.key`, outside the package. Shutdown asks the shell to
close the application and never returns to the CRT's exit path.

First build the shared external broker with `tools/build-shared-broker.ps1`.
Its ELF is loaded separately on the console, once per boot; see
[shared broker integration](docs/SHARED_BROKER.md). Then build on Debian WSL with LLVM 19: `wsl -d Debian bash tools/build-ps5.sh`.
`OPENPROSPERO_SDK` defaults to `../sdk/sdk`. Generate the Windows-hosted package
with `tools/package-ps5.ps1`; its result is `dist/MirrorPSX-PS5-test-r10.pkg`, test
identity `PPSA99221`. Keep `build/ps5/MirrorPSX.elf` for crash symbolization.
Packaging includes `sce_module/libc.prx`, reproduced from the reference project's
clean-room runtime sources by `tools/build-ps5-runtime.sh`. Its checksum must
match the recorded reference digest. The final PKG is extracted again to verify
the runtime's presence and checksum and the signed executable's exact bytes.
The RADV platform library is rebuilt at Hyperion's immutable upstream pin;
Hyperion itself is not modified. Console assets use absolute `/app0/assets/ui/`
paths. Startup checkpoints, error messages and captured driver stderr reach klog.
The revision marker is `Build R10: pairing retries and prompt framing`. This distinguishes
the new executable from a previously installed package during a hardware test.
The R3 console log confirms Vulkan instance/device, display surface, swapchain
and UI atlas initialization. Its pairing-key generation then trapped in OpenSSL's
FreeBSD sysctl entropy path. R4 then trapped at its statically linked RNG stub.
The R5 hardware run confirms the UI appears and stays active. All native RNG
module paths return 0x80020002 (not found). R6 first reads FreeBSD KERN_ARND
through the same validated libkernel syscall gate used for identity queries,
avoiding the unresolved sysctl import. It checks both result and complete byte
count; a short read or any failure disables the RNG and clears requested bytes.
A resolved SceRandom service remains an alternative if the kernel query fails.
No timestamp or deterministic fallback is used. Host tests exercise both entropy
sources, failed/short reads, actual Ed25519 key generation and 64-byte request
bounds. A separate SysV assembly test verifies all six syscall argument registers.
The bridge uses OpenSSL 3.5's deprecated RAND_METHOD API without changing OpenSSL.
The R6 hardware log confirms a successful kernel entropy probe and initialized
server sockets. Receiver startup then fails at mDNS registration. R7 adds
native sockaddr lengths and a unicast-route fallback for selecting the local
IPv4 address, plus logs for IPv4 socket creation, UDP/5353 bind, multicast
membership and responder thread startup. Pairing, discovery, received video
and HDMI audio remain unverified on the console.

The package uses OpenProspero's experimental native writer. Structural validation
does not establish install/launch compatibility. UI rendering is confirmed by the R5 hardware run; controller, networking,
HDMI audio and shell shutdown still need functional hardware tests.
For the first test, install and launch MirrorPSX, then select **MirrorPSX - PS5**
in the iPad's **Duplica schermo**. The **Video Android** mode exposes
**MirrorPSX - Video** for unprotected HTTP(S) content from DLNA controllers.
The native Cast buttons in YouTube/Netflix remain unsupported.

R8 connects to the shared external OpenProspero privileged broker. No broker
payload is packaged or autostarted by the title. Both Apple and DLNA startup
require a confirmed promotion of this process, after UI assets have been read.
The R7 hardware log isolated mDNS startup failure to UDP/5353 bind EACCES.
The shared-broker client and payload are compiled; promotion and subsequent
network operation are not yet verified on the console.

The R8 hardware log confirms broker promotion, persistent pairing-key creation,
DNS-SD registration and an incoming IPv6 iPad connection. It then crashes in
raop_ntp_start with a null timing object. The native platform resolver supports
IPv4 only; R9 parses numeric IPv4/IPv6 peers directly, preserving link-local
scope IDs, and rejects failed timing initialization instead of dereferencing
null. Receiving/decoding/presenting the iPad video is still unverified on PS5.

R10 fixes the PIN challenge being consumed at the first SRP request while still
visible in the UI. It remains valid across failed proof/reconnect attempts until
a fresh PIN-start or successful final confirmation. The Apple SHA-1 SRP proof
is 20 bytes: copying 64 bytes from the peer and server proof was an out-of-bounds
read; R10 validates and copies the actual proof size. PIN-start responses carry
Content-Length: 0 and accepted control sockets enable TCP_NODELAY. An independent
Python SRP client checks the actual RTSP server: correct PIN, wrong PIN rejection,
retry and server proof validation. Loopback PIN-start measured 16 ms on Windows;
this does not measure the iPad/macOS dialog or establish console behavior.
