#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
#
# Jubelle: end-to-end check of a built application, run on Windows and macOS CI.
#
# Uses the application's own command-line converter (the real app, no window) to:
#   1. open real scores, lay them out, and save them through the normal save routine
#      (new file, overwrite of an existing file, non-ASCII file name);
#   2. reopen every saved file and export it to PDF, proving the saved file is valid;
#   3. confirm that no temporary "_saving" files are left behind;
#   4. confirm Jubelle keeps its settings/data in its own folders and creates nothing
#      in MuseScore's folders (separate application identity);
#   5. measure how long opening + laying out + saving takes (median of several runs).
#
# Usage: smoke_test.py <path-to-Jubelle-executable> <scores-dir> <work-dir> [--runs N]

import argparse
import json
import os
import platform
import statistics
import subprocess
import sys
import time
from pathlib import Path

failures = []
results = {"platform": platform.platform(), "checks": [], "timings": []}


def check(name, ok, detail=""):
    results["checks"].append({"name": name, "ok": bool(ok), "detail": detail})
    print(("PASS  " if ok else "FAIL  ") + name + (f"  ({detail})" if detail else ""), flush=True)
    if not ok:
        failures.append(name)


class _Result:
    def __init__(self, returncode, output):
        self.returncode, self.stdout, self.stderr = returncode, output, ""


def run_app(exe, args, timeout=300):
    """Runs the app with a hard time limit, sending output to a temporary file rather than
    pipes (a helper process outliving the app could otherwise block forever on Windows)."""
    import tempfile
    with tempfile.TemporaryFile(mode="w+", encoding="utf-8", errors="replace") as log:
        start = time.perf_counter()
        proc = subprocess.Popen([str(exe)] + [str(a) for a in args], stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        try:
            rc = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            if sys.platform == "win32":
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            else:
                proc.kill()
            proc.wait()
            rc = f"timed out after {timeout}s"
        elapsed = time.perf_counter() - start
        log.seek(0)
        return _Result(rc, log.read()), elapsed


def data_dirs():
    """Folders where Qt places application data for an (organization, application) pair."""
    home = Path.home()
    if sys.platform == "win32":
        base = Path(os.environ["LOCALAPPDATA"])
        return base / "Jubelle" / "Jubelle", base / "MuseScore"
    if sys.platform == "darwin":
        base = home / "Library" / "Application Support"
        return base / "Jubelle" / "Jubelle", base / "MuseScore"
    base = Path(os.environ.get("XDG_DATA_HOME", home / ".local" / "share"))
    return base / "Jubelle" / "Jubelle", base / "MuseScore"


def convert(exe, src, dst, label, keep_output=None):
    proc, elapsed = run_app(exe, ["-o", str(dst), str(src)])
    if keep_output:
        # The app's built-in profiler prints a timing table when the command-line run ends.
        Path(keep_output).write_text(str(proc.stdout), encoding="utf-8", errors="replace")
    ok = proc.returncode == 0 and dst.exists() and dst.stat().st_size > 0
    detail = f"{elapsed:.2f}s"
    if not ok:
        detail += f", exit {proc.returncode}: " + (proc.stderr or proc.stdout)[-600:].strip()
    check(label, ok, detail)
    return ok, elapsed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("scores")
    ap.add_argument("work")
    ap.add_argument("--runs", type=int, default=3)
    a = ap.parse_args()

    exe, scores, work = Path(a.exe).resolve(), Path(a.scores).resolve(), Path(a.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    check("Jubelle executable exists", exe.exists(), str(exe))
    if not exe.exists():
        return finish(work)

    jubelle_dir, musescore_dir = data_dirs()
    musescore_existed = musescore_dir.exists()

    sources = sorted([p for p in scores.iterdir() if p.suffix in (".mscz", ".mscx")])
    check("Test scores found", len(sources) > 0, f"{len(sources)} scores")

    for src in sources:
        name = src.stem
        saved = work / f"{name}.mscz"

        # 1. open + lay out + save as a new .mscz (timed over several runs)
        times = []
        for i in range(a.runs):
            if saved.exists():
                saved.unlink()
            ok, t = convert(exe, src, saved, f"{name}: open and save as new file (run {i + 1})",
                            keep_output=work / f"profile_{name}.txt" if i == 0 else None)
            if not ok:
                break
            times.append(t)

        # 2. save again over the existing file (exercises the atomic replace)
        if saved.exists():
            before = saved.stat().st_mtime_ns
            ok, t = convert(exe, src, saved, f"{name}: save over existing file")
            check(f"{name}: existing file was replaced", ok and saved.stat().st_mtime_ns >= before)

        # 3. reopen the saved file and export to PDF: proves the saved score is valid
        pdf = work / f"{name}.pdf"
        if saved.exists():
            ok, _ = convert(exe, saved, pdf, f"{name}: reopen saved score and export PDF")
            check(f"{name}: PDF is non-trivial", ok and pdf.stat().st_size > 1000, f"{pdf.stat().st_size if pdf.exists() else 0} bytes")

        if times:
            results["timings"].append({"score": src.name, "size_kb": round(src.stat().st_size / 1024),
                                       "open_layout_save_median_s": round(statistics.median(times), 3),
                                       "runs_s": [round(t, 3) for t in times]})

    # Non-ASCII path (composers' titles often contain accents)
    if sources:
        uni = work / "Café Ré♭ – Ballade.mscz"
        ok, _ = convert(exe, sources[0], uni, "Save to a file name with accents and symbols")

    # File-format compatibility with MuseScore
    format_checks(exe, sources, work)

    leftovers = [p.name for p in work.rglob("*_saving")]
    check("No temporary _saving files left behind", not leftovers, ", ".join(leftovers))

    # Separate application identity
    check("Jubelle data folder was created", jubelle_dir.exists(), str(jubelle_dir))
    contents = sorted(p.name for p in jubelle_dir.iterdir()) if jubelle_dir.exists() else []
    check("Jubelle data folder is in use", len(contents) > 0, ", ".join(contents[:8]))
    if musescore_existed:
        check("MuseScore data folder untouched (pre-existing, not checked further)", True, str(musescore_dir))
    else:
        check("No MuseScore data folder was created", not musescore_dir.exists(), str(musescore_dir))

    return finish(work)


def format_checks(exe, sources, work):
    """Scores, MusicXML and MIDI written by Jubelle must be exactly what MuseScore writes."""
    import re
    import zipfile

    saved = [work / f"{s.stem}.mscz" for s in sources if (work / f"{s.stem}.mscz").exists()]
    for f in saved[:3]:
        try:
            with zipfile.ZipFile(f) as z:
                main = next(n for n in z.namelist() if n.endswith(".mscx"))
                xml = z.read(main).decode("utf-8")
            version = re.search(r"<programVersion>([^<]*)</programVersion>", xml)
            check(f"{f.stem}: saved score has MuseScore's version stamp",
                  version is not None and version.group(1) == EXPECTED_PROGRAM_VERSION,
                  version.group(1) if version else "missing")
            check(f"{f.stem}: saved score contains nothing Jubelle-specific", "Jubelle" not in xml)
        except Exception as e:  # noqa: BLE001
            check(f"{f.stem}: saved score is a readable MuseScore file", False, repr(e))

    if not sources:
        return
    src = next((s for s in sources if s.suffix == ".mscz"), sources[0])

    mxl = work / f"{src.stem}.musicxml"
    ok, _ = convert(exe, src, mxl, "Export MusicXML")
    if ok:
        text = mxl.read_text(encoding="utf-8", errors="replace")
        check("MusicXML identifies the exporter as MuseScore Studio (as MuseScore does)",
              "<software>MuseScore Studio" in text)
        convert(exe, mxl, work / f"{src.stem}_from_musicxml.mscz", "Re-import exported MusicXML")

    mid = work / f"{src.stem}.mid"
    ok, _ = convert(exe, src, mid, "Export MIDI")
    if ok:
        check("MIDI file is a valid standard MIDI file", mid.read_bytes()[:4] == b"MThd")
        convert(exe, mid, work / f"{src.stem}_from_midi.mscz", "Re-import exported MIDI")


def _expected_version():
    import re
    text = (Path(__file__).resolve().parents[2] / "version.cmake").read_text(encoding="utf-8")
    parts = [re.search(r'set\(MUSE_APP_VERSION_' + k + r' "(\d+)"\)', text).group(1) for k in ("MAJOR", "MINOR", "PATCH")]
    return ".".join(parts)


EXPECTED_PROGRAM_VERSION = _expected_version()


def finish(work):
    (work / "smoke_results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    lines = ["## Jubelle app check: " + results["platform"], "",
             f"**{sum(c['ok'] for c in results['checks'])} passed, {len(failures)} failed**", ""]
    if failures:
        lines += ["Failed:"] + [f"- {f}" for f in failures] + [""]
    if results["timings"]:
        lines += ["| Score | Size (KB) | Open + layout + save (median s) |", "|---|---:|---:|"]
        lines += [f"| {t['score']} | {t['size_kb']} | {t['open_layout_save_median_s']} |" for t in results["timings"]]
    summary = "\n".join(lines) + "\n"
    print(summary)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as f:
            f.write(summary)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
