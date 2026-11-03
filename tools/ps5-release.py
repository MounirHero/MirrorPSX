#!/usr/bin/env python3
"""Export the validated PS5 title as PKG + folder ZIP with an external broker."""
import argparse
import hashlib
import json
import re
import shutil
import tempfile
import zipfile
from pathlib import Path

TITLE_ID = "PPSA99221"
RUNTIME_SHA = "e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036"


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def record_build(elf, config, broker_manifest, output):
    broker = json.loads(Path(broker_manifest).read_text(encoding="utf-8-sig"))
    if sha(config) != broker["config_sha256"]:
        raise ValueError("Broker configuration changed: rebuild the shared broker first")
    Path(output).write_text(json.dumps({
        "schema": 1, "elf_sha256": sha(elf),
        "broker_sha256": broker["elf_sha256"],
    }, indent=2) + "\n", encoding="utf-8")


def export_release(app, pkg, broker, broker_license, receipt, elf, output, version):
    if not re.fullmatch(r"[0-9]{2}\.[0-9]{2}", version):
        raise ValueError("Version must use the native NN.NN format")
    app, output = Path(app).resolve(), Path(output).resolve()
    if output.exists():
        raise ValueError("Release directory already exists; choose a new version or output")
    if not app.is_dir() or output == app or app in output.parents or output in app.parents:
        raise ValueError("Release output must be separate from the staged title")
    required = ["eboot.bin", "sce_sys/param.json", "sce_sys/icon0.png",
                "sce_module/libc.prx", "assets/ui/atlas.rgba",
                "assets/ui/fonts/Inter-Regular.ttf", "assets/ui/fonts/Inter-SemiBold.ttf",
                "LICENSE", "THIRD_PARTY_NOTICES.md"]
    for name in required:
        if not (app / name).is_file():
            raise ValueError(f"Missing title file: {name}")
    if sha(app / "sce_module/libc.prx") != RUNTIME_SHA:
        raise ValueError("Title runtime checksum mismatch")
    param = json.loads((app / "sce_sys/param.json").read_text(encoding="utf-8"))
    if (param.get("titleId") != TITLE_ID or param.get("masterVersion") != version
            or param.get("contentVersion") != version[:2] + ".000.000"):
        raise ValueError("Title identity/version does not match the release")
    build = json.loads(Path(receipt).read_text(encoding="utf-8"))
    if build["elf_sha256"] != sha(elf) or build["broker_sha256"] != sha(broker):
        raise ValueError("App and external broker do not match the native build receipt")
    if Path(broker).read_bytes()[:4] != b"\x7fELF":
        raise ValueError("External broker must be a payload ELF")
    if not Path(pkg).is_file() or not Path(broker_license).is_file():
        raise ValueError("Validated PKG and broker license are required")
    files = []
    for path in sorted(app.rglob("*")):
        if path.is_symlink():
            raise ValueError("Symlinks are not allowed in the title archive")
        if not path.is_file():
            continue
        name = path.relative_to(app).as_posix()
        if path.suffix.lower() == ".key" or name.startswith("openprospero/") or path.name in {
                "token.txt", "broker_config.hpp", "pairing.key", "mirrorpsx.key"}:
            raise ValueError(f"Private/runtime companion file in title: {name}")
        files.append((path, f"{TITLE_ID}/{name}"))
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".mirror-release-", dir=output.parent) as temporary:
        stage = Path(temporary) / "release"
        stage.mkdir()
        base = f"MirrorPSX-PS5-v{version}"
        archive = stage / (base + ".zip")
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zipped:
            for path, name in files:
                zipped.write(path, name)
        # Verify all assets and executable bytes, not just the ZIP directory.
        with zipfile.ZipFile(archive) as zipped:
            if zipped.testzip() or set(zipped.namelist()) != {name for _, name in files}:
                raise ValueError("Title ZIP verification failed")
            for path, name in files:
                with zipped.open(name) as stream:
                    if hashlib.file_digest(stream, "sha256").hexdigest() != sha(path):
                        raise ValueError(f"Title ZIP content mismatch: {name}")
        shutil.copy2(pkg, stage / (base + ".pkg"))
        shutil.copy2(broker, stage / "OpenProspero-SharedBroker.elf")
        shutil.copy2(broker_license, stage / "OpenProspero-SharedBroker-LICENSE.txt")
        (stage / "INSTALL.txt").write_text(
            "MirrorPSX PS5 experimental release\n\n"
            "1. Load the matching OpenProspero-SharedBroker.elf once per console boot.\n"
            "2. Choose ONE installation method: install the PKG, OR extract the ZIP\n"
            f"   and copy {TITLE_ID}/ to /data/homebrew/{TITLE_ID}/ for ShadowMountPlus.\n"
            "3. Start MirrorPSX from the home screen; keep sender and console on the same LAN.\n"
            "Do not keep PKG and folder installations of this title active together.\n"
            "Close the app before updating. Keep /data/mirrorpsx/ (pairing keys).\n"
            "The external broker is shared by compatible apps; MirrorPSX does not stop it.\n"
            "No ELF installer or automatic updater is included.\n"
            "Folder-launch compatibility and streaming still need hardware testing.\n",
            encoding="utf-8")
        manifest = {"schema": 1, "version": version, "title_id": TITLE_ID,
                    "experimental": True, "external_companion_required": True,
                    "folder_destination": f"/data/homebrew/{TITLE_ID}",
                    "files": [{"name": p.name, "size": p.stat().st_size, "sha256": sha(p)}
                              for p in sorted(stage.iterdir())]}
        (stage / "release.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        (stage / "SHA256SUMS.txt").write_text("".join(
            f"{sha(p)}  {p.name}\n" for p in sorted(stage.iterdir())), encoding="utf-8")
        stage.rename(output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    record = commands.add_parser("record-build")
    for name in ("elf", "config", "broker-manifest", "output"):
        record.add_argument("--" + name, required=True)
    export = commands.add_parser("export")
    for name in ("app", "pkg", "broker", "broker-license", "receipt", "elf", "output", "version"):
        export.add_argument("--" + name, required=True)
    arguments = vars(parser.parse_args())
    command = arguments.pop("command")
    try:
        result = record_build(**arguments) if command == "record-build" else export_release(**arguments)
        if result:
            print(result)
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f"Release failed: {error}\n")


if __name__ == "__main__":
    main()
