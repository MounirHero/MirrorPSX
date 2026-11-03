#!/usr/bin/env python3
"""Resolve official releases and immutable nightly identities before building."""
import argparse
from datetime import datetime, timedelta, timezone
from pathlib import Path
import re
from zoneinfo import ZoneInfo

VERSION_RE = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?")


def resolve(*, version, native_version, event, ref_name, sha, now, nightly=False,
            draft=False, prerelease=False, schedule="", existing_tags=()):
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise ValueError("Expected a full Git commit SHA")
    if not re.fullmatch(r"[0-9]{2}\.[0-9]{2}", native_version):
        raise ValueError("PS5_VERSION must use the SDK's native NN.NN format")
    if event not in ("push", "workflow_dispatch", "schedule"):
        raise ValueError("Unsupported release event")
    if event == "push":
        if not ref_name.startswith("v") or nightly:
            raise ValueError("Official tag releases must use v<version>")
        version = ref_name[1:]
    else:
        version = version.removeprefix("v")
    match = VERSION_RE.fullmatch(version)
    if not match or (match[4] and any(p.isdigit() and len(p) > 1 and p.startswith("0")
                                    for p in match[4].split("."))):
        raise ValueError("Use a release version such as 0.1.0, v0.1.0 or 0.1.0-rc.1")
    instant = datetime.fromisoformat(now.replace("Z", "+00:00"))
    if instant.tzinfo is None:
        raise ValueError("Release timestamp needs a timezone")
    instant = instant.astimezone(timezone.utc)
    should_run = True
    if event == "schedule":
        if schedule not in ("0 20 * * *", "0 21 * * *"):
            raise ValueError("Unexpected nightly schedule")
        hour = int(schedule.split()[1])
        nominal = instant.replace(hour=hour, minute=0, second=0, microsecond=0)
        if nominal > instant:
            nominal -= timedelta(days=1)
        instant = nominal
        nightly = True
        should_run = instant.astimezone(ZoneInfo("Europe/Rome")).hour == 22
        if any(re.fullmatch(r"nightly-[0-9]{8}-g[0-9a-f]{7,40}", tag)
               and sha.startswith(tag.rsplit("-g", 1)[1]) for tag in existing_tags):
            should_run = False
    elif schedule:
        raise ValueError("Cron expressions are only valid for scheduled releases")
    if nightly and (draft or prerelease):
        raise ValueError("Nightly snapshots use their own publication mode; leave draft/prerelease unchecked")
    local_date = instant.astimezone(ZoneInfo("Europe/Rome")).strftime("%Y%m%d")
    tag = f"nightly-{local_date}-g{sha[:12]}" if nightly else f"v{version}"
    return dict(should_run=should_run, nightly=nightly, version=version,
                native_version=native_version, tag=tag,
                title=f"nightly [{local_date}-g{sha[:12]}]" if nightly else f"MirrorPSX v{version}",
                prerelease=not nightly and bool(match[4] or prerelease),
                stable=not nightly and not match[4] and not draft and not prerelease)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default="")
    parser.add_argument("--event", required=True)
    parser.add_argument("--ref-name", default="")
    parser.add_argument("--sha", required=True)
    parser.add_argument("--now", required=True)
    parser.add_argument("--schedule", default="")
    parser.add_argument("--nightly", action="store_true")
    parser.add_argument("--draft", action="store_true")
    parser.add_argument("--prerelease", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        values = vars(args)
        values["version"] = args.version.strip() or (root / "VERSION").read_text(encoding="utf-8").strip()
        values["native_version"] = (root / "PS5_VERSION").read_text(encoding="utf-8").strip()
        import subprocess
        values["existing_tags"] = subprocess.check_output(
            ["git", "tag", "--points-at", args.sha], text=True).splitlines()
        for key, value in resolve(**values).items():
            print(f"{key}={str(value).lower() if isinstance(value, bool) else value}")
    except (ValueError, OSError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
