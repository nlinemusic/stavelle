#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""S3, editing commands: every corpus score edited with the engine's commands (je_io_tool cmdedit, tsedit) must be read by
MuseScore's engine with exactly the music the edited model holds (<name>.mscz.je.txt next to each file).

Usage: je_command_compare.py <cmdedit dir> <list> <MuseScore listings dir> <report.md> --expected <reference_differences.txt>
Files whose original MuseScore already reads differently from Jubelle (the expected list) are reported, not failed.
Exit status 1 on any other difference or a missing listing."""
import os
import sys


def stem(path):
    return os.path.splitext(path)[0]


def strip_head(text):
    # je_io_tool tsedit and measedit head their listings with "# timesig …" or "# measures …"
    return text.split("\n", 1)[1] if text.startswith(("# timesig", "# measures")) else text


def main(argv):
    if len(argv) < 5:
        print(__doc__)
        return 2
    root, listing, ref, report = argv[1:5]
    expected = set()
    if "--expected" in argv:
        for line in open(argv[argv.index("--expected") + 1], encoding="utf-8"):
            if line.strip() and not line.startswith("#"):
                expected.add(stem(line.split("\t")[0]))
    same, known, diff, missing = 0, [], [], []
    for rel in open(listing, encoding="utf-8").read().splitlines():
        mine = os.path.join(root, rel + ".je.txt")
        theirs = os.path.join(ref, rel + ".txt")
        if not os.path.exists(theirs) or not os.path.exists(mine):
            missing.append(rel)
        elif strip_head(open(mine, encoding="utf-8").read()) == open(theirs, encoding="utf-8").read():
            same += 1
        elif stem(rel) in expected:
            known.append(rel)
        else:
            diff.append(rel)
    with open(report, "w", encoding="utf-8") as out:
        out.write("## Editing commands: the edited scores read back by MuseScore's engine\n\n")
        out.write(f"{same + len(known) + len(diff) + len(missing)} scores edited; **{same} read with exactly the edited music**; "
                  f"{len(known)} differ as their originals already do; **{len(diff)} other differences**; "
                  f"{len(missing)} without a listing\n\n")
        for rel in diff:
            out.write(f"- **different: {rel}**\n")
        for rel in missing:
            out.write(f"- **no listing: {rel}**\n")
    print(f"same {same}, as the originals {len(known)}, different {len(diff)}, no listing {len(missing)}")
    return 1 if diff or missing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
