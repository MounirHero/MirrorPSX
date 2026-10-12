param(
    [Parameter(Mandatory = $true)][string]$PortsRoot,
    [string]$SdkRoot,
    [string]$HyperionRoot,
    [string]$OutputRoot,
    [switch]$Probe
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $SdkRoot) { $SdkRoot = Join-Path (Split-Path (Split-Path $root -Parent) -Parent) 'sdk' }
if (-not $HyperionRoot) { $HyperionRoot = Split-Path $root -Parent }
if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'build-ps5-codec-probe' }
$SdkRoot = [IO.Path]::GetFullPath($SdkRoot)
$HyperionRoot = [IO.Path]::GetFullPath($HyperionRoot)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$PortsRoot = [IO.Path]::GetFullPath($PortsRoot)
$sdk = Join-Path $SdkRoot 'sdk'
$bin = Join-Path $SdkRoot '.tools\llvm-mingw-20240619-ucrt-x86_64\bin'
$clang = Join-Path $bin 'clang.exe'
$cxx = Join-Path $bin 'clang++.exe'
$link = Join-Path $SdkRoot 'build\bin\Release\prospero-link.exe'
$inspect = Join-Path $SdkRoot 'build\bin\Release\prospero.exe'
$platform = Join-Path $HyperionRoot 'build\ps5-platform-source\platform\build\ps5\libps5platform.a'
$builtins = Join-Path $HyperionRoot 'build\ps5-platform-source\platform\build\ps5\libclang_rt.builtins-x86_64.a'
$src = Join-Path $root 'tests\ps5_codec_probe.cpp'
$decoder = Join-Path $root 'src\receiver\decoder.cpp'
$compat = Join-Path $root 'tests\ps5_codec_compat.c'
$libraries = @('libavcodec.a', 'libswscale.a', 'libavutil.a', 'libswresample.a',
               'libx264.a', 'liblzma.a', 'libiconv.a', 'libssl.a', 'libcrypto.a', 'libz.a') |
    ForEach-Object { Join-Path (Join-Path $PortsRoot 'lib') $_ }
$files = @($clang, $cxx, $link, $inspect, $platform, $builtins,
           $src, $decoder, $compat) + $libraries
$missing = @($files | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
if ($missing.Count) { throw "Missing required probe inputs: $($missing -join ', ')" }
if (-not (Test-Path -LiteralPath (Join-Path $PortsRoot 'include\libavcodec\avcodec.h') -PathType Leaf)) {
    throw 'PacBrew libavcodec headers not found under PortsRoot/include'
}
Write-Host "PacBrew codec probe: $PortsRoot"
if ($Probe) { Write-Host 'All probe prerequisites found. No compilation performed.'; return }
if (Test-Path -LiteralPath $OutputRoot) { throw "OutputRoot already exists: $OutputRoot" }
New-Item -ItemType Directory -Path $OutputRoot | Out-Null
$resource = (& $cxx -print-resource-dir).Trim()
if ($LASTEXITCODE -ne 0 -or -not $resource) { throw 'Clang resource directory lookup failed' }
$flags = @('--target=x86_64-sie-ps5', '-O2', '-fPIC', '-fPIE', '-ffreestanding', '-nostdinc',
           '-D__SCE__=1', '-isystem', (Join-Path $sdk 'target\cxx\include\c++\v1'),
           '-isystem', (Join-Path $resource 'include'), '-isystem',
           (Join-Path $sdk 'target\cxx\include'), '-isystem',
           (Join-Path $sdk 'target\include'), '-I', (Join-Path $PortsRoot 'include'),
           '-I', (Join-Path $root 'src'))
function Invoke-Probe([string]$tool, [string[]]$toolArgs, [string]$name) {
    & $tool @toolArgs
    if ($LASTEXITCODE -ne 0) { throw "$name failed (exit $LASTEXITCODE)" }
}
Invoke-Probe $cxx ($flags + @('-std=c++20', '-fno-exceptions', '-fno-rtti',
    '-c', $src, '-o', (Join-Path $OutputRoot 'probe.o'))) 'H.264/HEVC probe compile'
Invoke-Probe $cxx ($flags + @('-std=c++20', '-fno-exceptions', '-fno-rtti',
    '-c', $decoder, '-o', (Join-Path $OutputRoot 'decoder.o'))) 'MirrorPSX decoder compile'
Invoke-Probe $clang @('--target=x86_64-sie-ps5', '-O2', '-fPIC', '-c', $compat,
    '-o', (Join-Path $OutputRoot 'compat.o')) 'Experimental ABI shim compile'
$linkArgs = @('--profile', 'application', '--sdk-root', $sdk, '--title-heap', '--no-companion',
          '--', '--error-limit=12', '--start-group',
          (Join-Path $OutputRoot 'probe.o'), (Join-Path $OutputRoot 'decoder.o'),
          (Join-Path $OutputRoot 'compat.o')) + $libraries +
         @('-lc++', '-lc++abi', '-lunwind', $platform, $builtins, '--end-group', '-lSceNet',
           '--defsym=arc4random_buf=ps5_arc4random_buf',
           '-o', (Join-Path $OutputRoot 'codec-probe.elf'))
Invoke-Probe $link $linkArgs 'OpenProspero PacBrew codec link'
Invoke-Probe $inspect @('inspect', (Join-Path $OutputRoot 'codec-probe.elf')) 'ELF inspection'
Write-Host "Experimental codec ELF: $(Join-Path $OutputRoot 'codec-probe.elf')"
Write-Host 'Link-only probe. Runtime, libc shim and licensing compatibility are NOT verified.'
