param(
    [Parameter(Mandatory = $true)][string]$PayloadHeaders,
    [Parameter(Mandatory = $true)][string]$PortsRoot,
    [Parameter(Mandatory = $true)][string]$LibplistSource,
    [Parameter(Mandatory = $true)][string]$LibplistArchive,
    [string]$SdkRoot,
    [string]$HyperionRoot,
    [string]$OutputRoot,
    [switch]$Probe
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $SdkRoot) { $SdkRoot = Join-Path (Split-Path (Split-Path $root -Parent) -Parent) 'sdk' }
if (-not $HyperionRoot) { $HyperionRoot = Split-Path $root -Parent }
if (-not $OutputRoot) { $OutputRoot = Join-Path $root 'build-ps5-airplay-link' }
$SdkRoot = [IO.Path]::GetFullPath($SdkRoot)
$HyperionRoot = [IO.Path]::GetFullPath($HyperionRoot)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$PayloadHeaders = [IO.Path]::GetFullPath($PayloadHeaders)
$PortsRoot = [IO.Path]::GetFullPath($PortsRoot)
$LibplistSource = [IO.Path]::GetFullPath($LibplistSource)
$LibplistArchive = [IO.Path]::GetFullPath($LibplistArchive)
$uxplay = Join-Path $root 'third_party\uxplay\lib'
$bin = Join-Path $SdkRoot '.tools\llvm-mingw-20240619-ucrt-x86_64\bin'
$clang = Join-Path $bin 'clang.exe'
$link = Join-Path $SdkRoot 'build\bin\Release\prospero-link.exe'
$inspect = Join-Path $SdkRoot 'build\bin\Release\prospero.exe'
$platform = Join-Path $HyperionRoot 'build\ps5-platform-source\platform\build\ps5\libps5platform.a'
$builtins = Join-Path $HyperionRoot 'build\ps5-platform-source\platform\build\ps5\libclang_rt.builtins-x86_64.a'
$libssl = Join-Path $PortsRoot 'lib\libssl.a'
$libcrypto = Join-Path $PortsRoot 'lib\libcrypto.a'
$libz = Join-Path $PortsRoot 'lib\libz.a'
$probeSource = Join-Path $root 'tests\ps5_airplay_probe.c'
$dirs = @('', 'llhttp', 'playfair', 'mdnsd')
$sourceFiles = @(
    foreach ($dir in $dirs) {
        Get-ChildItem -LiteralPath (Join-Path $uxplay $dir) -File -Filter '*.c' |
            Sort-Object Name | ForEach-Object FullName
    }
)
if ($sourceFiles.Count -lt 25) {throw "Incomplete UxPlay source set: $($sourceFiles.Count)"}
$required = @($clang, $link, $inspect, $platform, $builtins,
    $libssl, $libcrypto, $libz, $LibplistArchive, $probeSource,
    (Join-Path $LibplistSource 'include\plist\plist.h'),
    (Join-Path $PayloadHeaders 'sys\endian.h'),
    (Join-Path $PortsRoot 'include\openssl\evp.h')) + $sourceFiles
$missing = @($required | Where-Object {-not (Test-Path -LiteralPath $_ -PathType Leaf)})
if ($missing.Count) {throw "Missing AirPlay PS5 prerequisites: $($missing -join ', ')"}
if ($Probe) {
    Write-Host "AirPlay source probe PASS: $($sourceFiles.Count) UxPlay C translation units"
    Write-Host 'No compilation or socket calls performed.'
    return
}
if (Test-Path -LiteralPath $OutputRoot) {
    throw "Output already exists: $OutputRoot"
}
New-Item -ItemType Directory $OutputRoot -Force | Out-Null
$resource = (& $clang -print-resource-dir).Trim()
if ($LASTEXITCODE -ne 0 -or -not $resource) {throw 'Cannot determine clang resource directory'}
$flags = @('--target=x86_64-sie-ps5','-std=gnu11','-O1','-fPIC',
    '-ffreestanding','-nostdinc','-D__SCE__=1','-D_GNU_SOURCE',
    '-DTARGET_POSIX','-D_LINUX','-DNOHOLD','-DSTANDALONE',
    '-DSYS_ENDIAN_H','-DPLIST_210','-DPLIST_230',
    '-Wno-unreachable-code-generic-assoc',
    '-isystem', $PayloadHeaders, '-isystem',(Join-Path $PortsRoot 'include'),
    '-isystem',(Join-Path $resource 'include'),
    '-I',$uxplay,
    '-I',(Join-Path $LibplistSource 'include'),
    '-I',(Join-Path $uxplay 'llhttp'),
    '-I',(Join-Path $uxplay 'playfair'),
    '-I',(Join-Path $uxplay 'mdnsd'))
$objects = @()
for ($i = 0; $i -lt $sourceFiles.Count; $i++) {
    $obj = Join-Path $OutputRoot ("uxplay-{0:D2}.o" -f $i)
    & $clang @flags -c $sourceFiles[$i] -o $obj
    if ($LASTEXITCODE -ne 0) {throw "PS5 UxPlay compile failed: $($sourceFiles[$i])"}
    $objects += $obj
}
$probeObj = Join-Path $OutputRoot 'probe.o'
& $clang @flags -c $probeSource -o $probeObj
if ($LASTEXITCODE -ne 0) {throw 'PS5 AirPlay smoke probe compile failed'}
$elf = Join-Path $OutputRoot 'airplay-probe.elf'
$linkArgs = @('--profile','application','--sdk-root',
    (Join-Path $SdkRoot 'sdk'),'--title-heap','--no-companion','--',
    '--error-limit=12','--start-group',$probeObj) + $objects +
    @($LibplistArchive,$libssl,$libcrypto,$libz,
      '-lc++','-lc++abi','-lunwind',$platform,$builtins,'--end-group',
      '-lSceNet','--defsym=arc4random_buf=ps5_arc4random_buf','-o',$elf)
& $link @linkArgs
if ($LASTEXITCODE -ne 0) {throw 'PS5 UxPlay / libplist / OpenSSL link failed'}
& $inspect inspect $elf
if ($LASTEXITCODE -ne 0) {throw 'OpenProspero failed to inspect AirPlay probe ELF'}
Write-Host "PS5 UxPlay cross-compile/link PASS: $elf"
Write-Host 'No console installation or runtime test was performed by this script.'
