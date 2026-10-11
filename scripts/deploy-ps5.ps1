# MirrorPSX PS5 FTP staging and runtime evidence collection.
# The currently verified ftpsrv.elf protocol does not implement title installation.
# This script deliberately never sends payloads to elfldr or writes to /user/app.
[CmdletBinding(DefaultParameterSetName = 'Probe')]
param(
    [Parameter(ParameterSetName = 'Probe')][switch]$Probe,
    [Parameter(ParameterSetName = 'Stage', Mandatory = $true)][switch]$Stage,
    [Parameter(ParameterSetName = 'Stage', Mandatory = $true)][string]$PackagePath,
    [Parameter(ParameterSetName = 'Stage')][string]$ManifestPath,
    [Parameter(ParameterSetName = 'Capture', Mandatory = $true)][switch]$Capture,
    [Parameter(ParameterSetName = 'Capture')][ValidateRange(3, 300)][int]$CaptureSeconds = 30,
    [Parameter(ParameterSetName = 'Archive', Mandatory = $true)][switch]$Archive,
    [Parameter(ParameterSetName = 'Archive', Mandatory = $true)][string]$ArchiveRoot,
    [string]$Console = $env:MIRRORPSX_PS5_HOST,
    [ValidateRange(1, 65535)][int]$FtpPort = 2121,
    [ValidateRange(1, 65535)][int]$KlogPort = 3232,
    [string]$EvidenceRoot,
    [string]$PythonExe
)

$ErrorActionPreference = 'Stop'
if (-not $Console) {
    throw 'Provide -Console <PS5 address> or set MIRRORPSX_PS5_HOST before probing.'
}
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $EvidenceRoot) { $EvidenceRoot = Join-Path $project 'build-ps5-package\deployment-evidence' }
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$mode = if ($Stage) { 'stage' } elseif ($Capture) { 'capture' } elseif ($Archive) { 'archive' } else { 'probe' }
if ($Archive) { $ArchiveRoot = (Resolve-Path -LiteralPath $ArchiveRoot -ErrorAction Stop).Path }

if (-not $PythonExe) {
    $pythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue
    if (-not $pythonCommand) {
        throw 'Python 3 is required for FTP streaming, hash verification and klog capture; pass -PythonExe.'
    }
    $PythonExe = $pythonCommand.Source
}
if (-not (Test-Path -LiteralPath $PythonExe -PathType Leaf)) {
    throw "Python executable not found: $PythonExe"
}

$expectedHash = ''
$pkgPath = ''
$contentId = ''
$fileName = ''
if ($Stage) {
    $pkgPath = (Resolve-Path -LiteralPath $PackagePath -ErrorAction Stop).Path
    $fileName = [IO.Path]::GetFileName($pkgPath)
    if ($fileName -cnotmatch '^(?<id>[A-Z]{2}[0-9]{4}-MPSX0000[12]_[0-9]{2}-[A-Z0-9]{16})-A[0-9]{4}-V[0-9]{4}\.pkg$') {
        throw 'Refusing upload: only MirrorPSX UI-only or AirPlay experimental title IDs are allowed.'
    }
    $contentId = $Matches.id
    if (-not $ManifestPath) {
        $ManifestPath = Join-Path (Split-Path -Parent (Split-Path -Parent $pkgPath)) 'manifest.json'
    }
    $ManifestPath = (Resolve-Path -LiteralPath $ManifestPath -ErrorAction Stop).Path
    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    $expectedHash = (Get-FileHash -LiteralPath $pkgPath -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($manifest.contentId -cne $contentId -or
        $manifest.package -cne $fileName -or
        $manifest.packageSha256 -cne $expectedHash) {
        throw 'Refusing upload: PKG filename, manifest content ID, package filename or SHA256 do not agree.'
    }
}
if ($Capture) {
    # The first subscriber to klogsrv TCP 3232 can consume the only copies of
    # messages. Do not race Hyperion's existing archiver.
    $recorders = @(Get-CimInstance Win32_Process -ErrorAction Stop |
        Where-Object { $_.CommandLine -match 'capture-console-logs\.py|console-tools\.ps1' -and
                       $_.ProcessId -ne $PID })
    if ($recorders.Count) {
        $ids = ($recorders | ForEach-Object ProcessId) -join ', '
        throw "A PS5 klog collector is already active (PID $ids). Read its evidence instead; a second reader steals messages."
    }
}

# All remote FTP mutations are confined to a new staging directory named
# after the exact package digest. Both control and data connections use Python
# stdlib ftplib. No unverified PS5 installer/control protocol is invoked.
$client = @'
import argparse
import datetime
import ftplib
import hashlib
import json
from pathlib import Path
import re
import socket
import sys
import time

def record(**fields):
    fields["utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    return fields

def ftp_connect(host, port):
    client = ftplib.FTP()
    client.connect(host, port, timeout=8)
    welcome = client.getwelcome()
    if "ftpsrv.elf" not in welcome.lower():
        client.close()
        raise RuntimeError("Unexpected FTP server; refusing console operations: " + welcome[:120])
    client.login()
    return client, welcome

def mlst(client, path):
    try:
        return client.sendcmd("MLST " + path)
    except ftplib.error_perm as ex:
        if str(ex).startswith("550"):
            return None
        raise

def dir_is_safe(client, path):
    reply = mlst(client, path)
    if reply is None:
        return False
    if not re.search(r"(?:^|\s|;)type=dir;", reply, re.I):
        raise RuntimeError("Remote path is not an ordinary directory: " + path)
    client.cwd(path)
    resolved = client.pwd()
    client.cwd("/")
    if resolved.rstrip("/") != path.rstrip("/"):
        raise RuntimeError("Remote directory unexpectedly resolves elsewhere: " + resolved)
    return True

def probe(host, port):
    with ftp_connect(host, port)[0] as client:
        welcome = client.getwelcome()
        features = client.sendcmd("FEAT")
        help_text = client.sendcmd("HELP")
        title_path = "/user/app/MPSX00001"
        title = mlst(client, title_path)
        staging = mlst(client, "/data/mirrorpsx-staging")
        return record(mode="probe", server=welcome, ftpPort=port,
                      titlePath=title_path, titleState="present" if title else "not-visible",
                      stagingState="present" if staging else "not-visible",
                      nativeInstallerAdvertised=bool(re.search(r"\b(?:PKG\s+INSTALL|INSTALL\s+PKG)\b", features + help_text, re.I)),
                      result="read-only network inspection")

def stage(host, port, package_path, digest, content_id):
    package = Path(package_path)
    size = package.stat().st_size
    if size <= 0 or not re.fullmatch(r"[0-9A-F]{64}", digest):
        raise ValueError("Invalid validated PKG hash or zero-sized package")
    folder = "/data/mirrorpsx-staging"
    title_id = content_id.split("-", 1)[1].split("_", 1)[0]
    if title_id not in ("MPSX00001", "MPSX00002"):
        raise RuntimeError("Unexpected MirrorPSX title ID")
    unique = folder + "/" + title_id + "-" + digest[:16]
    temporary = unique + "/" + package.name + ".uploading"
    remote = unique + "/" + package.name
    with ftp_connect(host, port)[0] as client:
        if not dir_is_safe(client, folder):
            client.mkd(folder)
            if not dir_is_safe(client, folder):
                raise RuntimeError("Created staging root not confirmed as ordinary directory")
        if mlst(client, unique):
            raise RuntimeError("Digest-specific remote stage already exists; refusing to overwrite: " + unique)
        client.mkd(unique)
        if not dir_is_safe(client, unique):
            raise RuntimeError("New remote stage directory is not an ordinary directory")
        hasher = hashlib.sha256()
        with package.open("rb") as handle:
            def upload_monitor(block):
                hasher.update(block)
            # Stream file once for upload; ftplib never writes another destination.
            client.storbinary("STOR " + temporary, handle, blocksize=262144, callback=upload_monitor)
        if hasher.hexdigest().upper() != digest:
            raise RuntimeError("Local package mutated during upload; refusing rename")
        received = hashlib.sha256()
        received_bytes = [0]
        def copy_chunk(chunk):
            received.update(chunk)
            received_bytes[0] += len(chunk)
        client.retrbinary("RETR " + temporary, copy_chunk, blocksize=262144)
        if received_bytes[0] != size or received.hexdigest().upper() != digest:
            raise RuntimeError("Remote staged bytes do not match SHA256; partial upload retained at " + temporary)
        if mlst(client, remote):
            raise RuntimeError("Destination already exists; refusing to replace " + remote)
        client.rename(temporary, remote)
        final_size = client.size(remote)
        if final_size != size:
            raise RuntimeError("Final renamed remote PKG has unexpected size")
    return record(mode="stage", contentId=content_id, package=package.name,
                  bytes=size, sha256=digest, remotePath=remote,
                  uploadVerified=True, readbackVerified=True, finalSizeVerified=True,
                  packageInstalled=False, consoleExecuted=False)

def capture(host, ftp_port, klog_port, seconds, output):
    output.mkdir(parents=True, exist_ok=False)
    log_file = output / "klog-raw.log"
    state = record(mode="capture", klogPort=klog_port, seconds=seconds,
                   bytes=0, consoleObserved=False, mirrorpsxLines=0,
                   ftpLogs=[], streamError=None)
    # stderr from the native host generally goes to this console klog stream.
    chunks = []
    deadline = time.monotonic() + seconds
    try:
        with socket.create_connection((host, klog_port), timeout=4) as stream:
            state["consoleObserved"] = True
            stream.settimeout(1)
            with log_file.open("xb") as sink:
                while time.monotonic() < deadline:
                    try:
                        data = stream.recv(65536)
                    except socket.timeout:
                        continue
                    if not data:
                        break
                    sink.write(data)
                    sink.flush()
                    state["bytes"] += len(data)
                    chunks.append(data)
    except OSError as ex:
        state["streamError"] = str(ex)
    if chunks:
        decoded = b"".join(chunks).decode("utf-8", "replace")
        lines = [line for line in decoded.splitlines() if "MirrorPSX" in line]
        (output / "mirrorpsx-filtered.log").write_text("\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")
        state["mirrorpsxLines"] = len(lines)
    # Only fetch project-owned logs, if future builds begin writing them.
    try:
        with ftp_connect(host, ftp_port)[0] as client:
            root = "/data/mirrorpsx/logs"
            if dir_is_safe(client, root):
                for entry in client.nlst(root):
                    name = entry.rsplit("/", 1)[-1]
                    if not re.fullmatch(r"[A-Za-z0-9_-]{1,60}\.log", name):
                        continue
                    remote = root + "/" + name
                    meta = mlst(client, remote)
                    if not meta or not re.search(r"(?:^|\s|;)type=file;", meta, re.I):
                        continue
                    length = client.size(remote)
                    if length is None or length > 8 * 1024 * 1024:
                        continue
                    path = output / name
                    with path.open("xb") as sink:
                        client.retrbinary("RETR " + remote, sink.write)
                    state["ftpLogs"].append({"path": remote, "bytes": path.stat().st_size,
                                             "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    except ftplib.all_errors as ex:
        state["ftpLogsError"] = str(ex)
    state["utcFinished"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    return state

def archive(root, output):
    output.mkdir(parents=True, exist_ok=False)
    # Inspect only MirrorPSX messages. Archive files belong to the already
    # running Hyperion collector; never open another console klog connection.
    logs = sorted(root.glob("klog-*.log"), key=lambda p: p.name)
    if not logs:
        raise RuntimeError("No klog-*.log files exist in the selected archive")
    selected = logs[-3:]
    found = []
    files = []
    for path in selected:
        with path.open("rb") as handle:
            size = path.stat().st_size
            handle.seek(max(0, size - 4 * 1024 * 1024))
            excerpt = handle.read(4 * 1024 * 1024)
        if size > 4 * 1024 * 1024:
            excerpt = excerpt.partition(b"\n")[2]
        lines = excerpt.decode("utf-8", "replace").splitlines()
        found += [line for line in lines if "MirrorPSX" in line]
        files.append({"name": path.name, "sizeAtCapture": size,
                      "scannedTailBytes": len(excerpt)})
    (output / "mirrorpsx-filtered.log").write_text(
        "\n".join(found) + ("\n" if found else ""), encoding="utf-8")
    return record(mode="archive", source=str(root), files=files,
                  mirrorpsxLines=len(found), createsConsoleConnection=False,
                  sourceIsLiveArchive=True)

if __name__ == "__main__":
    args = argparse.ArgumentParser()
    args.add_argument("--mode", choices=("probe", "stage", "capture", "archive"), required=True)
    args.add_argument("--host", required=True)
    args.add_argument("--ftp-port", type=int, required=True)
    args.add_argument("--klog-port", type=int, required=True)
    args.add_argument("--package", default="")
    args.add_argument("--digest", default="")
    args.add_argument("--content-id", default="")
    args.add_argument("--seconds", type=int, default=30)
    args.add_argument("--output", default="")
    args.add_argument("--archive-root", default="")
    opts = args.parse_args()
    try:
        if opts.mode == "probe":
            report = probe(opts.host, opts.ftp_port)
        elif opts.mode == "stage":
            report = stage(opts.host, opts.ftp_port, opts.package, opts.digest, opts.content_id)
        elif opts.mode == "archive":
            report = archive(Path(opts.archive_root), Path(opts.output))
        else:
            report = capture(opts.host, opts.ftp_port, opts.klog_port, opts.seconds, Path(opts.output))
        print(json.dumps(report, indent=2))
    except Exception as ex:
        print(type(ex).__name__ + ": " + str(ex), file=sys.stderr)
        sys.exit(1)
'@

$argumentList = @(
    '--mode', $mode, '--host', $Console,
    '--ftp-port', [string]$FtpPort, '--klog-port', [string]$KlogPort,
    '--package', $pkgPath, '--digest', $expectedHash,
    '--content-id', $contentId, '--seconds', [string]$CaptureSeconds,
    '--archive-root', [string]$ArchiveRoot
)
$runDirectory = $null
if ($Stage -or $Capture -or $Archive) {
    New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null
    $runDirectory = Join-Path $EvidenceRoot ('mirrorpsx-' + (Get-Date -AsUTC -Format 'yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
}
if ($Capture -or $Archive) { $argumentList += @('--output', $runDirectory) }
$outputLines = @(& $PythonExe -c $client @argumentList)
if ($LASTEXITCODE -ne 0) {
    throw "MirrorPSX console $mode failed (Python exit $LASTEXITCODE)."
}
$jsonText = $outputLines -join "`n"
$report = $jsonText | ConvertFrom-Json
if ($Stage) {
    New-Item -ItemType Directory -Path $runDirectory | Out-Null
    $record = [ordered]@{
        contentId = $contentId
        package = $fileName
        localSha256 = $expectedHash
        manifestPath = $ManifestPath
        remotePath = $report.remotePath
        stageVerified = $report.readbackVerified
        packageInstalled = $false
        runtimeVerified = $false
        result = $report
    }
    $record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runDirectory 'staging.json') -Encoding utf8
    Write-Host "MirrorPSX PKG uploaded and SHA256 read-back verified: $($report.remotePath)"
    Write-Host "Deployment evidence: $runDirectory"
    Write-Host 'Console title installation requires a separately confirmed package installer.'
} elseif ($Capture -or $Archive) {
    $jsonText | Set-Content -LiteralPath (Join-Path $runDirectory 'capture.json') -Encoding utf8
    if ($Archive) {
        Write-Host "Existing Hyperion klog archive examined without opening a second collector; MirrorPSX lines: $($report.mirrorpsxLines)"
    } else {
        Write-Host "MirrorPSX klog captured: $($report.bytes) bytes; MirrorPSX lines: $($report.mirrorpsxLines)"
    }
    Write-Host "Evidence: $runDirectory"
} else {
    Write-Host $jsonText
    Write-Host 'Probe is read-only. The FTP server does not advertise a trusted package installation interface.'
}
