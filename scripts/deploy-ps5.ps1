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
    [Parameter(ParameterSetName = 'Archive')][string]$BaselinePath,
    [Parameter(ParameterSetName = 'Verify', Mandatory = $true)][switch]$VerifyInstall,
    [Parameter(ParameterSetName = 'Verify')][ValidateSet('MPSX00001', 'MPSX00002')][string]$TitleId = 'MPSX00001',
    [Parameter(ParameterSetName = 'Verify')][string]$ExpectedContentId,
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
$mode = if ($Stage) { 'stage' } elseif ($Capture) { 'capture' } elseif ($Archive) { 'archive' } elseif ($VerifyInstall) { 'verify' } else { 'probe' }
if ($Archive) { $ArchiveRoot = (Resolve-Path -LiteralPath $ArchiveRoot -ErrorAction Stop).Path }
if ($BaselinePath) { $BaselinePath = (Resolve-Path -LiteralPath $BaselinePath -ErrorAction Stop).Path }
if ($VerifyInstall -and $ExpectedContentId -and
    $ExpectedContentId -cnotmatch "^[A-Z]{2}[0-9]{4}-${TitleId}_[0-9]{2}-[A-Z0-9]{16}$") {
    throw "ExpectedContentId must contain exactly the selected MirrorPSX title ID $TitleId."
}

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
        airplay_title = mlst(client, "/user/app/MPSX00002")
        staging = mlst(client, "/data/mirrorpsx-staging")
        etahen_config = mlst(client, "/data/etaHEN/config.ini")
        # etaHEN documents optional DPI v1 on 9090 and v2 WebUI on 12800.
        # A TCP handshake cannot authenticate their implementation and never
        # authorizes installing a package; do not send protocol requests.
        installer_ports = []
        for name, target_port in (("etaHEN-DPI-v1", 9090), ("etaHEN-DPI-v2-WebUI", 12800)):
            try:
                with socket.create_connection((host, target_port), timeout=1.5):
                    reachable = True
            except OSError:
                reachable = False
            installer_ports.append({"name": name, "port": target_port,
                                    "tcpReachable": reachable,
                                    "serviceIdentityVerified": False,
                                    "installAuthorized": False})
        return record(mode="probe", server=welcome, ftpPort=port,
                      titlePath=title_path, titleState="present" if title else "not-visible",
                      airplayTitleState="present" if airplay_title else "not-visible",
                      stagingState="present" if staging else "not-visible",
                      etaHenConfigVisible=etahen_config is not None,
                      installerCandidates=installer_ports,
                      nativeInstallerAdvertised=bool(re.search(r"\b(?:PKG\s+INSTALL|INSTALL\s+PKG)\b", features + help_text, re.I)),
                      result="read-only network inspection")

def inspect_install(host, port, title_id, expected_content_id):
    if title_id not in ("MPSX00001", "MPSX00002"):
        raise ValueError("Only MirrorPSX title IDs are allowed")
    app = "/user/app/" + title_id
    appmeta = "/system_data/priv/appmeta/" + title_id
    paths = [app, appmeta]
    metadata_paths = [app + "/sce_sys/param.json",
                      appmeta + "/param.json"]
    evidence = []
    metadata = []
    with ftp_connect(host, port)[0] as client:
        for path in paths + metadata_paths:
            detail = mlst(client, path)
            kind = None
            if detail:
                found = re.search(r"(?:^|\s|;)type=([^;]+);", detail, re.I)
                kind = found.group(1).lower() if found else "unknown"
            evidence.append({"path": path, "visible": detail is not None, "type": kind})
            if path not in metadata_paths or kind != "file":
                continue
            try:
                length = client.size(path)
                if length is None or length > 65536 or length < 2:
                    metadata.append({"path": path, "parsed": False, "reason": "invalid-size"})
                    continue
                blocks = []
                client.retrbinary("RETR " + path, blocks.append)
                data = json.loads(b"".join(blocks).decode("utf-8-sig"))
                def value_for_key(value, key):
                    if isinstance(value, dict):
                        for name, entry in value.items():
                            if name.lower() == key.lower() and isinstance(entry, str):
                                return entry
                            nested = value_for_key(entry, key)
                            if nested is not None:
                                return nested
                    if isinstance(value, list):
                        for entry in value:
                            nested = value_for_key(entry, key)
                            if nested is not None:
                                return nested
                    return None
                found_title = value_for_key(data, "titleId")
                found_content = value_for_key(data, "contentId")
                title_matches = found_title == title_id
                content_matches = found_content == expected_content_id if expected_content_id else None
                metadata.append({"path": path, "parsed": True,
                                 "titleId": found_title, "contentId": found_content,
                                 "titleMatches": title_matches,
                                 "contentMatches": content_matches})
            except (ftplib.all_errors, ValueError, UnicodeError) as ex:
                metadata.append({"path": path, "parsed": False,
                                 "reason": type(ex).__name__})
    matched = any(row.get("titleMatches") and
                  (not expected_content_id or row.get("contentMatches"))
                  for row in metadata)
    any_visible = any(row["visible"] for row in evidence[:2])
    status = ("metadata-identity-match" if matched else
              "candidate-path-visible-unconfirmed" if any_visible else
              "not-visible-in-checked-paths")
    return record(mode="verify", titleId=title_id,
                  expectedContentId=expected_content_id or None,
                  status=status, metadataIdentityMatches=matched,
                  titleRuntimeVerified=False, consoleLaunchObserved=False,
                  remoteWrites=False, paths=evidence, metadata=metadata,
                  caveat="FTP path visibility alone does not prove installed, launchable or running title")

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

def archive(root, output, baseline_path):
    output.mkdir(parents=True, exist_ok=False)
    # A cursor records every archived klog byte offset. A later invocation
    # reads only the bytes appended since this checkpoint, across rotations.
    # Never open a second socket to the PS5 or write into an active archive.
    root = root.resolve(strict=True)
    if not root.is_dir():
        raise RuntimeError("Log archive must be an existing directory")
    baseline = None
    old = {}
    if baseline_path:
        baseline = json.loads(baseline_path.read_text(encoding="utf-8-sig"))
        if baseline.get("schema") != 1 or baseline.get("root") != str(root):
            raise RuntimeError("Archive cursor does not match the selected log directory")
        if not isinstance(baseline.get("offsets"), dict):
            raise RuntimeError("Invalid archive cursor offsets")
        old = baseline["offsets"]
    logs = sorted((p for p in root.glob("klog-*.log")
                   if p.is_file() and p.resolve().is_relative_to(root)),
                  key=lambda p: p.name)
    if not logs:
        raise RuntimeError("No klog-*.log files exist in the selected archive")
    found = []
    files = []
    offsets = {}
    exceeded = False
    for path in logs:
        size = path.stat().st_size
        offsets[path.name] = size
        if baseline is None and path not in logs[-3:]:
            continue
        start = old.get(path.name, 0) if baseline is not None else max(0, size - 4 * 1024 * 1024)
        if not isinstance(start, int) or start < 0:
            raise RuntimeError("Invalid cursor offset for " + path.name)
        if start > size:
            raise RuntimeError("Archived log was truncated since baseline: " + path.name)
        read_limit = 16 * 1024 * 1024
        if size - start > read_limit:
            # Never silently discard messages from a log that grew too much.
            exceeded = True
            files.append({"name": path.name, "unreadBytes": size - start,
                          "error": "per-file-read-limit-exceeded"})
            continue
        with path.open("rb") as handle:
            handle.seek(start)
            excerpt = handle.read(size - start)
        if start > 0 and baseline is None:
            excerpt = excerpt.partition(b"\n")[2]
        lines = excerpt.decode("utf-8", "replace").splitlines()
        found += [line for line in lines if "mirrorpsx" in line.lower()]
        files.append({"name": path.name, "sizeAtCapture": size,
                      "startOffset": start, "scannedBytes": len(excerpt)})
    if exceeded:
        raise RuntimeError("Some archive deltas exceeded 16 MiB; preserve original logs and create a more recent cursor")
    (output / "mirrorpsx-filtered.log").write_text(
        "\n".join(found) + ("\n" if found else ""), encoding="utf-8")
    cursor = {"schema": 1, "root": str(root), "offsets": offsets,
              "utc": datetime.datetime.now(datetime.timezone.utc).isoformat()}
    (output / "cursor.json").write_text(json.dumps(cursor, indent=2), encoding="utf-8")
    checkpoints = {"started": False, "display": False, "firstFrameSubmitted": False,
                   "exitRequested": False, "vulkanFailure": False}
    for line in found:
        lower = line.lower()
        checkpoints["started"] |= "starting" in lower
        checkpoints["display"] |= "display" in lower
        checkpoints["firstFrameSubmitted"] |= (
            "first vulkan frame submitted for presentation" in lower or
            "first vulkan frame presented" in lower)
        checkpoints["exitRequested"] |= "exit requested" in lower
        checkpoints["vulkanFailure"] |= ("failed" in lower or "error" in lower)
    return record(mode="archive", source=str(root), files=files,
                  baselinePath=str(baseline_path) if baseline_path else None,
                  cursored=True, checkpoints=checkpoints,
                  mirrorpsxLines=len(found), createsConsoleConnection=False,
                  sourceIsLiveArchive=True)

if __name__ == "__main__":
    args = argparse.ArgumentParser()
    args.add_argument("--mode", choices=("probe", "stage", "capture", "archive", "verify"), required=True)
    args.add_argument("--host", required=True)
    args.add_argument("--ftp-port", type=int, required=True)
    args.add_argument("--klog-port", type=int, required=True)
    args.add_argument("--package", default="")
    args.add_argument("--digest", default="")
    args.add_argument("--content-id", default="")
    args.add_argument("--seconds", type=int, default=30)
    args.add_argument("--output", default="")
    args.add_argument("--archive-root", default="")
    args.add_argument("--baseline", default="")
    args.add_argument("--title-id", default="MPSX00001")
    args.add_argument("--expected-content-id", default="")
    opts = args.parse_args()
    try:
        if opts.mode == "probe":
            report = probe(opts.host, opts.ftp_port)
        elif opts.mode == "stage":
            report = stage(opts.host, opts.ftp_port, opts.package, opts.digest, opts.content_id)
        elif opts.mode == "archive":
            report = archive(Path(opts.archive_root), Path(opts.output),
                             Path(opts.baseline) if opts.baseline else None)
        elif opts.mode == "verify":
            report = inspect_install(opts.host, opts.ftp_port,
                                     opts.title_id, opts.expected_content_id)
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
    '--archive-root', [string]$ArchiveRoot,
    '--baseline', [string]$BaselinePath,
    '--title-id', [string]$TitleId,
    '--expected-content-id', [string]$ExpectedContentId
)
$runDirectory = $null
if ($Stage -or $Capture -or $Archive -or $VerifyInstall) {
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
} elseif ($VerifyInstall) {
    New-Item -ItemType Directory -Path $runDirectory | Out-Null
    $jsonText | Set-Content -LiteralPath (Join-Path $runDirectory 'post-install.json') -Encoding utf8
    Write-Host $jsonText
    Write-Host "Read-only post-install evidence: $runDirectory"
    Write-Host 'Only console launch and an actual picture/controller test can confirm working runtime.'
} elseif ($Capture -or $Archive) {
    $jsonText | Set-Content -LiteralPath (Join-Path $runDirectory 'capture.json') -Encoding utf8
    if ($Archive) {
        Write-Host "Existing Hyperion klog archive examined without opening a second collector; MirrorPSX lines: $($report.mirrorpsxLines)"
        Write-Host "Reusable post-launch cursor: $(Join-Path $runDirectory 'cursor.json')"
    } else {
        Write-Host "MirrorPSX klog captured: $($report.bytes) bytes; MirrorPSX lines: $($report.mirrorpsxLines)"
    }
    Write-Host "Evidence: $runDirectory"
} else {
    Write-Host $jsonText
    Write-Host 'Probe is read-only. The FTP server does not advertise a trusted package installation interface.'
}
