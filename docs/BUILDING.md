# Building MirrorPSX

Run commands from the repository root. Build products, reference checkouts and
private broker configuration are not tracked by Git.

## Windows preview

The desktop script expects MSYS2 under `C:\msys64`, with MinGW C/C++, CMake,
Ninja, Vulkan, GLFW, FFmpeg, SDL2, libxml2 and the UxPlay dependencies available
through pkg-config. Python 3.11+ is used for tooling; Pillow is needed for PS5
package artwork. The ImGui and Volk source directories must be present.

```powershell
.\tools\build-desktop.ps1 -Run
```

Use `-MsysRoot` or `-BuildDirectory` to select another installation/output.
The script bundles DLL dependencies, runs CTest and launches from the repository
root so `assets/ui/` resolves correctly. A desktop source build is not a PS5
release artifact.

For presentation checks without network services:

```powershell
.\build\desktop\MirrorPSX.exe --ui-preview ready
.\build\desktop\MirrorPSX.exe --ui-preview pin
.\build\desktop\MirrorPSX.exe --gpu-smoke
```

Other preview states are `paused`, `error`, `video` and `portrait`.
`--dlna` starts the desktop receiver in video-content mode.

## Native PS5 build

For GitHub Actions, use [CI.md](CI.md): the workflow checks out the private SDK
with a read-only secret and fetches pinned public inputs on a clean runner.
The sibling paths below describe the local development layout only.

The current toolchain uses Debian WSL and LLVM 19. It requires these local
checkouts/build inputs; a plain clone alone does not provide all of them:

| Input | Default location |
| --- | --- |
| OpenProspero SDK and host tools | `../sdk` (`OPENPROSPERO_SDK` points to its `sdk/` subdirectory) |
| Mesa Vulkan headers | `../../PS5_Mesa/include` |
| Pinned RADV/platform sources and build | Paths in `tools/build-ps5-deps.sh` and `tools/build-ps5-platform.sh`, including Hyperion's upstream platform checkout |
| FFmpeg and libplist source trees | `reference/ffmpeg`, `reference/libplist` |
| Clean-room native libc source | `reference/ps5-homebrew-ui/tooling/native` |

Keep the revisions expected by the build scripts. Native OpenSSL, FFmpeg and
libxml2 are built into `build/ps5-deps`. The platform layer uses an immutable
upstream revision; the scripts do not modify the Hyperion checkout.

```powershell
# Creates/reuses the workstation's shared configuration and builds the broker.
.\tools\build-shared-broker.ps1

# Builds the title and records which broker belongs with its executable.
wsl -d Debian bash tools/build-ps5.sh

# Builds the independently authored runtime used in the native title.
wsl -d Debian bash tools/build-ps5-runtime.sh

# Exports a matching PKG + ZIP + external companion release set.
.\tools\package-ps5.ps1 -Version 10.00
```

The app links without an internal companion/autostart object. A post-link
receipt binds the native executable to the external broker build. See
[RELEASING.md](RELEASING.md) for validation, artifact names and publication.

## Checks

```powershell
ctest --test-dir build/desktop --output-on-failure
python tests/ps5_release_test.py
```

For CI configuration checks, install PyYAML and run
`python tests/ci_config_test.py`; the hosted workflow already provides it.

The release suite uses synthetic files and checks archive contents, identity,
runtime integrity, app/broker matching and rejection of private files. It does
not install or run console code. The independent PIN test exercises the actual
desktop RTSP receiver; the media integration test uses a generated MP4 fixture
and dummy audio output when FFmpeg is available.

UI and pipeline checks, benchmarks and protocol tests do not establish PS5
performance or device compatibility. Hardware evidence is recorded separately
in [CONSOLE_TESTING.md](CONSOLE_TESTING.md).

CI also compiles and tests the host receiver on Linux and the desktop preview on
Windows in Debug mode. Only Windows gets a portable preview artifact; the native
PS5 release remains a separate, credentialed job. See [CI.md](CI.md).
