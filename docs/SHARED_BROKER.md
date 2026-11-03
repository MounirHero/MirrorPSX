# Shared external OpenProspero companion

A single `OpenProspero-SharedBroker.elf` serves multiple compatible applications.
It is loaded separately from the titles, stays running when a client closes and
must be loaded again after a console reboot. MirrorPSX does not embed, autostart,
replace or stop the broker. The daemon and public protocol are reused from the
OpenProspero SDK without modifying its sources or the Hyperion project.

## First run

1. Load the companion ELF supplied with the release through a compatible PS5
   ELF loader. The payload environment must provide the kernel functionality
   required by OpenProspero's privileged companion. Start it once.
2. Choose one app format from `dist/ps5-vNN.NN/`: install the PKG, or extract the
   ZIP and copy `PPSA99221/` to `/data/homebrew/PPSA99221/` for ShadowMountPlus.
3. Launch MirrorPSX. In the log, check `External broker promoted own PID=...`
   followed by `Receiver listening: 1`. Try Screen Mirroring from the iPad.
4. If the broker was unavailable, start it and select Retry in MirrorPSX.
   A timeout is never treated as confirmed success.

Use a matched companion/app release. An existing Hyperion companion with a
separate configuration is not automatically interchangeable; the app will not
terminate or replace that process. Do not keep PKG and folder installations of
MirrorPSX's title ID active together.

## Contract for other applications

- Link `libopenprospero_companion.a` and use the SDK's
  `openprospero/companion_privileged.h` public API.
- Share the build configuration without putting its token in source control
  or logs. Matching protocol versions alone do not establish compatibility.
- Connect to the console itself: loopback or its routed local IPv4 address,
  on ports 9069 through 9072.
- Use the SDK's UDP request when title TCP is unavailable. Requests and waits
  must be bounded; validate replies rather than assuming a sent request succeeded.
- Ask to promote the app's own PID and check the operation result and all
  rootdir, jaildir and credential results before starting the receiver.
- Load `/app0` assets before promotion, or preserve valid file descriptors / use
  the correct physical mount afterwards. Promotion changes the filesystem root.
- Reuse the running service. Do not create a daemon per title or stop the shared
  service when a client exits.

MirrorPSX keeps its fonts and atlas in memory before promotion. Pairing keys
are written afterwards to `/data/mirrorpsx/pairing.key`; KERN_ARND provides the
native random bytes. Apple and DLNA startup both require confirmed preparation.
Other applications must adopt this contract; they are not modified by this work.

## Build and release

```powershell
.\tools\build-shared-broker.ps1
wsl -d Debian bash tools/build-ps5.sh
.\tools\package-ps5.ps1 -Version 10.00
```

The workstation's shared token stays in
`%LOCALAPPDATA%/OpenProspero/shared-broker/token.txt`. The generated header and
broker manifest live only in `build/broker-config/`. Rebuilding reuses the token.
The app's post-link receipt records the executable and broker hashes. Packaging
rejects a mismatched pair and exports the companion separately beside both app
formats in `dist/ps5-v10.00/`. See [RELEASING.md](RELEASING.md).

The SDK build uses AllowPrivileged/Lan. Its legacy protocol has a 32-bit token
and is not encrypted. TCP peers are restricted to the console itself; UDP
requests require the configured token. Do not expose this service to the
Internet. Optional shutdown and OPCP spawn are not enabled; the SDK daemon's
privileged operations remain available.

## Hardware evidence

The R8 console log confirms process promotion, persistent keys, DNS-SD
registration and an incoming IPv6 iPad connection. Later timing and PIN fixes
have desktop tests; successful PS5 video/audio and folder launch still require
hardware testing. See [CONSOLE_TESTING.md](CONSOLE_TESTING.md).
