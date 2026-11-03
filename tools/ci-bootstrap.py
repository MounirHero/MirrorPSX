#!/usr/bin/env python3
"""Fetch public native build inputs at explicit commits on a clean CI runner."""
import json
import re
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
lock = json.loads((root / "tools/ci-dependencies.json").read_text())
for name, entry in lock.items():
    destination = (root / name).resolve()
    if root not in destination.parents or not re.fullmatch(r"[a-f0-9]{40}", entry["revision"]):
        raise SystemExit("Invalid dependency destination or revision")
    if destination.exists():
        raise SystemExit(f"Dependency destination already exists: {name}; bootstrap expects a clean runner")
    subprocess.run(["git", "init", "--quiet", str(destination)], check=True)
    subprocess.run(["git", "-C", str(destination), "remote", "add", "origin", entry["repository"]], check=True)
    subprocess.run(["git", "-C", str(destination), "fetch", "--quiet", "--depth=1", "origin", entry["revision"]], check=True)
    subprocess.run(["git", "-C", str(destination), "checkout", "--quiet", "--detach", "FETCH_HEAD"], check=True)
    actual = subprocess.check_output(["git", "-C", str(destination), "rev-parse", "HEAD"], text=True).strip()
    if actual != entry["revision"]:
        raise SystemExit(f"Dependency revision mismatch: {name}")
    print(f"Ready: {name} at {actual[:12]}")
