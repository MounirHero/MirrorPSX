param([string]$SdkRoot = "../sdk", [string]$BuildDirectory = "build/ps5")
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Push-Location $root
try {
    $sdk = (Resolve-Path -LiteralPath $SdkRoot).Path
    $prospero = Join-Path $sdk 'build/bin/Release/prospero.exe'
    $elf = (Resolve-Path -LiteralPath "$BuildDirectory/MirrorPSX.elf").Path
    $runtime = Join-Path $root 'build/ps5-runtime/libc.prx'
    $expectedRuntimeHash = 'E6FF45D16ADF687855CC3B33B0C8A4132B6504360B221E0A34C7E99FB3BA0036'
    if (-not (Test-Path -LiteralPath $runtime)) {
        & wsl.exe -d Debian bash tools/build-ps5-runtime.sh
        if ($LASTEXITCODE -ne 0) { throw 'PS5 runtime build failed' }
    }
    if ((Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash -ne $expectedRuntimeHash) {
        throw 'PS5 runtime checksum mismatch'
    }
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
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
    Copy-Item -LiteralPath 'reference/ps5-homebrew-ui/LICENSE' -Destination "$app/LICENSE"
    & $prospero self $elf "$app/eboot.bin" --signed-debug
    if ($LASTEXITCODE -ne 0) { throw 'PS5 SELF creation failed' }
    $env:MIRROR_PACKAGE_APP = $app
    @'
import os, json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
app=Path(os.environ['MIRROR_PACKAGE_APP'])
param={
 'titleId':'PPSA99221','conceptId':'99221',
 'contentId':'UP9000-PPSA99221_00-MIRRORPSX0000001',
 'contentVersion':'10.000.000','masterVersion':'10.00',
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
    & $prospero package $app $output --content-id 'UP9000-PPSA99221_00-MIRRORPSX0000001' --title 'MirrorPSX' --version '10.00' --no-companion --experimental
    if ($LASTEXITCODE -ne 0) { throw 'PS5 packaging failed' }
    $pkg = @(Get-ChildItem -LiteralPath $output -Filter '*.pkg' -Recurse)
    if ($pkg.Count -ne 1) { throw 'Expected one PS5 package' }
    & $prospero validate $pkg[0].FullName
    if ($LASTEXITCODE -ne 0) { throw 'PS5 package structural validation failed' }
    $verified = Join-Path $root "build/ps5-package-$stamp/verified"
    & $prospero extract $pkg[0].FullName $verified
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
    New-Item -ItemType Directory -Path 'dist' -Force | Out-Null
    $destination = Join-Path $root 'dist/MirrorPSX-PS5-test-r10.pkg'
    Copy-Item -LiteralPath $pkg[0].FullName -Destination $destination -Force
    Get-FileHash -LiteralPath $destination -Algorithm SHA256 | Format-List
    Write-Host "PS5 test package: $destination"
} finally {
    Remove-Item Env:MIRROR_PACKAGE_APP -ErrorAction SilentlyContinue
    Pop-Location
}





