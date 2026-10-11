param(
    [string]$SdkRoot = $env:OPENPROSPERO_ROOT,
    [string]$HyperionRoot,
    [string]$ImGuiDir,
    [string]$VulkanIncludeDir,
    [string]$VolkDir,
    [string]$RadvArchive,
    [string]$PlatformArchive,
    [string]$BuiltinsArchive,
    [string]$LibcPrx,
    [string]$OutputRoot,
    [string]$ContentId = 'UP9000-MPSX00001_00-MIRRORPSXUI00001',
    [string]$Title = 'MirrorPSX',
    [string]$PackageVersion = '01.00',
    [string]$SceSysDir,
    [string[]]$ExtraSources = @(),
    [string[]]$ExtraArchives = @(),
    [string[]]$ExtraIncludes = @(),
    [string[]]$ExtraSystemIncludes = @(),
    [string[]]$ExtraObjectFiles = @(),
    [string[]]$ExtraDefines = @(),
    [switch]$EnableAirPlay,
    [switch]$Probe,
    [switch]$StageOnly
)

$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $HyperionRoot) { $HyperionRoot = Split-Path -Parent $project }
if (-not $OutputRoot) { $OutputRoot = Join-Path $project 'build-ps5-package' }

function Full-Path([string]$Value, [string]$Base = $project) {
    if (-not $Value) { return $null }
    if ([IO.Path]::IsPathRooted($Value)) { return [IO.Path]::GetFullPath($Value) }
    return [IO.Path]::GetFullPath((Join-Path $Base $Value))
}

function Require-File([string]$Label, [string]$Value) {
    if (-not $Value -or -not (Test-Path -LiteralPath $Value -PathType Leaf)) {
        $script:missing.Add("$Label : $Value")
    }
}

function Require-Directory([string]$Label, [string]$Value) {
    if (-not $Value -or -not (Test-Path -LiteralPath $Value -PathType Container)) {
        $script:missing.Add("$Label : $Value")
    }
}

function Invoke-Checked([string]$Binary, [string[]]$Arguments, [string]$Context) {
    & $Binary @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Context (exit $LASTEXITCODE)" }
}

if ($ContentId -cnotmatch '^[A-Z]{2}[0-9]{4}-[A-Z]{4}[0-9]{5}_[0-9]{2}-[A-Z0-9]{16}$') {
    throw 'ContentId must match UP9000-MPSX00001_00-MIRRORPSXUI00001 (replace with your allocated ID).'
}
if ($PackageVersion -cnotmatch '^[0-9]{2}\.[0-9]{2}$') {
    throw 'PackageVersion must be NN.NN (for example 01.00).'
}
if (-not $Title.Trim()) { throw 'Package title is empty.' }
$OutputRoot = Full-Path $OutputRoot
$buildBase = [IO.Path]::GetFullPath((Join-Path $project 'build-ps5-package')).TrimEnd('\', '/')
if (-not ($OutputRoot.Equals($buildBase, [StringComparison]::OrdinalIgnoreCase) -or
           $OutputRoot.StartsWith($buildBase + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase))) {
    throw "OutputRoot must be inside $buildBase"
}
if (-not $Probe -and (Test-Path -LiteralPath $OutputRoot)) {
    throw "Output already exists; specify a new -OutputRoot to avoid stale packaging: $OutputRoot"
}

$HyperionRoot = Full-Path $HyperionRoot
if (-not $SdkRoot) { $SdkRoot = Join-Path (Split-Path -Parent $HyperionRoot) 'sdk' }
$SdkRoot = Full-Path $SdkRoot
if (-not $VolkDir) { $VolkDir = Join-Path $HyperionRoot 'vendor\PS5_VulkanTemplate\ps5\third_party\volk' }
if (-not $VulkanIncludeDir) { $VulkanIncludeDir = Join-Path $HyperionRoot 'vendor\PS5_VulkanTemplate\external' }
$VolkDir = Full-Path $VolkDir
$VulkanIncludeDir = Full-Path $VulkanIncludeDir

if (-not $ImGuiDir) {
    foreach ($candidate in @(
        (Join-Path $project 'build-mingw\_deps\mirror_imgui-src'),
        (Join-Path $project 'build-vulkan-ui-only\_deps\mirror_imgui-src'),
        (Join-Path $project 'build-ui-only\_deps\mirror_imgui-src'),
        (Join-Path $project 'build-updated\_deps\mirror_imgui-src'),
        (Join-Path $project 'build\_deps\mirror_imgui-src'),
        (Join-Path $HyperionRoot 'vendor\PS5_VulkanTemplate\external\imgui')
    )) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'imgui.cpp') -PathType Leaf) {
            $ImGuiDir = $candidate
            break
        }
    }
}
$ImGuiDir = Full-Path $ImGuiDir
$RadvArchive = Full-Path $RadvArchive
$PlatformArchive = Full-Path $PlatformArchive
$BuiltinsArchive = Full-Path $BuiltinsArchive
$LibcPrx = Full-Path $LibcPrx
$SceSysDir = Full-Path $SceSysDir
$ExtraSources = @($ExtraSources | ForEach-Object { Full-Path $_ })
$ExtraArchives = @($ExtraArchives | ForEach-Object { Full-Path $_ })
$ExtraIncludes = @($ExtraIncludes | ForEach-Object { Full-Path $_ })
$ExtraSystemIncludes = @($ExtraSystemIncludes | ForEach-Object { Full-Path $_ })
$ExtraObjectFiles = @($ExtraObjectFiles | ForEach-Object { Full-Path $_ })
if ($EnableAirPlay -and (-not $ExtraSources -or -not $ExtraObjectFiles -or -not $ExtraArchives)) {
    throw 'AirPlay profile requires explicit C++ sources, UxPlay C objects and dependency archives.'
}

$toolchain = Join-Path $SdkRoot '.tools\llvm-mingw-20240619-ucrt-x86_64\bin'
$clang = Join-Path $toolchain 'clang.exe'
$clangxx = Join-Path $toolchain 'clang++.exe'
$archiver = Join-Path $toolchain 'llvm-ar.exe'
$linker = Join-Path $SdkRoot 'build\bin\Release\prospero-link.exe'
$cli = Join-Path $SdkRoot 'build\bin\Release\prospero.exe'
$sdk = Join-Path $SdkRoot 'sdk'
$mainSource = Join-Path $project 'src\platform\ps5_main.cpp'

$sources = @(
    $mainSource,
    (Join-Path $project 'src\ui\screen.cpp'),
    (Join-Path $ImGuiDir 'imgui.cpp'),
    (Join-Path $ImGuiDir 'imgui_draw.cpp'),
    (Join-Path $ImGuiDir 'imgui_tables.cpp'),
    (Join-Path $ImGuiDir 'imgui_widgets.cpp'),
    (Join-Path $ImGuiDir 'backends\imgui_impl_vulkan.cpp')
) + $ExtraSources
$identityC = Join-Path $HyperionRoot 'desktop\src\ps5_identity.c'
$identityAsm = Join-Path $HyperionRoot 'desktop\src\ps5_identity_gate.S'

$missing = [Collections.Generic.List[string]]::new()
Require-Directory 'OpenProspero root' $SdkRoot
Require-File 'Clang C compiler' $clang
Require-File 'Clang C++ compiler' $clangxx
Require-File 'LLVM archiver' $archiver
Require-File 'OpenProspero linker' $linker
Require-File 'OpenProspero package CLI' $cli
Require-File 'Vulkan headers' (Join-Path $VulkanIncludeDir 'vulkan\vulkan.h')
Require-File 'Volk header' (Join-Path $VolkDir 'volk.h')
Require-File 'Volk implementation' (Join-Path $VolkDir 'volk.c')
Require-File 'PS5 input header' (Join-Path $sdk 'target\include\openprospero\input.hpp')
Require-File 'OpenProspero platform library' (Join-Path $sdk 'target\lib\libopenprospero_platform.a')
foreach ($source in $sources) { Require-File 'Source' $source }
Require-File 'Hyperion PS5 identity wrappers' $identityC
Require-File 'Hyperion PS5 identity syscall gate' $identityAsm
Require-File 'RADV archive (-RadvArchive)' $RadvArchive
Require-File 'Platform archive (-PlatformArchive)' $PlatformArchive
Require-File 'Compiler builtins (-BuiltinsArchive)' $BuiltinsArchive
foreach ($archive in $ExtraArchives) { Require-File 'Extra archive' $archive }
foreach ($include in $ExtraIncludes) { Require-Directory 'Extra include directory' $include }
foreach ($include in $ExtraSystemIncludes) { Require-Directory 'Extra system include directory' $include }
foreach ($object in $ExtraObjectFiles) { Require-File 'Extra object' $object }
foreach ($font in @('Figtree-400.ttf', 'Figtree-650.ttf', 'fa-solid-900.ttf')) {
    Require-File 'UI font' (Join-Path $project "assets\fonts\$font")
}
if ($SceSysDir) { Require-Directory 'sce_sys artwork' $SceSysDir }
if (-not $StageOnly) {
    Require-File 'Verified libc.prx (-LibcPrx)' $LibcPrx
}

if ($Probe) {
    Write-Host "MirrorPSX PS5 build probe (read only)"
    Write-Host "  source: $mainSource"
    Write-Host "  SDK: $SdkRoot"
    Write-Host "  ImGui: $ImGuiDir"
    Write-Host "  RADV: $RadvArchive"
    Write-Host "  platform: $PlatformArchive"
    Write-Host "  builtins: $BuiltinsArchive"
    Write-Host "  output: $OutputRoot"
    Write-Host "  content ID: $ContentId"
    Write-Host "  mode: $(if ($StageOnly) { 'compile/link/stage' } else { 'compile/link/package/validate' })"
    if ($missing.Count) {
        $missing | ForEach-Object { Write-Host "  MISSING $_" }
        throw "PS5 probe found $($missing.Count) missing inputs."
    }
    Write-Host 'PS5 probe passed: required files are present; compilation, linking and console execution are unverified.'
    return
}
if ($missing.Count) {
    $missing | ForEach-Object { Write-Host "MISSING $_" }
    throw "PS5 build prerequisites incomplete ($($missing.Count) missing files)."
}

# Hyperion's FW9.40 signed, source-built application module is deliberately an
# explicit input. Never silently use the SDK's or the console's unrelated libc.prx.
if (-not $StageOnly) {
    $knownHash = 'E6FF45D16ADF687855CC3B33B0C8A4132B6504360B221E0A34C7E99FB3BA0036'
    $prx = Get-Item -LiteralPath $LibcPrx -Force
    if ($prx.Length -ne 1284674 -or $prx.LinkType -or $prx.Target -or
        (Get-FileHash -LiteralPath $LibcPrx -Algorithm SHA256).Hash -ne $knownHash) {
        throw 'The FW9.40 source-built signed libc.prx does not match the Hyperion validated module.'
    }
}

$resource = (& $clang -print-resource-dir).Trim()
if ($LASTEXITCODE -ne 0 -or -not $resource) { throw 'Clang resource include path is unavailable.' }
$objDir = Join-Path $OutputRoot 'obj'
$application = Join-Path $OutputRoot 'app'
New-Item -ItemType Directory -Path $objDir, $application -Force | Out-Null
$compile = @(
    '--target=x86_64-sie-ps5', '-O2', '-ffreestanding', '-fPIC', '-fPIE',
    '-fvisibility=hidden', '-femulated-tls', '-ffunction-sections', '-fdata-sections',
    '-nostdinc', '-D__SCE__=1', '-DVK_NO_PROTOTYPES=1', '-DIMGUI_IMPL_VULKAN_USE_VOLK=1',
    '-include', (Join-Path $VolkDir 'volk.h'),
    '-isystem', (Join-Path $sdk 'target\cxx\include\c++\v1'),
    '-isystem', (Join-Path $resource 'include'),
    '-isystem', (Join-Path $sdk 'target\cxx\include'),
    '-isystem', (Join-Path $sdk 'target\include'),
    '-isystem', $VulkanIncludeDir,
    '-I', $VolkDir, '-I', $ImGuiDir,
    '-I', (Join-Path $ImGuiDir 'backends'), '-I', (Join-Path $project 'src')
)
foreach ($include in $ExtraIncludes) { $compile += @('-I', $include) }
foreach ($include in $ExtraSystemIncludes) { $compile += @('-isystem', $include) }
foreach ($definition in $ExtraDefines) { $compile += "-D$definition" }
if ($EnableAirPlay) { $compile += '-DMIRRORPSX_PS5_AIRPLAY=1' }
$cxx = $compile + @('-std=c++20', '-fno-exceptions', '-fno-rtti', '-DNDEBUG',
                     '-Wall', '-Wextra', '-Wno-missing-field-initializers')
$objects = @()
$index = 0
foreach ($source in $sources) {
    $object = Join-Path $objDir ('src-' + $index.ToString('D2') + '.o')
    Invoke-Checked $clangxx ($cxx + @('-c', $source, '-o', $object)) "Compile failed: $source"
    $objects += $object
    $index++
}
$volkObj = Join-Path $objDir 'volk.o'
Invoke-Checked $clang ($compile + @('-std=c11', '-c', (Join-Path $VolkDir 'volk.c'), '-o', $volkObj)) 'Volk compilation failed'
$objects += $volkObj
$identityObj = Join-Path $objDir 'ps5_identity.o'
Invoke-Checked $clang ($compile + @('-std=c11', '-I', (Join-Path $HyperionRoot 'desktop\include'),
                             '-c', $identityC, '-o', $identityObj)) 'PS5 identity wrapper compile failed'
$objects += $identityObj
$identityGateObj = Join-Path $objDir 'ps5_identity_gate.o'
Invoke-Checked $clang @('--target=x86_64-sie-ps5', '-fPIC', '-c', $identityAsm, '-o', $identityGateObj) 'PS5 identity syscall gate compile failed'
$objects += $identityGateObj
$objects += $ExtraObjectFiles
$archive = Join-Path $objDir 'libmirrorpsx_ps5.a'
Invoke-Checked $archiver (@('rcs', $archive) + $objects) 'Cannot archive native MirrorPSX objects'

# RADV requires the same extra libc bindings and platform wrappers used by
# Hyperion's PS5 Vulkan host. Keep this contract synchronized with
# Hyperion/scripts/link-vulkan-desktop.ps1 if its pinned RADV version changes.
$bindings = @'
qsort_r mkstemps openlog popen pclose open_memstream __xuname __assert
__memset_chk regcomp regexec regfree regerror localtime_r newlocale freelocale
strtod_l strtof_l dladdr utimensat localeconv_l strtoll_l strtoull_l strtold_l
snprintf_l sscanf_l asprintf_l strcoll_l strxfrm_l strftime_l wcscoll_l wcsxfrm_l
btowc_l wctob_l iswctype_l mbrlen_l mbrtowc_l mbsrtowcs_l mbsnrtowcs_l wcrtomb_l
wcsnrtombs_l mbtowc_l ___mb_cur_max_l ___runetype_l ___tolower_l ___toupper_l
__runes_for_locale catopen catgets catclose backtrace backtrace_symbols_fd
__cxa_thread_atexit_impl arc4random arc4random_buf arc4random_uniform
gmtime_r statvfs fstatvfs futimens clock_nanosleep getaddrinfo freeaddrinfo
if_nameindex if_freenameindex opendir fdopendir readdir rewinddir dirfd closedir
nl_langinfo nl_langinfo_l getpwuid_r posix_fallocate access openat unlinkat
fchmodat fstatat mkdirat renameat memfd_create
'@ -split '\s+' | Where-Object { $_ }
$map = Join-Path $OutputRoot 'radv-platform-local.map'
$lines = @('{', '    local:') + @($bindings | ForEach-Object { "        $_;" }) + @('};')
[IO.File]::WriteAllLines($map, [string[]]$lines, [Text.Encoding]::ASCII)
$elf = Join-Path $application 'application.elf'
$linkArgs = @(
    '--profile', 'application', '--sdk-root', $sdk, '--title-heap', '--no-companion',
    '--', '--gc-sections', '--error-limit=0', '--start-group', $archive,
    '--whole-archive', $RadvArchive, '--no-whole-archive',
    '-lc++', '-lc++abi', '-lunwind', $PlatformArchive, $BuiltinsArchive
) + $ExtraArchives + @(
    '--end-group', '-lSceAgc', '-lSceAgcDriver', '-lSceVideoOut', '-lScePad',
    '-lSceSysmodule', '-lSceUserService', '-lSceSystemService',
    '--wrap=pthread_create', '--wrap=pthread_join', '--wrap=pthread_detach',
    '--wrap=fclose', '--wrap=fflush',
    '--wrap=getuid', '--wrap=geteuid', '--wrap=getgid', '--wrap=getegid'
)
if ($EnableAirPlay) { $linkArgs += '-lSceNet' }
foreach ($name in $bindings) { $linkArgs += "--defsym=$name=ps5_$name" }
$linkArgs += @('--version-script', $map, '-o', $elf)
Invoke-Checked $linker $linkArgs 'OpenProspero application link failed'
Invoke-Checked $cli @('inspect', $elf) 'OpenProspero ELF inspect failed'
Copy-Item -LiteralPath $elf -Destination (Join-Path $application 'eboot.bin')

# A PS5 application ELF has e_type 0xfe10 and e_machine 62 (x86-64).
$header = [byte[]]::new(20)
$reader = [IO.File]::OpenRead($elf)
try { if ($reader.Read($header, 0, 20) -ne 20) { throw 'Truncated PS5 ELF.' } }
finally { $reader.Dispose() }
if ([BitConverter]::ToUInt32($header, 0) -ne 0x464c457f -or
    [BitConverter]::ToUInt16($header, 16) -ne 0xfe10 -or
    [BitConverter]::ToUInt16($header, 18) -ne 62) {
    throw 'Linker output is not an OpenProspero x86-64 application ELF.'
}

$stage = Join-Path $OutputRoot 'package-input'
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $elf -Destination (Join-Path $stage 'eboot.bin')
Copy-Item -LiteralPath (Join-Path $project 'assets') -Destination (Join-Path $stage 'assets') -Recurse
if ($SceSysDir) { Copy-Item -LiteralPath $SceSysDir -Destination (Join-Path $stage 'sce_sys') -Recurse }
if ($StageOnly) {
    Write-Host "PS5 application ELF and staging built: $elf ; $stage"
    Write-Host 'StageOnly requested: PKG was not created.'
    return
}
$module = Join-Path $stage 'sce_module'
New-Item -ItemType Directory -Path $module | Out-Null
Copy-Item -LiteralPath $LibcPrx -Destination (Join-Path $module 'libc.prx')
if ((Get-FileHash -LiteralPath (Join-Path $module 'libc.prx') -Algorithm SHA256).Hash -ne $knownHash) {
    throw 'Staged libc.prx digest mismatch.'
}
$pkgDir = Join-Path $OutputRoot 'pkg'
Invoke-Checked $cli @('package', $stage, $pkgDir, '--content-id', $ContentId,
                      '--title', $Title, '--experimental', '--no-companion',
                      '--version', $PackageVersion) 'OpenProspero PKG generation failed'
$packages = @(Get-ChildItem -LiteralPath $pkgDir -Filter '*.pkg' -File)
if ($packages.Count -ne 1 -or $packages[0].Length -le 0) { throw 'Expected exactly one nonempty PS5 PKG.' }
Invoke-Checked $cli @('validate', $packages[0].FullName) 'PS5 PKG structural validation failed'
$manifest = [ordered]@{
    contentId = $ContentId
    version = $PackageVersion
    package = $packages[0].Name
    packageSha256 = (Get-FileHash -LiteralPath $packages[0].FullName -Algorithm SHA256).Hash
    elfSha256 = (Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash
    radvSha256 = (Get-FileHash -LiteralPath $RadvArchive -Algorithm SHA256).Hash
    libcPrxSha256 = $knownHash
    airPlayEnabled = [bool]$EnableAirPlay
    consoleExecutionVerified = $false
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputRoot 'manifest.json') -Encoding utf8
Write-Host "MirrorPSX experimental PS5 PKG: $($packages[0].FullName)"
Write-Host "SHA-256: $($manifest.packageSha256)"
Write-Host 'Native PKG structural validation passed; real-console launch and Vulkan presentation require console testing.'
