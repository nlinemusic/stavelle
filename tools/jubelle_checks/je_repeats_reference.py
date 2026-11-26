#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The play order (S5 groundwork, perform::RepeatList) against MuseScore's own RepeatList: repeats, endings, jumps,
markers and section breaks unwound the same way.

1. MuseScore's 72 repeat test files (src/engraving/tests/repeat_data, format 1.x): MuseScore lists their play order
   and saves them as 4.x (the engine reads 4.x), the engine lists the saved files.
2. Every 4.x corpus score: both list the play order.

Usage: je_repeats_reference.py <je_io_tool> <reference tests binary> <repository> <work dir>
Prints a summary, writes <work dir>/differences.txt, and returns 1 when a play order differs."""
import os
import subprocess
import sys

try:
    import resource
except ImportError:   # Windows
    resource = None


def limited():
    if resource:
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        resource.setrlimit(resource.RLIMIT_AS, (6 << 30, 6 << 30))


def reference(ref, root, scores, out, work, save=None):
    listing = os.path.join(work, "list.txt")
    with open(listing, "w", encoding="utf-8") as f:
        f.write("\n".join(scores) + "\n")
    env = dict(os.environ, JUBELLE_REFERENCE_ROOT=root, JUBELLE_REFERENCE_LIST=listing, JUBELLE_REFERENCE_OUT=out,
               JUBELLE_REFERENCE_REPEATS="1")
    if save:
        env["JUBELLE_REFERENCE_SAVE"] = save
    subprocess.run([ref, "--gtest_filter=Jubelle_Reference.MusicListing"], env=env, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, cwd=work, timeout=2400, preexec_fn=limited if resource else None)


def engine(tool, folder, out):
    log = subprocess.run([tool, "repeats", folder, "--out", out], capture_output=True, text=True).stdout
    return sorted(l.split(": ", 1)[1] for l in log.splitlines() if l.startswith("listed: "))


def first(path, prefix):
    try:
        for line in open(path, encoding="utf-8"):
            if line.startswith(prefix):
                return line.rstrip("\n")
    except OSError:
        pass
    return None


def compare(pairs, diffs):
    same = 0
    for name, theirs, mine in pairs:
        a, b = first(theirs, "# repeats"), first(mine, "# repeats")
        if a is not None and a == b:
            same += 1
        else:
            diffs.append(f"{name}\n  MuseScore {a}\n  Jubelle   {b}")
    return same


def main(argv):
    if len(argv) != 5:
        print(__doc__)
        return 2
    tool, ref, repo, work = argv[1:5]
    os.makedirs(work, exist_ok=True)
    diffs = []

    # 1. MuseScore's repeat tests
    data = "src/engraving/tests/repeat_data"
    tests = sorted(f"{data}/{n}" for n in os.listdir(os.path.join(repo, data)) if n.endswith(".mscx"))
    saved, theirs, mine = (os.path.join(work, d) for d in ("tests_4x", "tests_musescore", "tests_jubelle"))
    reference(ref, repo, tests, theirs, work, save=saved)
    engine(tool, saved, mine)
    same_tests = compare([(t, os.path.join(theirs, t + ".txt"), os.path.join(mine, t + ".txt")) for t in tests], diffs)

    # 2. the corpus
    theirs, mine = os.path.join(work, "corpus_musescore"), os.path.join(work, "corpus_jubelle")
    scores = engine(tool, repo, mine)
    reference(ref, repo, scores, theirs, work)
    same_corpus = compare([(s, os.path.join(theirs, s + ".txt"), os.path.join(mine, s + ".txt")) for s in scores], diffs)

    with open(os.path.join(work, "differences.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(diffs) + ("\n" if diffs else ""))
    print(f"play order, MuseScore's repeat tests: {same_tests} of {len(tests)} the same")
    print(f"play order, corpus: {same_corpus} of {len(scores)} the same")
    for d in diffs[:20]:
        print(d)
    return 1 if diffs or not tests or not scores else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
