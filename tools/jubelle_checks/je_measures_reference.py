#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""S3, measure commands against MuseScore's own: the engine (`je_io_tool tsedit` / `measedit`) and MuseScore
(Score::cmdAddTimeSig, SplitJoinMeasure::splitMeasure / joinMeasures, by the reference listing with
JUBELLE_REFERENCE_TIMESIG / _SPLIT / _JOIN) make the same change in every 4.x corpus score; the music listings are
compared.

Usage: je_measures_reference.py <je_io_tool> <reference tests binary> <repository> <work dir> timesig <index> <num> <den>
       je_measures_reference.py … split <measure index>
       je_measures_reference.py … join <first index> <last index>

MuseScore's command crashes on a few test files in the headless test build; those are skipped (and counted), the run
resumes after them. Prints a summary and writes <work dir>/differences.txt."""
import collections
import os
import subprocess
import sys

try:
    import resource
except ImportError:   # Windows
    resource = None


def limited():
    # no core files (a crash would otherwise write one the size of the test binary's memory), and a memory limit,
    # so that a command that runs away fails by itself instead of taking the machine down
    if resource:
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        resource.setrlimit(resource.RLIMIT_AS, (6 << 30, 6 << 30))


def main(argv):
    if len(argv) < 7:
        print(__doc__)
        return 2
    tool, ref, repo, work, op = argv[1:6]
    args = argv[6:]
    spec = " ".join(args)
    mine, theirs = os.path.join(work, "jubelle"), os.path.join(work, "musescore")
    os.makedirs(theirs, exist_ok=True)
    if op == "timesig":
        command, variable = [tool, "tsedit", repo, "--out", mine] + args, "JUBELLE_REFERENCE_TIMESIG"
    else:
        command, variable = [tool, "measedit", repo, "--out", mine, op] + args, "JUBELLE_REFERENCE_" + op.upper()
    log = subprocess.run(command, capture_output=True, text=True).stdout
    scores = [l.split(": ", 1)[1].removesuffix(" PROBLEMS") for l in log.splitlines()
              if l.split(": ", 1)[0] in ("changed", "refused", "unchanged") and ": " in l]
    problems = [l for l in log.splitlines() if l.endswith(" PROBLEMS")]
    todo, crashed = list(scores), []
    env = dict(os.environ, JUBELLE_REFERENCE_ROOT=repo,
               JUBELLE_REFERENCE_LIST=os.path.join(work, "todo.txt"), JUBELLE_REFERENCE_OUT=theirs)
    if op.startswith("slash") or op == "movestaff":
        env["JUBELLE_REFERENCE_SLASH"] = "1"   # slashes and accent rests marked in both listings
    env[variable] = spec
    while todo:
        with open(env["JUBELLE_REFERENCE_LIST"], "w", encoding="utf-8") as f:
            f.write("\n".join(todo) + "\n")
        try:
            subprocess.run([ref, "--gtest_filter=Jubelle_Reference.*"], env=env, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, cwd=work, timeout=1800, preexec_fn=limited if resource else None)
        except subprocess.TimeoutExpired:
            print(f"{op} {spec}: MuseScore's command still running after 30 minutes; skipped", file=sys.stderr)
        rest = [r for r in todo if not os.path.exists(os.path.join(theirs, r + ".txt"))]
        if rest:
            print(f"{op} {spec}: MuseScore stopped at {rest[0]}", file=sys.stderr)
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
        ha, hb = a.split("\n", 1)[0].split(" ")[-1], b.split("\n", 1)[0].split(" ")[-1]
        if a == b:
            stat[f"the same music ({hb})"] += 1
        else:
            stat[f"different: Jubelle {ha}, MuseScore {hb}"] += 1
            diffs.append(rel)
    print(f"{op} {spec}: {len(scores)} scores; {len(problems)} left inconsistent")
    for k, v in sorted(stat.items()):
        print(f"  {v:5d}  {k}")
    with open(os.path.join(work, "differences.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(diffs) + "\n")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
