param([string]$SdkRoot='../sdk')
$ErrorActionPreference='Stop'
$repoRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sdkRootPath=(Resolve-Path -LiteralPath $SdkRoot).Path
# One per-user credential shared by all clients built on this workstation.
# Neither credential nor generated header belongs in source control.
$configBase=if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { Join-Path ([Environment]::GetFolderPath('UserProfile')) '.config' }
$configRoot=Join-Path $configBase 'OpenProspero/shared-broker'
New-Item -ItemType Directory -Force -Path $configRoot | Out-Null
$tokenPath=Join-Path $configRoot 'token.txt'
if ($env:MIRROR_BROKER_TOKEN) {
    $tokenText=$env:MIRROR_BROKER_TOKEN.Trim()
} elseif (-not (Test-Path -LiteralPath $tokenPath)) {
    $bytes=New-Object byte[] 4
    do { [Security.Cryptography.RandomNumberGenerator]::Fill($bytes)
         $token=[BitConverter]::ToUInt32($bytes,0) } while ($token -eq 0)
    [IO.File]::WriteAllText($tokenPath,$token.ToString('x8'))
    $tokenText=$token.ToString('x8')
} else {
    $tokenText=[IO.File]::ReadAllText($tokenPath).Trim()
}
if ($tokenText -notmatch '^[0-9a-fA-F]{8}$' -or [Convert]::ToUInt32($tokenText,16) -eq 0) {
    throw 'Invalid shared broker credential'
}
$generated=Join-Path $repoRoot 'build/broker-config'
New-Item -ItemType Directory -Force -Path $generated | Out-Null
[IO.File]::WriteAllText((Join-Path $generated 'broker_config.hpp'),"#pragma once`n#define MIRROR_BROKER_TOKEN 0x$($tokenText)u`n")
$output=Join-Path $repoRoot 'build/shared-broker'
if ($IsLinux -or $IsMacOS) {
    & sh (Join-Path $sdkRootPath 'tools/build-companion.sh') $output --allow-privileged --lan "--companion-token=$tokenText"
} else {
    & (Join-Path $sdkRootPath 'tools/build-companion.ps1') -Output $output -AllowPrivileged -Lan -CompanionToken $tokenText
}
if ($LASTEXITCODE -ne 0) { throw 'Shared broker build failed' }
$dist=Join-Path $repoRoot 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item -LiteralPath (Join-Path $output 'openprospero-companion.elf') -Destination (Join-Path $dist 'OpenProspero-SharedBroker.elf')
Copy-Item -LiteralPath (Join-Path $sdkRootPath 'LICENSE') -Destination (Join-Path $dist 'OpenProspero-SharedBroker-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs/SHARED_BROKER.md') -Destination (Join-Path $dist 'OpenProspero-SharedBroker-README.md')
@{
    schema=1
    elf_sha256=(Get-FileHash -LiteralPath (Join-Path $dist 'OpenProspero-SharedBroker.elf') -Algorithm SHA256).Hash.ToLowerInvariant()
    config_sha256=(Get-FileHash -LiteralPath (Join-Path $generated 'broker_config.hpp') -Algorithm SHA256).Hash.ToLowerInvariant()
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $generated 'broker.json') -Encoding utf8
Write-Output 'Shared external broker ready. Credential retained outside the repository.'
