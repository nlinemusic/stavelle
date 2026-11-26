#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compares the Jubelle engine's reading of every score's style (je_io_tool stylelist) with MuseScore's
(the reference tool's Jubelle_Reference.StyleListing): every style value of every score.

    je_style_reference.py <je_io_tool> <reference tool> <repository> <score list> <work folder>

Numbers are compared to MuseScore's file precision (6 significant digits). Prints the differing values by name,
with how many scores differ in each, and exits 1 if any score differs outside KNOWN.
"""
import os
import subprocess
import sys
import xml.etree.ElementTree as ET
from collections import Counter

# (value name) → reason: values the engine knows it doesn't read like MuseScore yet
KNOWN = {
    # MuseScore converts pre-4.6 values with Edwin's cap height at 10pt (121 units, measured by the font provider);
    # the reference tool has no text font metrics, so its result is not a number
    'chordExtensionAdjust': 'the reference tool has no text font metrics',
    'chordModifierAdjust': 'the reference tool has no text font metrics',
}


def values(path):
    text = open(path, encoding='utf-8').read()
    if text.startswith('NOT READ'):
        return None
    root = ET.fromstring(text)
    out = {}
    for e in root:
        out[e.tag] = (e.text or '', tuple(sorted(e.attrib.items())))
    return out


def same(a, b):
    if a == b:
        return True
    (ta, aa), (tb, ab) = a, b

    def num(x, y):
        try:
            fx, fy = float(x), float(y)
        except ValueError:
            return x == y
        return abs(fx - fy) <= 1e-5 * max(1.0, abs(fx), abs(fy))
    if len(aa) != len(ab) or not all(ka == kb and num(va, vb) for (ka, va), (kb, vb) in zip(aa, ab)):
        return False
    return num(ta.strip(), tb.strip())


def main():
    tool, ref, repo, lst, work = sys.argv[1:6]
    je_out, ref_out = os.path.join(work, 'je'), os.path.join(work, 'ref')
    os.makedirs(je_out, exist_ok=True)
    os.makedirs(ref_out, exist_ok=True)
    subprocess.run([tool, 'stylelist', repo, lst, je_out], check=True)
    env = dict(os.environ, JUBELLE_REFERENCE_ROOT=repo, JUBELLE_REFERENCE_LIST=lst, JUBELLE_REFERENCE_STYLELIST=ref_out)
    subprocess.run([ref, '--gtest_filter=Jubelle_Reference.StyleListing'], env=env, check=True,
                   stdout=subprocess.DEVNULL)
    names = Counter()
    examples = {}
    bad = set()
    total = 0
    for rel in [l for l in open(lst, encoding='utf-8').read().splitlines() if l]:
        r = values(os.path.join(ref_out, rel + '.style'))
        j = values(os.path.join(je_out, rel + '.style'))
        if r is None:
            continue
        total += 1
        if j is None:
            bad.add(rel)
            names['(not read)'] += 1
            continue
        for k, v in r.items():
            if k in KNOWN and v[0].strip() in ('nan', '-nan'):
                continue   # the reference tool's own limit (above), not a difference
            if k not in j or not same(v, j[k]):
                names[k] += 1
                examples.setdefault(k, (rel, v, j.get(k)))
                if k not in KNOWN:
                    bad.add(rel)
    for k, n in names.most_common():
        rel, v, w = examples.get(k, ('', '', ''))
        print(f'{n:5}  {k}{"  (known: " + KNOWN[k] + ")" if k in KNOWN else ""}   e.g. {rel}: MuseScore {v}, engine {w}')
    print(f'style: {total - len(bad)} of {total} scores read with MuseScore\'s style values')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
