#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Jubelle engine (S2): file compatibility with the official MuseScore Studio 4.7.7, both ways.

For a fixed sample of MuseScore 4.x scores from the repository:
1. Jubelle -> MuseScore: the Jubelle engine reads and writes the score (je_io_tool rewrite, adding the
   engine store). MuseScore opens both files and exports MIDI; the two MIDI files must be identical.
2. MuseScore -> Jubelle: MuseScore saves the Jubelle-written file; the Jubelle engine reads MuseScore's file
   and its music listing must equal the listing of the original.
3. Jubelle's settings survive MuseScore: a score with all 11 Jubelle style settings set is saved by
   MuseScore (which drops them); the Jubelle engine restores all 11 from the engine store.

Usage: je_musescore_compat.py <MuseScore exe> <je_io_tool> <repository root> <work dir>
Writes <work>/je_compat.md and exits 1 if any check fails.
"""
import subprocess
import sys
from pathlib import Path

SAMPLE_EVERY = 12        # every 12th 4.x score in vtest/scores (sorted), plus the fixed list below
FIXED = [
    "demos/Dawn.mscx",
    "demos/Fugue_1.mscx",
    "demos/Unclaimed_Gift.mscx",
    "share/autobotscripts/data/Big_Score.mscz",
    "tools/jubelle_templates/testscores/Big_Band_test_music.mscz",
    "tools/jubelle_checks/data/Jubelle_features.mscz",
]


def run(exe, args, log, timeout=240):
    with open(log, "w", encoding="utf-8", errors="replace") as f:
        try:
            return subprocess.run([str(exe)] + [str(a) for a in args], stdout=f, stderr=subprocess.STDOUT,
                                  stdin=subprocess.DEVNULL, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/F", "/IM", "MuseScore4.exe"], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL) if sys.platform == "win32" else None
            return -1


def is_v4(path):
    try:
        if path.suffix == ".mscx":
            head = path.read_bytes()[:300]
        else:
            import zipfile
            with zipfile.ZipFile(path) as z:
                names = [n for n in z.namelist() if n.endswith(".mscx") and not n.startswith("Excerpts/")]
                head = z.read(names[0])[:300] if names else b""
        return b'museScore version="4.' in head
    except Exception:
        return False


def listing(tool, path, log):
    out = subprocess.run([str(tool), "listing", str(path)], capture_output=True, text=True, timeout=120)
    Path(log).write_text(out.stderr or "", encoding="utf-8")
    return out.stdout if out.returncode == 0 else None


def main():
    exe, tool, root, work = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]).resolve(), Path(sys.argv[4]).resolve()
    work.mkdir(parents=True, exist_ok=True)
    vtest = sorted(p for p in (root / "vtest/scores").glob("*.msc[zx]") if is_v4(p))
    sample = [root / f for f in FIXED if (root / f).exists()] + vtest[::SAMPLE_EVERY]
    rows, failures = [], 0
    for i, src in enumerate(sample):
        tag = f"s{i:03d}"
        je = work / f"{tag}_jubelle.mscz"
        ms = work / f"{tag}_musescore.mscz"
        mid_a, mid_b = work / f"{tag}_original.mid", work / f"{tag}_jubelle.mid"
        r = {"score": str(src.relative_to(root)).replace("\\", "/")}
        r["rewritten"] = subprocess.run([str(tool), "rewrite", str(src), str(je)], capture_output=True).returncode == 0
        run(exe, ["-o", mid_a, src], work / f"{tag}_a.log")
        run(exe, ["-o", mid_b, je], work / f"{tag}_b.log")
        r["midi_same"] = mid_a.exists() and mid_b.exists() and mid_a.read_bytes() == mid_b.read_bytes()
        run(exe, ["-o", ms, je], work / f"{tag}_save.log")
        la = listing(tool, src, work / f"{tag}_la.log")
        lb = listing(tool, ms, work / f"{tag}_lb.log") if ms.exists() else None
        r["musescore_saved"] = ms.exists()
        # the native reader reads MuseScore 4.x; for an older original (the 3.x demos) this check doesn't apply
        r["v4"] = is_v4(src)
        r["listing_same"] = (la is not None and la == lb) if r["v4"] else None
        ok = r["rewritten"] and r["midi_same"] and r["musescore_saved"] and r["listing_same"] is not False
        failures += 0 if ok else 1
        rows.append((r, ok))
        detail = ""
        if not ok:
            detail = " " + ", ".join(k for k in ("rewritten", "midi_same", "musescore_saved", "listing_same") if r[k] is False)
            if la is not None and lb is not None and la != lb:
                a, b = la.splitlines(), lb.splitlines()
                i = next((j for j in range(min(len(a), len(b))) if a[j] != b[j]), min(len(a), len(b)))
                detail += f"; listing line {i + 1}: original `{a[i] if i < len(a) else '(end)'}` · after MuseScore `{b[i] if i < len(b) else '(end)'}`"
        print(("PASS " if ok else "FAIL ") + r["score"] + detail, flush=True)

    # Jubelle's 11 style settings through a MuseScore save
    base = root / "tools/jubelle_checks/data/Jubelle_features.mscz"
    styled, saved = work / "styled_jubelle.mscz", work / "styled_musescore.mscz"
    subprocess.run([str(tool), "set-jubelle-style", str(base), str(styled)], check=False)
    run(exe, ["-o", saved, styled], work / "styled_save.log")
    store = subprocess.run([str(tool), "store", str(saved)], capture_output=True, text=True) if saved.exists() else None
    restored = store.stdout.count("restored ") if store else 0
    dropped = 0
    if saved.exists():
        import zipfile
        with zipfile.ZipFile(saved) as z:
            main = [n for n in z.namelist() if n.endswith(".mscx") and not n.startswith("Excerpts/")][0]
            text = z.read(main).decode("utf-8", "replace")
            dropped = sum(1 for n in ("jubelleCourtesyAccidentals>", "jubelleMagneticLayout>", "jubelleNudgeLimit>")
                          if n not in text)
    style_ok = restored == 11
    failures += 0 if style_ok else 1

    md = ["## Jubelle engine ⇄ MuseScore 4.7.7 (files, both ways)", "",
          f"{len(sample)} scores. MIDI: MuseScore's export of the original and of the Jubelle-written file. "
          "Listing: the Jubelle engine's reading of MuseScore's save of the Jubelle-written file, against the original.", "",
          "| Score | Jubelle wrote it | MuseScore MIDI identical | MuseScore saved it | Jubelle reads the same music |",
          "|---|---|---|---|---|"]
    yn = lambda b: "n/a (not 4.x)" if b is None else ("yes" if b else "**no**")
    for r, ok in rows:
        md.append(f"| {r['score']} | {yn(r['rewritten'])} | {yn(r['midi_same'])} | {yn(r['musescore_saved'])} | {yn(r['listing_same'])} |")
    md += ["", f"Jubelle's 11 style settings: MuseScore's save dropped them ({dropped} of 3 checked were gone); "
               f"the Jubelle engine restored **{restored} of 11** from the engine store → {'PASS' if style_ok else 'FAIL'}", "",
           f"**{len(sample) - sum(1 for _, ok in rows if not ok)} of {len(sample)} scores passed**; failures: {failures}"]
    (work / "je_compat.md").write_text("\n".join(md) + "\n", encoding="utf-8")
    print("\n".join(md[-3:]))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
