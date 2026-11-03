# Third-party notices

This document records dependency licenses, asset attribution and local portability changes.
Original notices in vendored sources and license files remain authoritative.

## UI assets

- `assets/ui/backdrop.png`: original `sce_sys/background-source.png` from
  ps5-homebrew-ui, Copyright (C) 2026 BlackBearReloaded, GPL-3.0-or-later.
  Reused with the owner's authorization. The reference artwork is unchanged;
  the atlas contains a resized copy. Full license: `assets/ui/BACKDROP-LICENSE.txt`.
  Source: `reference/ps5-homebrew-ui`.
- `assets/ui/fonts/Inter-*.ttf`: Inter, SIL Open Font License 1.1, copied from
  ps5-homebrew-ui. Full license: `assets/ui/fonts/Inter-LICENSE.txt`.
- `assets/ui/prompts/*.png`: Kenney Input Prompts 1.5A, CC0. Original PNGs
  from the PlayStation Series/Default directory, with shortened filenames.
  Full license: `assets/ui/prompts/LICENSE.txt`.
  Source: https://kenney.nl/assets/input-prompts
- `assets/ui/icons/*.svg`: Lucide Icons, ISC license, with the included MIT
  notice for icons derived from Feather. Original SVGs are unchanged;
  `circle-help.svg` is the upstream `circle-question-mark.svg` with a shorter
  filename. Atlas rasterization sets the stroke to white.
  Full license: `assets/ui/icons/LICENSE.txt`.
  Source: https://github.com/lucide-icons/lucide

`atlas.png` and `atlas.rgba` are packed/resized derivatives of the assets above.
Rebuild them with `python tools/pack-ui-assets.py` (Pillow and CairoSVG required).
The packed backdrop is resized and blurred for an unobtrusive waiting screen.
Controller prompts are third-party assets, not a claim of Sony endorsement.

## Desktop media runtime

UxPlay's internal `lib/mdnsd` retains its LGPL-2.1-or-later notices. Local
Windows discovery portability fixes give its selected LAN address a distinct
hostname so it does not overwrite Bonjour's machine-wide address records, and
retry startup announcements at 1, 2 and 4 second intervals (RFC 6762 section 8.3).
There are no continuous periodic mDNS announcements. The existing adapter filter
selects an active gateway interface instead of Hyper-V/WSL adapters.

- FFmpeg libavformat/libavcodec/libavutil/libswscale/libswresample:
  https://ffmpeg.org/legal.html. Runtime licensing depends on the installed build
  configuration (LGPL/GPL); preserve its corresponding notices and sources when
  distributing bundled DLLs.
- SDL2: zlib license, https://github.com/libsdl-org/SDL/blob/SDL2/LICENSE.txt.
- libxml2: MIT license, https://gitlab.gnome.org/GNOME/libxml2/-/blob/master/Copyright.

These libraries are supplied by the local MinGW environment. No third-party
sources were patched for the desktop DLNA media feature; the UxPlay
interoperability changes below are documented separately. DLNA/UPnP support here is an
implementation of UPnP AV interfaces, not a certification claim.

## PS5 build

The console build consumes OpenProspero, static RADV/Mesa, OpenSSL 3.5.2,
libxml2 2.14.5 and the pinned FFmpeg reference sources. Upstream source trees and
the shared SDK archives are unchanged. `tools/build-ps5-deps.sh` builds a local
copy of the SDK's C compatibility archive without `gmtime.o`, whose `gmtime_r`
duplicates the application CRT. Optional weak dynamic-loader hooks and
`kernel_mprotect` resolve to null, retaining missing-weak ELF semantics; static
RADV does not require a runtime Vulkan loader.

The RADV platform layer consumes the same immutable PS5_PayloadSDK revision as
Hyperion: `95c08f27386fc698f6bbe21dde3030140a41d10b`, from
https://github.com/mihawk-99/PS5_PayloadSDK. Its platform library retains its
GPL-3.0-or-later notices, musl regex's MIT notice and dlmalloc's MIT notice.
It supplies the console thread/stack wrappers, memory streams, locale, directory
and regex ABI used by RADV. LLVM 19 compiler-rt is Apache-2.0 with LLVM exception.
The native identity gate is adapted from Hyperion (GPL-3.0-or-later); it queries
libkernel instead of relying on nullable console identity imports. Hyperion's
source and build are read-only inputs; no files in that checkout are modified.

The controller sample ABI follows PS5 Vulkan Template, Copyright (C) 2026 Mihawk,
MIT. Vulkan frame submission follows Dear ImGui's MIT-licensed Vulkan example.
OpenProspero runtime code retains its GPL-3.0-or-later licensing; Mesa is MIT,
OpenSSL Apache-2.0, libxml2 MIT and this FFmpeg configuration LGPL-2.1-or-later.

`sce_module/libc.prx` is the independently authored loader companion from
ps5-homebrew-ui, Copyright (C) 2026 BlackBearReloaded, GPL-3.0-or-later. It is
reproduced from `tooling/native/libc_builder.cpp` and its tracked manifests;
its source and build procedure are in `reference/ps5-homebrew-ui`. It contains
no proprietary console runtime implementation. The exact signed artifact's
SHA-256 is `e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036`.

The native RNG bridge changes no OpenSSL or UxPlay sources. Its PS5 API declaration
follows the public PS5 payload SDK's `samples/hello_sprx/main.c`:
https://github.com/john-tornblom/ps5-payload-sdk/blob/master/samples/hello_sprx/main.c.
It requests at most 64 bytes per native call and fails closed on any service
error. The platform library's timestamp-seeded arc4random helpers are not used
for cryptographic pairing keys.
Module path probing and NID lookup follow OpenProspero's `target/crt/agc_dlfcn.c`.
The RNG export NID `PI7jIZj4pcE` is computed by its `target/crt/nid.c` algorithm.
No system SPRX binary is copied into the package.

The six-argument syscall trampoline follows OpenProspero target/crt/start.S
(GPL-3.0-or-later), using the identity gate validated against the current PID.
The read-only entropy query uses SYS___sysctl=202 and CTL_KERN/KERN_ARND=1/37
from the SDK/FreeBSD headers. KERN_ARND is OpenSSL's BSD entropy route. The R6 hardware log confirms a
successful query in the native PS5 title.
PacBrew's OpenSSL recipe also uses OpenSSL 3.5.2 with BSD-x86_64:
https://github.com/ps5-payload-dev/pacbrew-repo/blob/main/openssl/PKGBUILD.

R7 mDNS portability fix in UxPlay's mdnsd.c preserves its LGPL license: native
PS5 socket addresses populate the FreeBSD length fields. Local IPv4 discovery
can query the default unicast route if the multicast connect is refused, as in
Hyperion desktop/src/desktop_items.cpp network_status (GPL-3.0-or-later).
No datagram is sent by this route query. Socket/membership/thread failures
are logged for hardware diagnosis; multicast permission is not bypassed.

The shared external broker is built without source modifications from
OpenProspero sdk/companion/daemon.c, privileged_ps5.c, the protocol/transport
files and injector sources, using the SDK companion build scripts. The application
links the SDK companion client archive and public headers. These retain their
GPL-3.0-or-later notices and embedded upstream attributions, including the
Hyperion and psxterm adaptations. SDK source is available in ../sdk and its
LICENSE accompanies the separate ELF. No broker payload is embedded in the PKG or the folder ZIP.

R9 documents a UxPlay native portability fix: netutils_parse_address delegates
numeric peers to src/platform/numeric_address.c only on PS5, because the
platform getaddrinfo implementation rejects AF_INET6. No DNS lookup is needed
for the numeric addresses generated from accepted peer sockets. IPv6 scope
IDs are retained. raop_handlers.h checks failed timing initialization and
returns RTSP 500/disconnect rather than dereferencing null; the peer text
buffer also accommodates full IPv6 plus numeric scope. LGPL notices remain.
The host parser test covers the R8 log's actual address, IPv4, global IPv6,
malformed scopes, overflow and undersized output under ASan/UBSan.

R10 UxPlay interoperability fixes preserve LGPL notices: retain the visible PIN
across reconnect/retry until final confirmation; frame the empty PIN-start reply
with Content-Length: 0; enable TCP_NODELAY on control sockets. Apple legacy SRP
uses a SHA-1, 20-byte proof, as documented by the public RAOP-Player protocol:
https://github.com/philippe44/RAOP-Player/blob/master/doc/auth_protocol.html.
The previous fixed 64-byte copies read past both proofs. R10 validates and uses
20 bytes without changing the SRP algorithm or accepting incorrect credentials.
The native setup-failure guard remains. tests/pin_integration.py implements an
independent SRP client for positive, negative and retry interoperability tests.
