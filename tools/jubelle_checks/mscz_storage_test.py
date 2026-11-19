#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Jubelle engine check (decision D-E10): what official MuseScore 4.7.7 does with
Jubelle's extra data in a .mscz file.

For each way Jubelle can store its own data, a variant of the same 4.x score is
made, then MuseScore:
  1. opens it and writes a PDF (the file opens);
  2. writes MIDI, compared with the unchanged score's MIDI (the music is read the same);
  3. saves it as a new .mscz, which is then inspected: does the Jubelle data survive
     MuseScore saving the score?
MuseScore's log for each run is searched for warnings about the file.

Ways tested:
  - folder:   a Jubelle/ folder inside the ZIP (engine.json, condensing.json)
  - metatag:  a score meta tag <metaTag name="jubelleTest...">
  - style:    an unknown style value in score_style.mss (as Jubelle's Sid::jubelle... values)
  - element:  an unknown tag inside a chord in score.mscx
  - part:     an unknown style value in a part's .mss
  - all:      all of the above at once
  - real:     the score exactly as Jubelle saved it (its real Jubelle style values and meta tags)

Writes mscz_storage.md and .json. Exit code 1 if MuseScore fails to open a variant or
reads different music from it.

Usage: mscz_storage_test.py <MuseScore exe> <Jubelle-saved 4.x .mscz> <work dir>
"""
import html
import json
import re
import subprocess
import sys
import zipfile
from pathlib import Path

MARK = "jubelleStorageProbe"
# about 120 kB of JSON with every character XML must escape, and non-ASCII text
BIG_VALUE = json.dumps({"schema": "jubelle.store/1", "note": "quotes \" & <tags> ’ é ♭ ♯ 𝄞",
                        "items": [{"eid": f"E{i}", "t": [i, 480], "v": "x" * 40} for i in range(1500)]},
                       ensure_ascii=False)


def run(exe, args, log, timeout=240):
    with open(log, "w", encoding="utf-8", errors="replace") as f:
        try:
            rc = subprocess.run([str(exe)] + [str(a) for a in args], stdout=f, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            rc = "timeout"
    subprocess.run(["taskkill", "/F", "/IM", "MuseScore4.exe"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) \
        if sys.platform == "win32" else None
    return rc


def rewrite(src, dst, edit):
    """Copies the ZIP, letting edit(name, bytes) change entries and return extra entries."""
    with zipfile.ZipFile(src) as zin, zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as zout:
        extra = {}
        for info in zin.infolist():
            data = zin.read(info.filename)
            new = edit(info.filename, data, extra)
            zout.writestr(info.filename, new if new is not None else data)
        for name, data in extra.items():
            zout.writestr(name, data)


def main_mscx(names):
    return next(n for n in names if n.endswith(".mscx") and not n.startswith("Excerpts/"))


def variants(base, work):
    with zipfile.ZipFile(base) as z:
        names = z.namelist()
    mscx = main_mscx(names)
    part_mss = next((n for n in names if n.startswith("Excerpts/") and n.endswith(".mss")), None)

    def add_folder(name, data, extra):
        extra["Jubelle/engine.json"] = json.dumps({"schema": "jubelle.engine/1", MARK: "folder"}).encode()
        extra["Jubelle/condensing.json"] = json.dumps({"layouts": [], MARK: "folder"}).encode()

    def add_metatag(name, data, extra):
        if name == mscx:
            s = data.decode("utf-8")
            i = s.find("<metaTag ")
            if i < 0:
                i = s.find("<Score>") + len("<Score>")
            return (s[:i] + f'<metaTag name="{MARK}">metatag</metaTag>\n    ' + s[i:]).encode("utf-8")

    def add_style(name, data, extra):
        if name == "score_style.mss":
            s = data.decode("utf-8")
            i = s.find("<Style>") + len("<Style>")
            return (s[:i] + f"\n    <{MARK}>style</{MARK}>" + s[i:]).encode("utf-8")

    def add_element(name, data, extra):
        if name == mscx:
            s = data.decode("utf-8")
            i = s.find("<Chord>") + len("<Chord>")
            return (s[:i] + f"\n            <{MARK}>element</{MARK}>" + s[i:]).encode("utf-8")

    def add_part(name, data, extra):
        if part_mss and name == part_mss:
            s = data.decode("utf-8")
            i = s.find("<Style>") + len("<Style>")
            return (s[:i] + f"\n    <{MARK}>part</{MARK}>" + s[i:]).encode("utf-8")

    def add_bigtag(name, data, extra):
        if name == mscx:
            s = data.decode("utf-8")
            i = s.find("<metaTag ")
            if i < 0:
                i = s.find("<Score>") + len("<Score>")
            value = BIG_VALUE.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")
            return (s[:i] + f'<metaTag name="{MARK}Big">{value}</metaTag>\n    ' + s[i:]).encode("utf-8")

    def add_all(name, data, extra):
        for f in (add_folder, add_metatag, add_style, add_element, add_part):
            new = f(name, data, extra)
            if new is not None:
                data = new
        return data

    out = {}
    for label, fn in (("folder", add_folder), ("metatag", add_metatag), ("bigtag", add_bigtag), ("style", add_style),
                      ("element", add_element), ("part", add_part), ("all", add_all)):
        dst = work / f"variant_{label}.mscz"
        rewrite(base, dst, fn)
        out[label] = dst
    out["real"] = base
    return out


def probe(mscz):
    """Where the probe (or Jubelle's real data) is found in a saved .mscz."""
    found = {"folder": False, "metatag": False, "bigtag": False, "style": False, "element": False, "part": False}
    real = {"jubelleStyles": 0, "jubelleMetaTags": 0, "eids": set()}
    with zipfile.ZipFile(mscz) as z:
        names = z.namelist()
        found["folder"] = any(n.startswith("Jubelle/") for n in names)
        for n in names:
            text = z.read(n).decode("utf-8", errors="replace") if n.endswith((".mscx", ".mss", ".json")) else ""
            if n.endswith(".mscx") and not n.startswith("Excerpts/"):
                found["metatag"] = f'name="{MARK}"' in text
                found["element"] = f"<{MARK}>element" in text
                real["jubelleMetaTags"] = text.count('<metaTag name="jubelle')
                real["eids"] = set(re.findall(r"<eid>([^<]*)</eid>", text))
                m = re.search(f'<metaTag name="{MARK}Big">(.*?)</metaTag>', text, re.S)
                if m:
                    v = html.unescape(m.group(1))
                    found["bigtag"] = v == BIG_VALUE
            if n == "score_style.mss":
                found["style"] = f"<{MARK}>style" in text
                real["jubelleStyles"] = text.count("<jubelle")
            if n.startswith("Excerpts/") and n.endswith(".mss") and f"<{MARK}>part" in text:
                found["part"] = True
    return names, found, real


def eids_by_type(mscz):
    """eid -> the element tag that holds it (main score only)."""
    with zipfile.ZipFile(mscz) as z:
        text = z.read(main_mscx(z.namelist())).decode("utf-8", errors="replace")
    out, stack = {}, []
    for m in re.finditer(r"<eid>([^<]*)</eid>|<(/?)([A-Za-z][A-Za-z0-9_]*)([^>]*?)(/?)>", text):
        if m.group(1) is not None:
            if stack:
                out[m.group(1)] = stack[-1]
            continue
        closing, tag, selfclose = m.group(2), m.group(3), m.group(5)
        if tag == "eid":
            continue
        if closing:
            if stack and stack[-1] == tag:
                stack.pop()
        elif not selfclose:
            stack.append(tag)
    return out


def warnings_in(log):
    try:
        text = Path(log).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    keys = ("corrupt", "unknown", "error", "failed", "jubelle", MARK.lower())
    return [l.strip()[:200] for l in text.splitlines() if any(k in l.lower() for k in keys)][:12]


def main():
    exe, base, work = Path(sys.argv[1]), Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve()
    work.mkdir(parents=True, exist_ok=True)

    ref_mid = work / "reference.mid"
    rc = run(exe, ["-o", ref_mid, base], work / "reference.log")
    if not ref_mid.exists():
        print(f"MuseScore could not export the unchanged score (exit {rc})")
        return 1
    ref = ref_mid.read_bytes()
    _, _, real_before = probe(base)

    results, ok = [], True
    for label, src in variants(base, work).items():
        r = {"variant": label}
        pdf, mid, saved = work / f"{label}.pdf", work / f"{label}.mid", work / f"{label}_saved_by_musescore.mscz"
        r["opens"] = run(exe, ["-o", pdf, src], work / f"{label}_pdf.log") == 0 and pdf.exists() and pdf.stat().st_size > 1000
        run(exe, ["-o", mid, src], work / f"{label}_mid.log")
        r["same_music"] = mid.exists() and mid.read_bytes() == ref
        run(exe, ["-o", saved, src], work / f"{label}_save.log")
        r["saved"] = saved.exists()
        if saved.exists():
            names, found, real = probe(saved)
            r["survives_musescore_save"] = found if label != "real" else None
            r["real_after_save"] = ({k: v for k, v in real.items() if k != "eids"} if label == "real" else None)
            if label == "real":
                r["eids_kept"] = f"{len(real_before['eids'] & real['eids'])} of {len(real_before['eids'])}"
                before_t, after_t = eids_by_type(base), eids_by_type(saved)
                per = {}
                for eid, tag in before_t.items():
                    k = per.setdefault(tag, [0, 0])
                    k[0] += 1
                    if after_t.get(eid) == tag:
                        k[1] += 1
                r["eids_by_type"] = {t: f"{v[1]}/{v[0]}" for t, v in sorted(per.items(), key=lambda x: -x[1][0])}
            r["saved_entries"] = sorted(n for n in names if not n.startswith("Excerpts/"))
        r["log_lines"] = warnings_in(work / f"{label}_pdf.log")
        if label != "real":
            ok = ok and r["opens"] and r["same_music"]
        results.append(r)

    lines = ["# Jubelle data inside .mscz: what MuseScore 4.7.7 does with it", "",
             f"Base score: `{base.name}` (saved by Jubelle). Opened, exported and re-saved by official MuseScore.", "",
             "| Storage | Opens | Same music (MIDI identical) | Kept when MuseScore saves |", "|---|---|---|---|"]
    for r in results:
        if r["variant"] == "real":
            kept = (f"Jubelle style values {real_before['jubelleStyles']} → {r['real_after_save']['jubelleStyles']}, "
                    f"meta tags {real_before['jubelleMetaTags']} → {r['real_after_save']['jubelleMetaTags']}, "
                    f"element IDs (eid) kept {r.get('eids_kept')}"
                    if r.get("real_after_save") else "not saved")
        else:
            s = r.get("survives_musescore_save") or {}
            want = [r["variant"]] if r["variant"] != "all" else ["folder", "metatag", "style", "element", "part"]
            if r["variant"] == "bigtag":
                want = ["bigtag (120 kB JSON, exact value)"]
                s = {want[0]: s.get("bigtag")}
            kept = ", ".join(f"{k}: {'yes' if s.get(k) else 'no'}" for k in want) if s else "not saved"
        lines.append(f"| {r['variant']} | {'yes' if r['opens'] else '**no**'} | {'yes' if r['same_music'] else '**no**'} | {kept} |")
    for r in results:
        if r.get("eids_by_type"):
            lines += ["", "Element IDs (`eid`) kept by MuseScore's save, by element (kept/total):", ""]
            lines += ["| Element | Kept |", "|---|---|"] + [f"| {t} | {v} |" for t, v in r["eids_by_type"].items()]
    lines += ["", "MuseScore's log lines mentioning the file (first variant with any):"]
    for r in results:
        if r["log_lines"]:
            lines += [f"- {r['variant']}:"] + [f"  - `{l}`" for l in r["log_lines"]]
    lines += ["", f"**Result: {'PASS' if ok else 'FAIL'}** (every variant opens and plays the same music in MuseScore)"]
    md = "\n".join(lines) + "\n"
    (work / "mscz_storage.md").write_text(md, encoding="utf-8")
    (work / "mscz_storage.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(md, flush=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
