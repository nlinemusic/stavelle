#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""S3, time signature change against MuseScore's own command: the engine (`je_io_tool tsedit`) and MuseScore's
Score::cmdAddTimeSig (the reference listing with JUBELLE_REFERENCE_TIMESIG) change the same time signature in every
4.x corpus score; the music listings are compared.

Usage: je_timesig_reference.py <je_io_tool> <reference tests binary> <repository> <work dir> <index> <num> <den>

MuseScore's command crashes on a few test files in the headless test build; those are skipped (and counted), the run
resumes after them. Prints a summary and writes <work dir>/differences.txt."""
import collections
import os
import subprocess
import sys


def main(argv):
    if len(argv) != 8:
        print(__doc__)
        return 2
    tool, ref, repo, work, index, num, den = argv[1:]
    spec = f"{index} {num} {den}"
    mine, theirs = os.path.join(work, "jubelle"), os.path.join(work, "musescore")
    os.makedirs(theirs, exist_ok=True)
    log = subprocess.run([tool, "tsedit", repo, "--out", mine, index, num, den], capture_output=True, text=True).stdout
    scores = [l.split(": ", 1)[1].removesuffix(" PROBLEMS") for l in log.splitlines() if l.startswith(("changed: ", "refused: "))]
    problems = [l for l in log.splitlines() if l.endswith(" PROBLEMS")]
    todo, crashed = list(scores), []
    env = dict(os.environ, JUBELLE_REFERENCE_TIMESIG=spec, JUBELLE_REFERENCE_ROOT=repo,
               JUBELLE_REFERENCE_LIST=os.path.join(work, "todo.txt"), JUBELLE_REFERENCE_OUT=theirs)
    while todo:
        with open(env["JUBELLE_REFERENCE_LIST"], "w", encoding="utf-8") as f:
            f.write("\n".join(todo) + "\n")
        subprocess.run([ref, "--gtest_filter=Jubelle_Reference.*"], env=env, capture_output=True, cwd=work)
        rest = [r for r in todo if not os.path.exists(os.path.join(theirs, r + ".txt"))]
        if rest:
            crashed.append(rest[0])
            rest = rest[1:]
        todo = rest
    stat, diffs = collections.Counter(), []
    for rel in scores:
        if rel in crashed:
            stat["MuseScore's command crashed"] += 1
            continue
        a = open(os.path.join(mine, os.path.splitext(rel)[0] + ".mscz.je.txt"), encoding="utf-8").read()
        b = open(os.path.join(theirs, rel + ".txt"), encoding="utf-8").read()
        ha, hb = a.split("\n", 1)[0][10:], b.split("\n", 1)[0][10:]
        if a == b:
            stat[f"the same music ({hb})"] += 1
        else:
            stat[f"different: Jubelle {ha}, MuseScore {hb}"] += 1
            diffs.append(rel)
    print(f"time signature {num}/{den} at measure index {index}: {len(scores)} scores; {len(problems)} left inconsistent")
    for k, v in sorted(stat.items()):
        print(f"  {v:5d}  {k}")
    with open(os.path.join(work, "differences.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(diffs) + "\n")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
