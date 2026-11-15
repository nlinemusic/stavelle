#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Jubelle build check: the app updater may only ever look at Jubelle's own
GitHub releases, never at MuseScore's update servers.

Replaces the older "updater module must be off" check. Jubelle now ships its
own signed updater (approved R3), which reuses MuseScore's update module, so
the module being on is expected; what must never happen is the app contacting
MuseScore's app-update feeds.

Usage: check_update_feed.py <update.cfg> <Jubelle executable>
"""
import json
import sys

ALLOWED_PREFIX = "https://api.github.com/repos/nlinemusic/stavelle/releases"
FORBIDDEN = [
    "updates.musescore.org/feed/latest.xml",
    "updates.musescore.org/feed/latest.test.xml",
    "updates.musescore.org/feed/all.xml",
    "updates.musescore.org/feed/all.test.xml",
]


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    cfg_path, exe_path = sys.argv[1], sys.argv[2]
    ok = True

    with open(cfg_path, encoding="utf-8") as f:
        cfg = json.load(f)
    for key, url in cfg.items():
        if url and not url.startswith(ALLOWED_PREFIX):
            print(f"FAIL: update feed '{key}' points outside Jubelle's releases: {url}")
            ok = False
    if not cfg.get("latest", "").startswith(ALLOWED_PREFIX):
        print("FAIL: update feed 'latest' is not Jubelle's GitHub releases")
        ok = False

    with open(exe_path, "rb") as f:
        data = f.read()
    for s in FORBIDDEN:
        for enc in ("utf-8", "utf-16-le"):
            if s.encode(enc) in data:
                print(f"FAIL: {exe_path} contains MuseScore's app-update feed {s} ({enc})")
                ok = False

    print("Update feed check: " + ("PASS (only Jubelle's own GitHub releases)" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
