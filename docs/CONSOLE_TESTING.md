# Console validation history

This record distinguishes observations from implementation and desktop tests.
It does not establish support for untested firmware or sender combinations.

| Build | Evidence from console logs / device feedback |
| --- | --- |
| R1–R2 | Runtime/launch integration failures during early packaging. |
| R3 | Vulkan instance/device, surface, swapchain and UI assets initialized; OpenSSL's unresolved sysctl entropy import trapped during pairing-key generation. |
| R4 | A directly linked native RNG stub trapped; a linkable import was not proof of a runtime service. |
| R5 | The UI appeared; all attempted SceRandom module paths returned file-not-found. |
| R6 | KERN_ARND probe succeeded and receiver sockets initialized; mDNS startup failed. |
| R7 | IPv4 route resolved to the console; binding UDP/5353 failed with EACCES in the unprepared title process. |
| R8 | External broker promotion succeeded. Pairing keys were written outside the title, DNS-SD registered, and an IPv6 iPad client reached pairing. The subsequent timing setup dereferenced null. |
| R9 | The following device log showed repeated authentication-proof failures. Successful screen video was not confirmed. |
| R10 | Desktop independent SRP tests verify correct PIN, incorrect PIN rejection, retry across connections and the server proof. No subsequent successful console streaming run is recorded in this project history. |

The R9 portability fix parses numeric IPv6 peers directly, including numeric
scope IDs, because the platform resolver supports IPv4 only. Failed timing
initialization returns an error rather than dereferencing a null object.
The R10 pairing changes keep the visible PIN available through retries, correct
proof-copy lengths, explicitly frame the empty PIN-start response and enable
TCP_NODELAY for control sockets. The recorded ~16 ms is a desktop loopback
response measurement, not an iPad/macOS prompt timing or end-to-end latency.

Successful PS5 video, HDMI audio, current Apple pairing interoperability,
real Android DLNA playback and ShadowMountPlus folder launch need separate
hardware confirmation. Apple mirroring audio output is not implemented.

For a new test, record the release/build marker, console firmware, installation
format, sender model/OS, visible outcome and relevant logs. Redact pairing PINs,
keys and companion configuration. Avoid inferring compatibility from a successful
package checksum or a desktop test.
