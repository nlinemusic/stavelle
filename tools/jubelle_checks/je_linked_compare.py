#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Linked staves while editing (Jubelle engine S3): compares MuseScore's reading of scores edited by the engine
(je_io_tool linkedit) with its reading of the originals.

For each score with a linked staff (a guitar's standard and tablature staves), the engine entered a chord on the
main staff and changed a pitch on the linked staff, then saved. MuseScore's engine read both files with
JUBELLE_REFERENCE_LINKS=1. Passes when, in every edited score, the linked staves hold the same music and MuseScore
links at least as many of their chords, rests and notes as in the original, with no link to a different element.

usage: je_linked_compare.py <linkedit log> <original listings> <edited listings> <report.md>
"""
import os
import re
import sys

LINKS = re.compile(r'# links staff (\d+): (\d+) elements, (\d+) linked, (\d+) wrong(?:, with staff (\d+))?')


def main():
    log, orig_dir, edit_dir, report = sys.argv[1:5]
    edited = [l.split(': ', 1)[1].split(' (format ')[0] for l in open(log) if l.startswith('edited:')]
    worse, music, same, better = [], [], 0, 0
    for rel in edited:
        stem = os.path.splitext(rel)[0]
        a_path, b_path = os.path.join(orig_dir, rel + '.txt'), os.path.join(edit_dir, stem + '.mscz.txt')
        a = open(a_path).read() if os.path.exists(a_path) else ''
        b = open(b_path).read() if os.path.exists(b_path) else ''
        la, lb = LINKS.findall(a), LINKS.findall(b)
        unlinked_a = sum(int(e) - int(k) for _, e, k, _, _ in la)
        unlinked_b = sum(int(e) - int(k) for _, e, k, _, _ in lb)
        wrong_a = sum(int(x[3]) for x in la)
        wrong_b = sum(int(x[3]) for x in lb)
        if not lb or unlinked_b > unlinked_a or wrong_b > wrong_a:
            worse.append(f'{rel}: unlinked {unlinked_a} → {unlinked_b}, linked to another element {wrong_a} → {wrong_b}')
        elif unlinked_b < unlinked_a:
            better += 1
        else:
            same += 1
        staves = {}
        for line in b.splitlines():
            if line and not line.startswith('#'):
                s, rest = line.split(' ', 1)
                staves.setdefault(int(s), []).append(rest)
        groups = {}   # the staves linked together (by their first staff)
        for x in lb:
            groups.setdefault(x[4], []).append(int(x[0]))
        if any(len({tuple(staves.get(s, [])) for s in g}) > 1 for g in groups.values()):
            music.append(rel)
    with open(report, 'w') as out:
        out.write('## Linked staves while editing (S3)\n\n')
        out.write(f'{len(edited)} scores with a linked staff, edited on both staves by the engine and read by MuseScore\'s engine:\n\n')
        out.write(f'- linked staves holding the same music: **{len(edited) - len(music)} of {len(edited)}**\n')
        out.write(f'- links as in the original: {same}, more than in the original: {better}, **fewer: {len(worse)}**\n')
        for w in worse:
            out.write(f'  - {w}\n')
        for m in music:
            out.write(f'  - different music on the linked staves: {m}\n')
    print(open(report).read())
    return 1 if worse or music else 0


if __name__ == '__main__':
    sys.exit(main())
