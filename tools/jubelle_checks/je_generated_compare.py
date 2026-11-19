#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Jubelle engine (S2): MuseScore's reading of the files Jubelle generated from its model, against MuseScore's
reading of the originals (decision D-E27). MuseScore's engine is only the comparison baseline here.

Usage: je_generated_compare.py <generated folder> <generated list> <listings of generated> <listings of originals>
                               <report.md> [--expected <file>]

The generated files are named like the originals with .mscz (je_io_tool regenerate --out). --expected lists
originals (relative paths, one per line, '#' comments and text after a tab ignored) where Jubelle's own reading
already differs from MuseScore's (corpus/reference_differences.txt): a difference there is expected. The exit
code is 1 for a difference outside that list or a file without a listing.
"""
import sys
from pathlib import Path


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    expected = set()
    if "--expected" in argv:
        for line in Path(argv[argv.index("--expected") + 1]).read_text(encoding="utf-8").splitlines():
            line = line.split("\t")[0].strip()
            if line and not line.startswith("#"):
                expected.add(line)
        args = [a for a in args if a != argv[argv.index("--expected") + 1]]
    gen_root, gen_list, gen_listings, orig_listings, report = args[:5]
    same, different, missing, unexpected, fixed = 0, [], [], [], []
    for rel in Path(gen_list).read_text(encoding="utf-8").splitlines():
        if not rel:
            continue
        stem = rel[:-5]
        orig = next((stem + e for e in (".mscx", ".mscz") if (Path(orig_listings) / (stem + e + ".txt")).exists()), None)
        g = Path(gen_listings) / (rel + ".txt")
        if orig is None or not g.exists():
            missing.append(rel)
            continue
        if g.read_text(encoding="utf-8") == (Path(orig_listings) / (orig + ".txt")).read_text(encoding="utf-8"):
            same += 1
            if orig in expected:
                fixed.append(orig)
        else:
            different.append(orig)
            if orig not in expected:
                unexpected.append(orig)
    lines = ["## MuseScore's reading of files generated from the Jubelle model", "",
             f"{same + len(different)} generated files read by MuseScore's engine: **{same} read exactly as the originals**, "
             f"{len(different)} different, {len(missing)} without a listing.",
             f"Expected differences (Jubelle's own reading differs): {len(expected)} listed; "
             f"**outside the list: {len(unexpected)}**; listed files now matching: {len(fixed)}", ""]
    lines += [f"- **different: {f}**" for f in unexpected]
    lines += [f"- different (expected): {f}" for f in different if f not in unexpected]
    lines += [f"- MuseScore reads the generated file alike (Jubelle's own reading still differs): {f}" for f in fixed]
    lines += [f"- no listing: {f}" for f in missing]
    text = "\n".join(lines) + "\n"
    Path(report).write_text(text, encoding="utf-8")
    print(text)
    # a listed file that MuseScore now reads alike is reported only: the list is about Jubelle's own reading
    return 1 if unexpected or missing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
