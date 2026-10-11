# PS5 deployment and runtime evidence

## Latest measured state (2026-10-11)

### Updated isolated packages (after host and receiver fixes)

The original baseline below remains in the logs. Two **new** PKGs were
built and staged independently without touching installed titles:

| Profile | Local output | SHA-256 | Runtime |
| --- | --- | --- | --- |
| UI-only (`MPSX00001`) | `build-ps5-package/runtime-02/pkg/` | `E35310E7C1412BE23876BB73C88EFFC0C796F9F80D8ADA9094417FC4C923745A` | Not installed/launched |
| Experimental AirPlay (`MPSX00002`) | `build-ps5-package/airplay-exp-04/pkg/` | `D45DC948B6318AAE612CF1E12426286342F7A11D3E5AFE34151F5EB294747E17` | Not installed/launched |

Both passed OpenProspero PKG structural validation with zero errors/warnings,
were transferred to distinct SHA256-named directories under
`/data/mirrorpsx-staging/`, and were fetched back over FTP with matching
SHA256. The FTP server exposes no trusted remote title-installer command.
The first physical-console checkpoint is still installation of the UI-only
title using the console's existing package manager. The AirPlay title is a
separate, explicitly experimental follow-up.

MirrorPSX has a cross-compiled native PS5 UI host and an OpenProspero PKG
that passes **structural** validation. The authorized console (private LAN
host selected through the command's `-Console` argument)
responded on FTP `2121` (banner: `ftpsrv.elf 1.16-ng-stable`, built
2026-09-20), on elfldr `9021`, and on klogsrv `3232`. Hyperion's former
FTP `2120` endpoint was not listening.

The tested FTP `HELP` and `FEAT` responses advertise file operations,
including `STOR`, `RETR`, `MLST` and `RNFR/RNTO`, but **no documented
package-install command**. elfldr `9021` accepts native payload ELF files,
not title PKGs. Hyperion's known working workflow installs desktop titles
through the console's existing homebrew package installer; the repository
does not contain a verified remote PKG-installer protocol. No installer
payload has been launched in this session.

The MirrorPSX UI package was uploaded through FTP only to:

```text
/data/mirrorpsx-staging/MPSX00001-07D6B1BE9140EF0A/
  UP9000-MPSX00001_00-MIRRORPSXUI00001-A0100-V0100.pkg
```

The upload was streamed to a new temporary file, read back over FTP, compared
against the local SHA-256, renamed within the new directory and size-checked.
The **stage is verified**, but this does not install, sign, promote or execute
the title. `MLST /user/app/MPSX00001` reported *not visible*; FTP's view
alone cannot establish whether the title is installed elsewhere.

| Item | Value |
| --- | --- |
| Content ID | `UP9000-MPSX00001_00-MIRRORPSXUI00001` |
| Package size | `23,754,182` bytes |
| PKG SHA-256 | `07D6B1BE9140EF0A392800BE8F5E496891E075761B5BA9EB1906D4C9EC6BD90F` |
| Structural validation | OpenProspero: 0 errors, 0 warnings |
| Remote upload/readback | PASS, matching SHA-256 |
| PS5 title install | Not attempted; trusted installer interface unavailable |
| PS5 launch / first frame / DualSense | Not yet verified |
| AirPlay receive / pairing | Not included in current PS5 UI milestone |

Local deployment evidence is stored under
`build-ps5-package/deployment-evidence/mirrorpsx-20261011T181645Z-e8eb0529/staging.json`.
An existing Hyperion continuous log collector was already active.
The latest three available klog file tails were inspected using an isolated
reader and contained **zero MirrorPSX lines**. The filtered result is under
`build-ps5-package/deployment-evidence/mirrorpsx-20261011T181904Z-02c0eb1e/`.
This is a baseline before console launch, not proof the application starts or
that `stderr` would appear on klogsrv.

## Targeted deployment and log collection

From the `.mirrorpsx-work` root, run:

```powershell
# Read-only FTP identity, feature and title-path inspection
.\scripts\deploy-ps5.ps1 -Console '<PS5_HOST>' -Probe

# Upload to a fresh /data/mirrorpsx-staging/MPSX00001-<sha256-prefix>
# and verify the remote file byte-for-byte via SHA-256 readback.
.\scripts\deploy-ps5.ps1 -Console '<PS5_HOST>' -Stage -PackagePath `
    '.\build-ps5-package\runtime-02\pkg\UP9000-MPSX00001_00-MIRRORPSXUI00001-A0100-V0100.pkg'

# Inspect an already running log archiver without opening another klog socket.
.\scripts\deploy-ps5.ps1 -Console '<PS5_HOST>' -Archive -ArchiveRoot '<EXISTING_LOG_ARCHIVE>'

# When there is no other klogsrv reader, collect a bounded live session.
.\scripts\deploy-ps5.ps1 -Console '<PS5_HOST>' -Capture -CaptureSeconds 60
```

The script restricts uploads to the dedicated `MPSX00001` UI-only and
`MPSX00002` experimental AirPlay title IDs and separate checksum-named
directories under `/data/mirrorpsx-staging`. It refuses
an existing destination rather than replacing files. It checks the package
filename, content ID and SHA-256 against the release manifest. It does not
write under `/user/app`, change other applications, send payloads to elfldr,
or invoke unknown FTP `SITE`/administrative commands.

When the console's **already trusted and installed** package manager offers
a way to select this staged PKG, confirm the target title ID `MPSX00001`
and version `01.00` before installing. If that manager supports USB only,
use its established USB workflow and verify the same PKG hash beforehand.
Do not replace an existing installation of this title without inspecting the
console's installed version. After installing, launch MirrorPSX normally from
the console UI. Report whether the splash screen clears, Vulkan draws a frame,
the layout is centered at the television's active resolution, D-pad and Cross
move/confirm focus, and holding OPTIONS for 1.2 seconds exits gracefully.

Hyperion's long-running `capture-console-logs.py` recorder already reads
`3232` as its sole client and receives UDP logs on 9998/9999. A second klog
reader risks consuming messages intended for that recorder. `-Capture` refuses
to compete with the existing process; use `-Archive` on its output instead.
An archive import scans the tails of the latest three `klog-*.log` files
and saves only lines mentioning MirrorPSX. Live capture, when safe, writes
`klog-raw.log`, a filtered `mirrorpsx-filtered.log` and `capture.json`.
Raw kernel logs may contain unrelated applications' messages; handle them as
private diagnostic material. This PS5 UI host currently uses `fprintf(stderr)`
for its markers and has **no verified dedicated UDP log transport**.

Expected native host checkpoints in current source include
`MirrorPSX PS5: starting Vulkan UI-only milestone`,
`MirrorPSX PS5: display <width>x<height>`,
`MirrorPSX PS5: scePad open result <code>`,
`MirrorPSX PS5: first Vulkan frame presented`, and
`MirrorPSX PS5: exit requested by OPTIONS hold`.
Missing markers can mean the log transport does not carry process `stderr`;
do not automatically infer an earlier startup failure.

## Native Vulkan and controller audit

Initial review performed against `src/platform/ps5_main.cpp` (2026-10-11).
The following were source-level risks, **not confirmed console crashes**:

1. **Swapchain lifecycle** (`Ps5Vulkan::present`, roughly lines 177-227).
   `VK_ERROR_OUT_OF_DATE_KHR` from image acquisition immediately terminates
   the render loop, and `VK_SUBOPTIMAL_KHR` is accepted without requesting a
   rebuild. Presentation errors also terminate without recreating the
   swapchain. A TV mode switch, suspend/resume, or presentation change can
   therefore shut the app down. Recreate surface-dependent targets with
   `vkDeviceWaitIdle`, reset the ImGui helper and resume only after checking
   new dimensions.
2. **Unvalidated display queries** (`create_surface`, lines 46-121).
   Calls to `vkGetPhysicalDeviceDisplayPropertiesKHR`,
   `vkGetPhysicalDeviceDisplayPlanePropertiesKHR`,
   `vkGetDisplayModePropertiesKHR`, `vkGetDisplayPlaneSupportedDisplaysKHR`
   and `vkGetDisplayPlaneCapabilitiesKHR` ignore `VkResult`; some function
   pointers depend on supported extensions. Check instance extension
   availability, each result, resource counts and mode/plane support before
   dereferencing or selecting a candidate; log the rejected candidate.
3. **Partial initialization failure** (`start` and `shutdown`, lines
   123-175 and 229-236). `initialized` is marked after the ImGui window
   helper creates the swapchain. A partial helper failure can leave Vulkan
   frame/window allocations undisposed because cleanup is guarded by
   `initialized`. Track individual resource ownership and always destroy
   whichever resources were actually created. The ImGui helper contains
   assertions that can abort without producing a useful PS5 log; validate
   device and surface capabilities before invoking it.
4. **Controller recovery** (roughly lines 298-335). Opening `scePad`
   is attempted once. Subsequent `read` return values are ignored. A failed
   first open or a disconnect could remove the only reliable exit gesture.
   Retry a disconnected controller, log transitions and retain the console's
   normal application-close path.
5. **Observability** (`main`, lines 253-368). Logs are sparse between
   RADV loader initialization, instance creation, swapchain creation,
   ImGui setup and first presentation. Add bounded stage markers plus
   selected `VkResult` and GPU/queue IDs through a verified console log
   transport. A successful `vkQueuePresentKHR`/first-frame marker establishes
   submission, not actual image visibility on the television.

The latest host now requests swapchain recreation for OUT_OF_DATE/SUBOPTIMAL
results, checks individual display enumeration calls, handles partially
initialized Vulkan resources and retries DualSense after failed reads.
These improvements are compiled and statically validated, not console-tested.

The `MPSX00001` UI-only package intentionally stays disconnected from AirPlay.
An independent `MPSX00002` experimental package includes UxPlay, FFmpeg,
OpenSSL, libplist and native video textures with the unchanged Vulkan UI.
Neither title has been installed or shown a frame on hardware. Test
`MPSX00001` first; the network sandbox/codec ABI for `MPSX00002` is unproven.
