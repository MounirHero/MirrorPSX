param(
    [string]$SdkRoot = "../sdk",
    [string]$BuildDirectory = "build/ps5",
    [ValidatePattern('^[0-9]{2}\.[0-9]{2}$')][string]$Version = '10.00',
    [string]$OutputDirectory = 'dist',
    [string]$ProsperoPath = ''
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Push-Location $root
try {
    $sdk = (Resolve-Path -LiteralPath $SdkRoot).Path
    $prospero = if ($ProsperoPath) { (Resolve-Path -LiteralPath $ProsperoPath).Path } else { Join-Path $sdk 'build/bin/Release/prospero.exe' }
    if (-not (Test-Path -LiteralPath $prospero)) { throw 'OpenProspero packager missing. Build the SDK tools or pass -ProsperoPath.' }
    $elf = (Resolve-Path -LiteralPath "$BuildDirectory/MirrorPSX.elf").Path
    $receipt = Join-Path $BuildDirectory 'release-build.json'
    if (-not (Test-Path -LiteralPath $receipt)) { throw 'Native build receipt missing. Rebuild the shared broker and PS5 app before packaging.' }
    $broker = Join-Path $root 'dist/OpenProspero-SharedBroker.elf'
    $brokerLicense = Join-Path $root 'dist/OpenProspero-SharedBroker-LICENSE.txt'
    if (-not (Test-Path -LiteralPath $broker) -or -not (Test-Path -LiteralPath $brokerLicense)) { throw 'Matching external companion artifacts missing. Run tools/build-shared-broker.ps1.' }
    $releaseRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $root $OutputDirectory }
    $releaseDirectory = Join-Path $releaseRoot "ps5-v$Version"
    if (Test-Path -LiteralPath $releaseDirectory) { throw 'Release directory already exists. Choose a new version or output directory.' }
    $runtime = Join-Path $root 'build/ps5-runtime/libc.prx'
    $expectedRuntimeHash = 'E6FF45D16ADF687855CC3B33B0C8A4132B6504360B221E0A34C7E99FB3BA0036'
    if (-not (Test-Path -LiteralPath $runtime)) {
        & wsl.exe -d Debian bash tools/build-ps5-runtime.sh
        if ($LASTEXITCODE -ne 0) { throw 'PS5 runtime build failed' }
    }
    if ((Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash -ne $expectedRuntimeHash) {
        throw 'PS5 runtime checksum mismatch'
    }
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fffffff'
    $app = Join-Path $root "build/ps5-package-$stamp/app"
    $output = Join-Path $root "build/ps5-package-$stamp/package"
    New-Item -ItemType Directory -Path "$app/sce_sys" -Force | Out-Null
    New-Item -ItemType Directory -Path "$app/sce_module" -Force | Out-Null
    Copy-Item -LiteralPath $runtime -Destination "$app/sce_module/libc.prx"
    if ((Get-FileHash -LiteralPath "$app/sce_module/libc.prx" -Algorithm SHA256).Hash -ne $expectedRuntimeHash) {
        throw 'Packaged PS5 runtime checksum mismatch'
    }
    Copy-Item -LiteralPath 'assets' -Destination $app -Recurse
    Copy-Item -LiteralPath 'THIRD_PARTY_NOTICES.md' -Destination $app
    Copy-Item -LiteralPath 'LICENSE' -Destination "$app/LICENSE"
    & $prospero self $elf "$app/eboot.bin" --signed-debug
    if ($LASTEXITCODE -ne 0) { throw 'PS5 SELF creation failed' }
    $env:MIRROR_PACKAGE_APP = $app
    $env:MIRROR_PACKAGE_VERSION = $Version
    @'
import os, json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
app=Path(os.environ['MIRROR_PACKAGE_APP'])
version=os.environ['MIRROR_PACKAGE_VERSION']
param={
 'titleId':'PPSA99221','conceptId':'99221',
 'contentId':'UP9000-PPSA99221_00-MIRRORPSX0000001',
 'contentVersion':version[:2]+'.000.000','masterVersion':version,
 'applicationCategoryType':0,'applicationDrmType':'free',
 'contentBadgeType':1,'attribute':0,'attribute2':0,'attribute3':0,
 'downloadDataSize':0,'kernel':{'flexibleMemorySize':1073741824},
 'localizedParameters':{'defaultLanguage':'it-IT',
   'it-IT':{'titleName':'MirrorPSX'},'en-US':{'titleName':'MirrorPSX'}},
 'gameIntent':{'permittedIntents':[{'intentType':'launchActivity'}]}
}
(app/'sce_sys/param.json').write_text(json.dumps(param,indent=2),encoding='utf-8')
icon=Image.new('RGB',(512,512),(9,14,24))
draw=ImageDraw.Draw(icon)
draw.rounded_rectangle((100,105,412,302),radius=20,outline=(230,239,252),width=9)
draw.line((200,327,312,327),fill=(139,192,239),width=9)
draw.line((256,303,256,327),fill=(139,192,239),width=9)
font=ImageFont.truetype('assets/ui/fonts/Inter-SemiBold.ttf',44)
draw.text((256,372),'MirrorPSX',font=font,anchor='mm',fill=(230,239,252))
icon.save(app/'sce_sys/icon0.png')
'@ | python -
    if ($LASTEXITCODE -ne 0) { throw 'PS5 assets preparation failed' }
    & $prospero package $app $output --content-id 'UP9000-PPSA99221_00-MIRRORPSX0000001' --title 'MirrorPSX' --version $Version --no-companion --experimental
    if ($LASTEXITCODE -ne 0) { throw 'PS5 packaging failed' }
    $pkg = @(Get-ChildItem -LiteralPath $output -Filter '*.pkg' -Recurse)
    if ($pkg.Count -ne 1) { throw 'Expected one PS5 package' }
    & $prospero validate $pkg[0].FullName
    if ($LASTEXITCODE -ne 0) { throw 'PS5 package structural validation failed' }
    $verified = Join-Path $root "build/ps5-package-$stamp/verified"
    & $prospero extract $pkg[0].FullName $verified --include-metadata
    if ($LASTEXITCODE -ne 0) { throw 'PS5 package extraction verification failed' }
    $extractedRuntime = Join-Path $verified 'sce_module/libc.prx'
    if (-not (Test-Path -LiteralPath $extractedRuntime) -or
        (Get-FileHash -LiteralPath $extractedRuntime -Algorithm SHA256).Hash -ne $expectedRuntimeHash) {
        throw 'Final PKG does not contain the required verified libc.prx'
    }
    if ((Get-FileHash -LiteralPath "$verified/eboot.bin" -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath "$app/eboot.bin" -Algorithm SHA256).Hash) {
        throw 'Final PKG executable does not match the signed build'
    }
    # Export from the re-extracted PKG: folder users receive identical title bytes.
    & python tools/ps5-release.py export --app $verified --pkg $pkg[0].FullName `
        --broker $broker --broker-license $brokerLicense --receipt $receipt --elf $elf `
        --output $releaseDirectory --version $Version
    if ($LASTEXITCODE -ne 0) { throw 'PS5 dual-format release export failed' }
    Write-Host "PS5 PKG, folder ZIP and external companion: $releaseDirectory"
} finally {
    Remove-Item Env:MIRROR_PACKAGE_APP -ErrorAction SilentlyContinue
    Remove-Item Env:MIRROR_PACKAGE_VERSION -ErrorAction SilentlyContinue
    Pop-Location
}





