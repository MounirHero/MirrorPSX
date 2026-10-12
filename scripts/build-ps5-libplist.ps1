param(
    [Parameter(Mandatory = $true)][string]$LibplistSource,
    [Parameter(Mandatory = $true)][string]$PayloadHeaders,
    [string]$SdkRoot,
    [string]$OutputRoot,
    [switch]$Probe
)
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $SdkRoot) { $SdkRoot = Join-Path (Split-Path (Split-Path $project -Parent) -Parent) 'sdk' }
if (-not $OutputRoot) { $OutputRoot = Join-Path $project 'build-ps5-libplist' }
$SdkRoot = [IO.Path]::GetFullPath($SdkRoot)
$LibplistSource = [IO.Path]::GetFullPath($LibplistSource)
$PayloadHeaders = [IO.Path]::GetFullPath($PayloadHeaders)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$bin = Join-Path $SdkRoot '.tools\llvm-mingw-20240619-ucrt-x86_64\bin'
$clang = Join-Path $bin 'clang.exe'
$ar = Join-Path $bin 'llvm-ar.exe'
$link = Join-Path $SdkRoot 'build\bin\Release\prospero-link.exe'
$cli = Join-Path $SdkRoot 'build\bin\Release\prospero.exe'
$sdk = Join-Path $SdkRoot 'sdk'
$sourceFiles = @(
    'libcnary/node.c', 'libcnary/node_list.c',
    'src/base64.c', 'src/bytearray.c', 'src/hashtable.c',
    'src/ptrarray.c', 'src/time64.c', 'src/xplist.c', 'src/bplist.c',
    'src/jsmn.c', 'src/jplist.c', 'src/oplist.c', 'src/out-default.c',
    'src/out-plutil.c', 'src/out-limd.c', 'src/plist.c'
)
$required = @($clang, $ar, $link, $cli,
    (Join-Path $LibplistSource 'include\plist\plist.h'),
    (Join-Path $PayloadHeaders 'stdlib.h'),
    (Join-Path $project 'tests\ps5_plist_probe.c')) +
    @($sourceFiles | ForEach-Object {Join-Path $LibplistSource $_})
$missing = @($required | Where-Object {-not (Test-Path -LiteralPath $_ -PathType Leaf)})
if ($missing.Count) {throw "Missing libplist PS5 prerequisites: $($missing -join ', ')"}
$tag = & git -C $LibplistSource describe --tags --exact-match HEAD 2>$null
if ($LASTEXITCODE -ne 0 -or $tag.Trim() -ne '2.7.0') {
    throw 'Expected an upstream libplist checkout pinned to tag 2.7.0'
}
if ($Probe) {
    Write-Host "libplist 2.7.0 pinned PS5 prerequisites PASS: $LibplistSource"
    return
}
if (Test-Path -LiteralPath $OutputRoot) {
    throw "Refusing to overwrite an existing build: $OutputRoot"
}
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$config = Join-Path $OutputRoot 'config.h'
@(
    '#define PACKAGE_VERSION "2.7.0"',
    '#define HAVE_STRNDUP 1',
    '#define HAVE_LOCALTIME_R 1',
    '#define HAVE_GMTIME_R 1',
    '#define HAVE_TIMEGM 1',
    '#define HAVE_MEMMEM 1'
) | Set-Content -LiteralPath $config -Encoding ascii
$resource = (& $clang -print-resource-dir).Trim()
if ($LASTEXITCODE -ne 0 -or -not $resource) {throw 'Cannot determine Clang resource directory'}
$flags = @('--target=x86_64-sie-ps5', '-std=gnu11', '-O2', '-fPIC',
    '-ffreestanding', '-nostdinc', '-D__SCE__=1', '-D_GNU_SOURCE',
    '-Wno-unreachable-code-generic-assoc',
    '-include', $config, '-isystem', $PayloadHeaders,
    '-isystem', (Join-Path $resource 'include'),
    '-I', (Join-Path $LibplistSource 'include'),
    '-I', (Join-Path $LibplistSource 'src'),
    '-I', (Join-Path $LibplistSource 'libcnary\include'))
$objects = @()
for ($i=0; $i -lt $sourceFiles.Count; $i++) {
    $obj = Join-Path $OutputRoot ("libplist-{0:D2}.o" -f $i)
    & $clang @flags -c (Join-Path $LibplistSource $sourceFiles[$i]) -o $obj
    if ($LASTEXITCODE -ne 0) {throw "libplist compilation failed: $($sourceFiles[$i])"}
    $objects += $obj
}
$archive = Join-Path $OutputRoot 'libplist-2.0.a'
& $ar rcs $archive @objects
if ($LASTEXITCODE -ne 0) {throw 'libplist archive creation failed'}
$probeObj = Join-Path $OutputRoot 'ps5_plist_probe.o'
& $clang @flags -c (Join-Path $project 'tests\ps5_plist_probe.c') -o $probeObj
if ($LASTEXITCODE -ne 0) {throw 'libplist integration probe compilation failed'}
$elf = Join-Path $OutputRoot 'libplist-probe.elf'
$linkArgs = @('--profile', 'application', '--sdk-root', $sdk, '--title-heap',
    '--no-companion', '--', '--start-group', $probeObj, $archive,
    '--end-group', '-o', $elf)
& $link @linkArgs
if ($LASTEXITCODE -ne 0) {throw 'libplist OpenProspero PS5 linking failed'}
& $cli inspect $elf
if ($LASTEXITCODE -ne 0) {throw 'libplist PS5 ELF structural inspect failed'}
Write-Host "PS5 libplist cross-compile/link PASS: $archive / $elf"
Write-Host 'Link-only: actual on-console XML parse/encode still requires execution testing.'
