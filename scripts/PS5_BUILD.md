# Native PS5 package build

`build-ps5.ps1` compiles the MirrorPSX television UI host with the installed
OpenProspero PS5 toolchain, links the prebuilt PS5 RADV and platform libraries,
stages the required local fonts and application module, and uses OpenProspero
to generate and structurally validate a native PS5 PKG.

Run from this repository in PowerShell (requires the parent Hyperion working
tree and the sibling OpenProspero checkout):

```powershell
$arguments = @{
    SdkRoot          = 'C:\path\to\OpenProspero'
    RadvArchive      = '..\build\radv-osabi9.a'
    PlatformArchive  = '..\build\ps5-platform-source\platform\build\ps5\libps5platform.a'
    BuiltinsArchive  = '..\build\ps5-platform-source\platform\build\ps5\libclang_rt.builtins-x86_64.a'
    LibcPrx          = '..\build\gl-ui-console\package-input\sce_module\libc.prx'
    OutputRoot       = '.\build-ps5-package\release-01'
}
& .\scripts\build-ps5.ps1 @arguments -Probe
& .\scripts\build-ps5.ps1 @arguments
```

An already downloaded Dear ImGui `v1.92.9b` checkout under
`build-mingw/_deps/mirror_imgui-src` is detected automatically. Set `-ImGuiDir`
if it lives elsewhere. `-Probe` checks prerequisites without creating files;
`-StageOnly` builds and stages the ELF without requiring `-LibcPrx` or generating
a PKG. For an actual package, the provided `libc.prx` must match the
source-built signed firmware 9.40 module recorded by Hyperion; the script
verifies its size and SHA-256 before staging.

The default package ID uses OpenProspero's four-letter/five-digit title-ID
format (`UP9000-MPSX00001_00-MIRRORPSXUI00001`). Change the ID when
distributing under an independently allocated title identifier. The build
destination must be new and contained within `build-ps5-package`, preventing
old PKGs and assets from being mixed into releases. `-SceSysDir` can stage
console launcher artwork if available. Source-derived fonts and their license
files are copied from the MirrorPSX `assets` directory.

Outputs include `app/application.elf`, `app/eboot.bin`, `package-input/`,
`pkg/*.pkg` and `manifest.json` with hashes. The pipeline reuses the explicitly
supplied, compatible RADV/platform/builtins archives and Hyperion's native PS5
identity wrappers. Vulkan display, controller navigation, installation and
AirPlay operation still require physical-console testing: OpenProspero's
`validate` command checks package structure, not execution or signing.

## Experimental AirPlay profile

The reference UI-only title remains `MPSX00001`. To build the separately
identified `MPSX00002` profile, obtain the hash-verified PacBrew release and
unmodified upstream libplist 2.7.0 source described in
`../docs/PACBREW.md`. Call `build-ps5-airplay-experimental.ps1` with real
SDK/RADV/platform archives, verified libc.prx, the PacBrew headers/libraries,
and a fresh OutputSuffix. The orchestration builds libplist, UxPlay and
codec link probes before producing the fully linked AirPlay/Vulkan PKG.
It never replaces the UI-only title.

The full receiver remains experimental: the PacBrew/libiconv ABI bridge,
console sockets/mDNS, runtime decoding and optional companion permissions
are not validated. First test UI-only installation and visible Vulkan frames.
