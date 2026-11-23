#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""Fetches GUI.Forms at the commit third_party/gui-forms.lock.json pins."""
import argparse
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, default=ROOT / "gui_forms")
    args = parser.parse_args()
    lock = json.loads((ROOT / "third_party/gui-forms.lock.json").read_text(encoding="utf-8"))
    commit = lock["commit"]
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise SystemExit("third_party/gui-forms.lock.json pins no commit yet (" + commit + "); see its pin_note")
    destination = args.destination.resolve()
    if destination.exists():
        revision = subprocess.check_output(["git", "-C", str(destination), "rev-parse", "HEAD"], text=True).strip()
        changes = subprocess.check_output(["git", "-C", str(destination), "status", "--porcelain",
                                           "--untracked-files=no"], text=True)
        if revision != commit or changes:
            raise SystemExit(str(destination) + " differs from the pin; pass a fresh --destination.")
    else:
        subprocess.run(["git", "init", str(destination)], check=True)
        # The checkout may be made by Windows Git and read by MSYS2 Git.
        subprocess.run(["git", "-C", str(destination), "config", "core.autocrlf", "false"], check=True)
        subprocess.run(["git", "-C", str(destination), "remote", "add", "origin",
                        "https://github.com/" + lock["repository"] + ".git"], check=True)
        subprocess.run(["git", "-C", str(destination), "fetch", "--depth=1", "origin", commit], check=True)
        subprocess.run(["git", "-C", str(destination), "checkout", "--detach", commit], check=True)
    print(destination)


if __name__ == "__main__":
    main()
