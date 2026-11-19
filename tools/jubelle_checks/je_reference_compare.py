#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Jubelle engine (S2): the native reader against MuseScore's own reading, file by file.

Usage: je_reference_compare.py <je_io_tool> <repository root> <file list> <MuseScore listings folder> <report.md>
The MuseScore listings come from jubelle_engine_tests (Jubelle_Reference.MusicListing).
"""
import subprocess
import sys
from collections import Counter
from pathlib import Path


def main():
    tool, root, lst, ref, report = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]), Path(sys.argv[5])
    files = [l.strip() for l in lst.read_text(encoding="utf-8").splitlines() if l.strip()]
    same, different, unread, examples, kinds = 0, 0, 0, [], Counter()
    for rel in files:
        ms = ref / (rel + ".txt")
        if not ms.exists() or ms.read_text(encoding="utf-8") == "NOT READ\n":
            unread += 1
            continue
        out = subprocess.run([str(tool), "listing", str(root / rel)], capture_output=True, text=True, timeout=120)
        a, b = out.stdout.splitlines(), ms.read_text(encoding="utf-8").splitlines()
        if a == b:
            same += 1
            continue
        different += 1
        first = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
        ja = a[first] if first < len(a) else "(end)"
        mb = b[first] if first < len(b) else "(end)"
        kind = "count" if first >= min(len(a), len(b)) else ("grace" if " g" in ja + mb else "time/duration/pitch")
        kinds[kind] += 1
        if len(examples) < 40:
            examples.append(f"- {rel}: line {first + 1}: Jubelle `{ja}` · MuseScore `{mb}` ({len(a)} vs {len(b)} lines)")
    md = ["## Jubelle engine reader against MuseScore's reading", "",
          f"{len(files)} MuseScore 4.x scores: **{same} identical music listings**, {different} different, "
          f"{unread} not read by MuseScore's engine.", "", "Differences by kind: " +
          (", ".join(f"{k} {n}" for k, n in kinds.items()) or "none"), ""] + examples
    report.write_text("\n".join(md) + "\n", encoding="utf-8")
    print("\n".join(md))
    return 0


if __name__ == "__main__":
    sys.exit(main())
