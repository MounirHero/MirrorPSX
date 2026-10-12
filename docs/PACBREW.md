# PacBrew decoder bring-up on OpenProspero

The first MirrorPSX native PS5 PKG renders the genuine Vulkan/ImGui UI without
starting a receiver. The next independent dependency gate is a **link-only**
H.264/HEVC decoding probe, using prebuilt libraries from the upstream
[PacBrew package repository](https://github.com/ps5-payload-dev/pacbrew-repo).

## Pinned third-party build

- Release: `v0.40.2`
- Asset: `ps5-payload-dev.tar.gz`
- SHA-256:
  `a85f65de418a8e6a898c6c3e3c870d50fff7618a200e4dd59ea9692af6ecec4d`
- Prefix **inside** archive: `opt/ps5-payload-sdk/target/user/homebrew/`
- The extraction target must stay in a separate ignored build directory.
  **Never** overwrite OpenProspero's own `sdk/target` with the PacBrew image.

On the Windows development machine, the downloaded asset was hash-verified
and the selected header directories and static archives were extracted under
`build-pacbrew/selected/` (ignored by Git). Only the following libraries were
required for the first codec link: `avcodec`, `swscale`, `avutil`,
`swresample`, `x264`, `lzma`, `iconv`, `ssl`, `crypto` and `z`, plus
the already-used Hyperion PS5 platform/builtins archives.

## Reproducible link-only probe

```powershell
$ports = 'build-pacbrew/selected/opt/ps5-payload-sdk/target/user/homebrew'
./scripts/build-pacbrew-codec-probe.ps1 -PortsRoot $ports -Probe
./scripts/build-pacbrew-codec-probe.ps1 -PortsRoot $ports `
  -OutputRoot build-ps5-codec-probe-release
```

This script cross-compiles the real `src/receiver/decoder.cpp` and
`tests/ps5_codec_probe.cpp`, and links an OpenProspero application
`codec-probe.elf`. The entry point attempts to open H.264 and HEVC decoders
sequentially. Successful compilation and linking **do not prove** either
decoder opens, receives frames or renders video on a physical PS5.

The first link exposed differences between the PacBrew payload SDK and the
OpenProspero application ABI: the FreeBSD `___mb_cur_max` data symbol and a
missing `arc4random_buf` mapping. The independent probe currently supplies
a **test-only** constant multibyte-width symbol and binds
`arc4random_buf` to the existing `ps5_arc4random_buf` Hyperion platform
implementation. This is deliberately marked experimental; it cannot be used
as evidence of runtime ABI compatibility and **must not be silently added to
the MirrorPSX production PKG**.

The probe uses the upstream static archives with their original licenses,
which must be audited before distribution in an AirPlay-enabled title.

## Remaining dependencies / actual console gates

The `airplay-exp-08-license-audit` packaging path now copies unmodified
license text files from the existing UxPlay, llhttp, playfair and pinned
libplist source checkouts into `assets/licenses/dependency-*`. The PKG
manifest records the SHA-256 of each bundled text, and five copied texts
were verified against their manifest hashes. Font licenses remain packaged
separately. **This is not a complete redistribution audit:** PacBrew's
FFmpeg/x264, OpenSSL, libiconv and other statically linked components still
need their exact source revisions, build configurations, attribution and
corresponding license obligations reviewed before any public release.
In particular, x264 and GPL components can impose source-availability
requirements. A structurally valid PKG is not a license clearance.


UxPlay requires `libplist` as well as FFmpeg/OpenSSL and DNS-SD/Bonjour.
The examined PacBrew image contains the FFmpeg and OpenSSL archives but does
**not** contain a libplist archive. Upstream `libplist 2.7.0` was subsequently
cross-compiled as 16 C units for PS5 using isolated PacBrew headers. The new
`build-ps5-libplist.ps1` and `build-ps5-airplay-probe.ps1` scripts reproduce
this work, compiling 30 UxPlay C units and linking with OpenSSL and libplist.
These are successful **link tests, not runtime tests**.

`build-ps5-airplay-experimental.ps1` now combines UxPlay, the FFmpeg decoder
and MirrorPSX's Vulkan host in a **separate MPSX00002 experimental PKG**.
It starts/stops the receiver through the real UI action; pairing identity is
ephemeral until a stable writable console location is tested. The experimental
PacBrew ABI workaround is never added to the reference UI-only PKG.

Security correction for the `airplay-exp-06-secure-pairing` build: UxPlay's
previous empty-keyfile mode derived a reproducible Ed25519 private key from a
public device ID. MirrorPSX now generates an unpredictable in-memory key in
that case. A new Windows `ephemeral_pairing` unit test confirms distinct
public keys across repeated initializations using the same device ID. PS5
OpenSSL RNG and key generation are **not** yet hardware-verified. A private
savedata-backed identity should replace ephemeral keys once its location and
permissions have been established on the console.

Verify the codec probe on authorized PS5 hardware before linking it into the
full UI title. The application sandbox's access to mDNS multicast and RTSP/
RTP sockets remains unverified. Introduce an OpenProspero companion broker
only for operations demonstrated to require capabilities outside the sandbox,
using the SDK's established companion protocol. Add actual first-frame,
H.264/HEVC decoding, reconnect and latency tests before advertising AirPlay
as available on PS5.
