#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compares the Jubelle engine's chord shaping (je_io_tool chordshapes) with MuseScore's chord layout (the reference
tool's Jubelle_Reference.ChordLayout, with JUBELLE_REALISTIC_METRICS=2), chord by chord: each note's staff line,
notehead and accidental, which chords are beamed together, each chord's stem direction and which noteheads are
mirrored (S7's first shaping
stages; positions follow as the engine places them).

    je_layout_reference.py <je_io_tool> <reference tool> <repository> <score list> <work folder> [--reuse-reference]

Chords are matched by kind (grace before, chord, grace after), staff, voice, time and order. Chords on drum and
tablature staves are left out (not shaped yet); a notehead or stem direction the engine marks as not yet known
(cross-staff beams, beams placed by hand, two-note tremolos) is not compared.
Prints the kinds of differences with examples and exits 1 if any score differs.
"""
import os
import subprocess
import sys
from collections import Counter, defaultdict


HERE = os.path.dirname(os.path.abspath(__file__))
# scores whose difference is understood: file → reason (besides the files MuseScore reads differently,
# jubelle_engine/corpus/reference_differences.txt)
KNOWN = {
    'vtest/scores/ornament-1.mscz': 'ornaments: the accidental of an ornament\'s note restates the line (not yet)',
    'vtest/scores/ornament-3.mscz': 'ornaments (as above)',
    'vtest/scores/ornament-4.mscz': 'ornaments (as above)',
    'vtest/scores/ornament-2.mscz': 'ornaments (as above)',
    'vtest/scores/ornament-5.mscz': 'ornaments (as above)',
    'vtest/scores/ornament-6.mscz': 'ornaments (as above)',
    'vtest/scores/slurs-27.mscz': 'ornaments (as above)',
    'src/engraving/tests/changevisibility_data/changevisibility.mscx': 'ornaments (as above)',
    'vtest/scores/instrument-change-to-pitched.mscz': 'an instrument change from a drum set to a pitched instrument '
                                                       '(the staff is taken as a drum staff throughout, not yet per time)',
    'src/importexport/mnx/tests/data/project_examples/beamsOverBarlines_ref.mscx': 'a beam across a system break: MuseScore '
                                                       'splits it (no beams across systems); systems come later in S7',
}


def reading_differences():
    path = os.path.join(os.path.dirname(HERE), '..', 'jubelle_engine', 'corpus', 'reference_differences.txt')
    out = {}
    if os.path.exists(path):
        for line in open(path, encoding='utf-8'):
            if line.strip() and not line.startswith('#'):
                rel, _, why = line.rstrip('\n').partition('\t')
                out[rel] = 'MuseScore reads it differently: ' + why
    return out


def parse(path, engine):
    text = open(path, encoding='utf-8').read()
    if text.startswith('NOT READ'):
        return None
    chords = defaultdict(list)
    for line in text.splitlines():
        parts = line.split(' | ')
        head = parts[0].split()
        key = (head[0], int(head[1]), int(head[2]), int(head[3]))
        info = {'nonstandard': 'nonstandard' in head}
        for h in head[4:]:
            if '=' in h:
                k, v = h.split('=', 1)
                info[k] = v
        notes = []
        for p in parts[1:]:
            f = p.split()
            n = {'line': int(f[0]), 'head': f[1]}
            for x in f[2:]:
                if x.startswith('acc='):
                    n['acc'] = x[4:].split('@')[0]
                elif x == 'mirror':
                    n['mirror'] = True
            notes.append(n)
        info['notes'] = notes
        chords[key].append(info)
    return chords


def main():
    tool, ref, repo, lst, work = sys.argv[1:6]
    reuse = '--reuse-reference' in sys.argv
    je_out, ref_out = os.path.join(work, 'je'), os.path.join(work, 'ref')
    os.makedirs(je_out, exist_ok=True)
    subprocess.run([tool, 'chordshapes', repo, lst, je_out], check=True)
    if not reuse:
        env = dict(os.environ, JUBELLE_REALISTIC_METRICS='2', JUBELLE_REFERENCE_ROOT=repo, JUBELLE_REFERENCE_LIST=lst,
                   JUBELLE_REFERENCE_CHORDS=ref_out)
        subprocess.run([ref, '--gtest_filter=Jubelle_Reference.ChordLayout'], env=env, check=True, stdout=subprocess.DEVNULL)
    kinds = Counter()
    examples = {}
    bad = Counter()
    total = notes_compared = stems_compared = 0
    for rel in [l for l in open(lst, encoding='utf-8').read().splitlines() if l]:
        rp, jp = os.path.join(ref_out, rel + '.chords'), os.path.join(je_out, rel + '.chords')
        if not os.path.exists(rp) or not os.path.exists(jp):
            continue
        r, j = parse(rp, False), parse(jp, True)
        if r is None or j is None:
            continue
        total += 1

        def diff(kind, detail):
            kinds[kind] += 1
            bad[rel] += 1
            examples.setdefault(kind, f'{rel}: {detail}')
        for key, rchords in r.items():
            jchords = j.get(key, [])
            for i, rc in enumerate(rchords):
                if i >= len(jchords):
                    diff('chord missing', f'{key}')
                    continue
                jc = jchords[i]
                if jc['nonstandard']:
                    continue
                if len(rc['notes']) != len(jc['notes']):
                    diff('note count', f'{key}: MuseScore {len(rc["notes"])}, engine {len(jc["notes"])}')
                    continue
                laid_out = any(n['line'] != -10000 for n in rc['notes'])
                if laid_out and rc.get('beam') != jc.get('beam'):
                    diff('beam', f'{key}: MuseScore {rc.get("beam")}, engine {jc.get("beam")}')
                if laid_out and 'up' in jc:
                    stems_compared += 1
                if laid_out and 'up' in jc and rc.get('up') != jc['up']:
                    diff('stem direction', f'{key}: MuseScore {"up" if rc.get("up") == "1" else "down"}, engine {"up" if jc["up"] == "1" else "down"}')
                up = rc.get('up') == '1'
                for rn, jn in zip(rc['notes'], jc['notes']):
                    if rn['line'] == -10000:
                        continue   # a note MuseScore didn't lay out (its staff hidden, tablature): nothing to compare
                    notes_compared += 1
                    if rn['line'] != jn['line']:
                        diff('line', f'{key}: MuseScore {rn["line"]}, engine {jn["line"]}')
                    heads = jn['head'].rstrip('?')
                    if not jn['head'].endswith('?'):
                        hu, hd = heads.split('/')
                        if rn['head'] != (hu if up else hd):
                            diff('notehead', f'{key}: MuseScore {rn["head"]}, engine {hu if up else hd}')
                    if jc.get('mirror') != '?' and rn.get('mirror', False) != jn.get('mirror', False):
                        diff('mirrored notehead', f'{key} line {rn["line"]}: MuseScore {rn.get("mirror", False)}, engine {jn.get("mirror", False)}')
                    if rn.get('acc') != jn.get('acc'):
                        diff('accidental', f'{key} line {rn["line"]}: MuseScore {rn.get("acc")}, engine {jn.get("acc")}')
        for key, jchords in j.items():
            if len(jchords) > len(r.get(key, [])):
                diff('extra chord', f'{key}')
    known = dict(reading_differences(), **KNOWN)
    for k, n in kinds.most_common():
        print(f'{n:6}  {k}   e.g. {examples[k]}')
    unexplained = [rel for rel in bad if rel not in known]
    print(f'chord shaping: {total - len(bad)} of {total} scores the same as MuseScore ({notes_compared} notes and {stems_compared} stem directions compared); '
          f'{len(bad) - len(unexplained)} known differences, **{len(unexplained)} others**')
    for rel, n in bad.most_common():
        print(f'   {n:5}  {rel}' + (f'  ({known[rel]})' if rel in known else '  UNEXPLAINED'))
    return 1 if unexplained else 0


if __name__ == '__main__':
    sys.exit(main())
