#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
#
# Stavelle: builds a large test score for performance measurement by repeating the
# instruments and the music of an existing MuseScore 4 score (e.g. demos/Dawn.mscx).
#
# Usage: make_large_score.py <source.mscx> <output.mscx> --instruments N --measures M
#   --instruments N : the score's instrument list is repeated N times
#   --measures M    : every staff's music is repeated M times

import argparse
import copy
import xml.etree.ElementTree as ET


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source")
    ap.add_argument("output")
    ap.add_argument("--instruments", type=int, default=3)
    ap.add_argument("--measures", type=int, default=15)
    a = ap.parse_args()

    tree = ET.parse(a.source)
    score = tree.getroot().find("Score")
    parts = score.findall("Part")
    staves = score.findall("Staff")
    staff_count = len(staves)

    # Repeat the music of every staff
    for staff in staves:
        measures = staff.findall("Measure")
        for _ in range(a.measures - 1):
            for m in measures:
                staff.append(copy.deepcopy(m))

    # Repeat the instruments, giving the copies new staff numbers
    last_part = parts[-1]
    insert_parts_at = list(score).index(last_part) + 1
    new_parts, new_staves = [], []
    for k in range(1, a.instruments):
        offset = k * staff_count
        for p in parts:
            np = copy.deepcopy(p)
            for s in np.findall("Staff"):
                s.set("id", str(int(s.get("id")) + offset))
            new_parts.append(np)
        for s in staves:
            ns = copy.deepcopy(s)
            ns.set("id", str(int(s.get("id")) + offset))
            new_staves.append(ns)

    for i, np in enumerate(new_parts):
        score.insert(insert_parts_at + i, np)
    for ns in new_staves:
        score.append(ns)

    meta = score.find("metaTag[@name='workTitle']")
    if meta is not None:
        meta.text = "Stavelle large score test"
    tree.write(a.output, encoding="UTF-8", xml_declaration=True)

    total_staves = len(score.findall("Staff"))
    total_measures = len(score.find("Staff").findall("Measure"))
    print(f"{a.output}: {len(score.findall('Part'))} instruments, {total_staves} staves, {total_measures} measures")


if __name__ == "__main__":
    main()
