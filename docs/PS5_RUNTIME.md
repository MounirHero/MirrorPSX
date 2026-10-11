# PS5 deployment and runtime evidence

## Latest diagnostic packages staged (2026-10-11)

Following the read-only installer audit, both newly compiled packages were
uploaded into **new, hash-specific directories**, fetched back over FTP and
checked against the original SHA-256. Neither was installed or launched.

| Profile | Local PKG | Remote FTP directory | SHA-256 |
| --- | --- | --- | --- |
| UI-only diagnostic (`MPSX00001`) | `build-ps5-package/runtime-03-diagnostic/pkg/UP9000-MPSX00001_00-MIRRORPSXUI00001-A0100-V0100.pkg` | `/data/mirrorpsx-staging/MPSX00001-08E6DBE5FC4FD803/` | `08E6DBE5FC4FD8037490131FFA4A2FE81D7F75545B9DC4A4072F5999A9FD1AEA` |
| Secure ephemeral-pairing AirPlay (`MPSX00002`) | `build-ps5-package/airplay-exp-06-secure-pairing/pkg/UP9000-MPSX00002_00-MIRRORPSXAIR0001-A0100-V0100.pkg` | `/data/mirrorpsx-staging/MPSX00002-1BDF0E4BEEF74D92/` | `1BDF0E4BEEF74D92BCE60FFDA39FF2293283A87F36CCF5843B382B7000901BFB` |

Transfer evidence: `build-ps5-package/deployment-evidence/mirrorpsx-20261011T190131Z-60fbbf61/`
and `mirrorpsx-20261011T190133Z-8af8c73f/` within the same evidence root.
UI-only should be installed first, **only** through a package manager already
trusted on the console. If it cannot browse `/data/mirrorpsx-staging/`, use its
normal USB install procedure with the locally hash-verified file.

## Verified remote installer investigation (2026-10-11)

The authorized console (configured through `-Console` or
`$env:MIRRORPSX_PS5_HOST`) exposes the existing
`ftpsrv.elf 1.16-ng-stable` FTP service on TCP `2121`, elfldr on `9021`,
and klog on `3232`. The installed FTP service has filesystem commands
but **no package installation command**. Hyperion's existing scripts do not
implement a remotely callable trusted PKG installer; the documented Hyperion
procedure relies on a package manager already present in the PS5 interface.

The upstream [etaHEN README](https://github.com/etaHEN/etaHEN/blob/main/README.md)
documents two **optional** Direct PKG Installer services: DPI v1 (TCP `9090`)
and DPI v2 WebUI (TCP `12800`). Its documented defaults for `DPI` and
`DPI_v2` are both disabled. **Live read-only TCP handshakes on both ports
timed out**, and FTP `MLST` did not find `/data/etaHEN/config.ini` or the
`/data/etaHEN` directory. We did not enable settings, transmit installer
JSON, start a payload, or call an unauthenticated web endpoint. An open
port alone would not authenticate the service as etaHEN.

Likewise, read-only FTP `MLST` confirmed that `/user/app` is visible, but
`/user/app/MPSX00001`, `/user/app/MPSX00002`,
`/system_data/priv/appmeta/MPSX00001` and
`/system_data/priv/appmeta/MPSX00002` were not visible at the time of
the check. This is scoped filesystem evidence rather than a complete
inventory of installed titles.

### Concrete installation procedure using existing console software

1. **Choose the UI-only build first.** The latest local UI diagnostic PKG
   is `build-ps5-package/runtime-03-diagnostic/pkg/UP9000-MPSX00001_00-MIRRORPSXUI00001-A0100-V0100.pkg`.
   Its reported SHA-256 is
   `08E6DBE5FC4FD8037490131FFA4A2FE81D7F75545B9DC4A4072F5999A9FD1AEA`.
   Confirm that hash locally with `Get-FileHash -Algorithm SHA256` and check
   the adjacent `manifest.json`. The latest diagnostic build was uploaded
   and SHA-256 read-back verified after the installer investigation (see above).
   A newer separate experimental AirPlay PKG is also available at
   `build-ps5-package/airplay-exp-06-secure-pairing/pkg/` with reported
   SHA-256 `1BDF0E4BEEF74D92BCE60FFDA39FF2293283A87F36CCF5843B382B7000901BFB`.
   It remains uninstalled and should be tested only after the native UI
   proves reliable on console.
2. On the PS5, open the package installer **already present** in its homebrew
   manager or existing debug settings. Confirm that it is a trusted component
   you have used for previous homebrew installations. If you cannot find an
   existing installer, stop: the remote services inspected here cannot install
   the package safely.
3. **Prefer the newest `runtime-03-diagnostic` UI package.** If your already
   trusted package manager can browse the private staging path in the table
   above, select that PKG. Otherwise copy the locally verified PKG to a USB
   volume using your manager's established layout and select it there. The
   earlier `runtime-02` package is only for a labeled regression comparison.
   Match the selected file's hash with the corresponding build manifest.
4. Before confirming install, verify that the manager shows `MPSX00001`
   and `UP9000-MPSX00001_00-MIRRORPSXUI00001`. Cancel if a different
   title appears, or if an existing title would be replaced unexpectedly.
   Complete installation on the PS5 with the console's normal trusted UI.
5. From Windows, run `-VerifyInstall` below. It performs **FTP read-only**
   checks for that exact title's app and appmeta paths. If `param.json`
   is present and readable, it checks the title and optional content ID.
   A visible directory without matching metadata remains *unconfirmed*.
6. With the existing Hyperion log recorder active, create an `-Archive`
   cursor **before** pressing Launch on the console. Start MirrorPSX from
   the PS5 home screen, observe what is actually displayed and whether
   DualSense navigation works; then run `-Archive -BaselinePath` to extract
   only the log bytes appended since the cursor. Hold OPTIONS for about
   1.2 seconds to test clean exit once a frame is visible. Preserve both
   evidence folders.

```powershell
# Read-only installer reachability and PS5 FTP inventory
.\scripts\deploy-ps5.ps1 -Console $env:MIRRORPSX_PS5_HOST -Probe

# After installing through the already present console interface:
.\scripts\deploy-ps5.ps1 -Console $env:MIRRORPSX_PS5_HOST -VerifyInstall `
    -TitleId MPSX00001 `
    -ExpectedContentId 'UP9000-MPSX00001_00-MIRRORPSXUI00001'

# Before launch, snapshot all existing log file offsets (no PS5 klog connection):
.\scripts\deploy-ps5.ps1 -Console $env:MIRRORPSX_PS5_HOST -Archive `
    -ArchiveRoot '..\build\console-logs\continuous-20261010'

# After launch, pass the cursor.json path printed by the previous command:
.\scripts\deploy-ps5.ps1 -Console $env:MIRRORPSX_PS5_HOST -Archive `
    -ArchiveRoot '..\build\console-logs\continuous-20261010' `
    -BaselinePath '<BEFORE_LAUNCH_EVIDENCE>\cursor.json'

# Independently inspect the experimental title, without installing it:
.\scripts\deploy-ps5.ps1 -Console $env:MIRRORPSX_PS5_HOST -VerifyInstall -TitleId MPSX00002
```

The log cursor tracks byte offsets for every rotated `klog-*.log` file;
the delta command reads new bytes from the existing recorder's files without
opening a competing connection to port `3232`. The delta refuses truncated
archives or excessive unexamined data instead of silently dropping messages.
Its `capture.json`, `cursor.json` and `mirrorpsx-filtered.log` distinguish
host startup, display selection, reported first-frame submission, exit and
error messages. A first-frame log is evidence of submission only; the user
must still confirm an image on the display. If there are no markers, stderr
forwarding may be unavailable and the visual result takes priority.
The current host logs `first Vulkan frame submitted for presentation`;
older UI-only builds logged `first Vulkan frame presented`. Both messages
set the parser's `firstFrameSubmitted` checkpoint, which deliberately does
**not** claim the television displayed a picture.

**Current measurement:** `-VerifyInstall -TitleId MPSX00001` reported
`not-visible-in-checked-paths`; `-Probe` reported both optional DPI ports
unreachable. The baseline `-Archive` and immediate `-Archive -BaselinePath`
completed without a concurrent klog connection and found no MirrorPSX
markers, as expected before installation. The initial installer audit was
read-only; the later explicitly scoped staging uploaded both new PKGs without
installing or launching either title.

### Reproducible evidence from this read-only investigation

| Check | Local evidence | Observed result |
| --- | --- | --- |
| UI-only metadata check | `build-ps5-package/deployment-evidence/mirrorpsx-20261011T185404Z-6101855b/post-install.json` | Targeted app/appmeta paths not visible |
| AirPlay metadata check | `build-ps5-package/deployment-evidence/mirrorpsx-20261011T185634Z-fa9ad545/post-install.json` | Targeted app/appmeta paths not visible |
| Real klog baseline | `build-ps5-package/deployment-evidence/mirrorpsx-20261011T185414Z-60db6124/cursor.json` | Existing Hyperion recorder examined; no concurrent TCP reader |
| Real klog delta | `build-ps5-package/deployment-evidence/mirrorpsx-20261011T185422Z-2dea11a4/capture.json` | No MirrorPSX lines in the interval |

The `-Archive` delta logic was additionally exercised against temporary
synthetic rotated `klog-001.log` and `klog-002.log` files: after a cursor
was recorded, two subsequent MirrorPSX messages were found; the startup and
first-frame marker flags both became `true`. This validates the **log-parser
behavior**, not actual PS5 rendering. The newer diagnostic UI package was
checked again using the latest exact message
`first Vulkan frame submitted for presentation`: a one-line delta set
`firstFrameSubmitted=true` without a console connection. The UI package was
also independently hashed locally as
`08E6DBE5FC4FD8037490131FFA4A2FE81D7F75545B9DC4A4072F5999A9FD1AEA`;
the latest AirPlay package hash matched
`1BDF0E4BEEF74D92BCE60FFDA39FF2293283A87F36CCF5843B382B7000901BFB`.

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
