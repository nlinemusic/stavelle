#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Jubelle engine (decision D-E17): the user's own scores must never be tracked by Git.

Fails if:
  - any tracked file is under jubelle_engine/corpus/private/ or a JubellePrivateScores/ folder;
  - (when the private folder exists locally) any tracked file has the same name and size as a private score.
Usage: private_corpus_guard.py   (run from anywhere inside the repository)
"""
import os
import subprocess
import sys
from pathlib import Path


def main():
    root = Path(subprocess.check_output(["git", "rev-parse", "--show-toplevel"], text=True).strip())
    tracked = subprocess.check_output(["git", "-C", str(root), "ls-files", "-z"], text=True).split("\0")
    tracked = [t for t in tracked if t]
    bad = [t for t in tracked if t.startswith("jubelle_engine/corpus/private/") or "JubellePrivateScores/" in t]

    private = Path(os.environ.get("JUBELLE_PRIVATE_CORPUS", Path.home() / "JubellePrivateScores"))
    private_files = {}
    for folder in (private, root / "jubelle_engine/corpus/private"):
        if folder.is_dir():
            for p in folder.rglob("*"):
                if p.is_file():
                    private_files[(p.name.lower(), p.stat().st_size)] = p
    if private_files:
        for t in tracked:
            p = root / t
            if p.is_file() and (p.name.lower(), p.stat().st_size) in private_files:
                bad.append(t)

    if bad:
        print("Private scores are tracked by Git (they must never be committed):")
        for b in sorted(set(bad)):
            print(f"  {b}")
        return 1
    print(f"Private corpus guard: OK ({len(tracked)} tracked files checked, "
          f"{len(private_files)} private files known locally)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
