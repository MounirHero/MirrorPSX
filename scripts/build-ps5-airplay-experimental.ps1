param(
    [Parameter(Mandatory = $true)][string]$LibplistSource,
    [Parameter(Mandatory = $true)][string]$PayloadHeaders,
    [Parameter(Mandatory = $true)][string]$PortsRoot,
    [Parameter(Mandatory = $true)][string]$RadvArchive,
    [Parameter(Mandatory = $true)][string]$PlatformArchive,
    [Parameter(Mandatory = $true)][string]$BuiltinsArchive,
    [Parameter(Mandatory = $true)][string]$LibcPrx,
    [string]$SdkRoot,
    [string]$HyperionRoot,
    [string]$OutputSuffix = 'airplay-exp-01',
    [switch]$Probe
)
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $SdkRoot) { $SdkRoot = Join-Path (Split-Path (Split-Path $project -Parent) -Parent) 'sdk' }
if (-not $HyperionRoot) { $HyperionRoot = Split-Path $project -Parent }
if ($OutputSuffix -cnotmatch '^[a-zA-Z0-9_-]{1,48}$') {
    throw 'OutputSuffix may contain only letters, digits, underscore and dash'
}
$libplistBuild = Join-Path $project "build-ps5-libplist-$OutputSuffix"
$airplayBuild = Join-Path $project "build-ps5-uxplay-$OutputSuffix"
$codecBuild = Join-Path $project "build-ps5-codec-$OutputSuffix"
$pkgBuild = Join-Path $project "build-ps5-package\$OutputSuffix"
$ports = [IO.Path]::GetFullPath($PortsRoot)
$uxplay = Join-Path $project 'third_party\uxplay\lib'

$libplistArgs = @{
    LibplistSource = $LibplistSource
    PayloadHeaders = $PayloadHeaders
    SdkRoot = $SdkRoot
    OutputRoot = $libplistBuild
}
$codecArgs = @{
    PortsRoot = $PortsRoot
    SdkRoot = $SdkRoot
    HyperionRoot = $HyperionRoot
    OutputRoot = $codecBuild
}
$airplayArgs = @{
    PayloadHeaders = $PayloadHeaders
    PortsRoot = $PortsRoot
    LibplistSource = $LibplistSource
    LibplistArchive = (Join-Path $libplistBuild 'libplist-2.0.a')
    SdkRoot = $SdkRoot
    HyperionRoot = $HyperionRoot
    OutputRoot = $airplayBuild
}
if ($Probe) {
    & (Join-Path $PSScriptRoot 'build-ps5-libplist.ps1') @libplistArgs -Probe
    & (Join-Path $PSScriptRoot 'build-pacbrew-codec-probe.ps1') @codecArgs -Probe
    Write-Host 'UxPlay dependency validation will run after libplist archive is built.'
    return
}
foreach ($path in @($libplistBuild, $airplayBuild, $codecBuild, $pkgBuild)) {
    if (Test-Path -LiteralPath $path) {
        throw "Refusing to overwrite existing PS5 experimental build: $path"
    }
}
& (Join-Path $PSScriptRoot 'build-ps5-libplist.ps1') @libplistArgs
if (-not $?) {throw 'PS5 libplist build failed'}
& (Join-Path $PSScriptRoot 'build-ps5-airplay-probe.ps1') @airplayArgs
if (-not $?) {throw 'PS5 UxPlay cross-compile failed'}
& (Join-Path $PSScriptRoot 'build-pacbrew-codec-probe.ps1') @codecArgs
if (-not $?) {throw 'PS5 codec dependency compile failed'}

$objects = @(
    Get-ChildItem -LiteralPath $airplayBuild -Filter 'uxplay-*.o' -File |
        Sort-Object Name | ForEach-Object FullName
)
$objects += Join-Path $codecBuild 'compat.o'
$archives = @((Join-Path $libplistBuild 'libplist-2.0.a'))
$archives += @(
    'libavcodec.a','libswscale.a','libavutil.a','libswresample.a',
    'libx264.a','liblzma.a','libiconv.a','libssl.a','libcrypto.a','libz.a'
) | ForEach-Object {Join-Path $ports "lib\$_"}

$buildArgs = @{
    SdkRoot = $SdkRoot
    HyperionRoot = $HyperionRoot
    RadvArchive = $RadvArchive
    PlatformArchive = $PlatformArchive
    BuiltinsArchive = $BuiltinsArchive
    LibcPrx = $LibcPrx
    OutputRoot = $pkgBuild
    ContentId = 'UP9000-MPSX00002_00-MIRRORPSXAIR0001'
    Title = 'MirrorPSX AirPlay Experimental'
    PackageVersion = '01.00'
    EnableAirPlay = $true
    ExtraSources = @(
        (Join-Path $project 'src\receiver\airplay.cpp'),
        (Join-Path $project 'src\receiver\decoder.cpp'),
        (Join-Path $project 'src\platform\video_texture.cpp')
    )
    ExtraIncludes = @($uxplay)
    ExtraSystemIncludes = @(
        ([IO.Path]::GetFullPath($PayloadHeaders)),
        (Join-Path $ports 'include')
    )
    ExtraObjectFiles = $objects
    ExtraArchives = $archives
}
& (Join-Path $PSScriptRoot 'build-ps5.ps1') @buildArgs
if (-not $?) {throw 'PS5 AirPlay experimental PKG build failed'}
Write-Host "Built separate UNVERIFIED PS5 AirPlay profile: $pkgBuild"
Write-Host 'Do not replace UI-only reference package until console runtime proves this works.'
