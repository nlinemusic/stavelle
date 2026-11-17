#!/usr/bin/env python3
"""Jubelle: music-font compatibility check (fonts, F1; approved 15 Nov 2026).

A font is accepted only if the font file itself contains the symbols Jubelle's
notation uses (its character map; MuseScore measures and draws glyphs from the
font file) and it has the SMuFL metadata MuseScore's engine needs (engraving
defaults such as line thicknesses). Reported alongside Leland, Jubelle's default.

Usage: font_coverage.py <font.otf|ttf> <metadata.json> [more pairs...]
       font_coverage.py --bundled            (the fonts already in fonts/)
Exit code 1 if any font misses an essential glyph.
"""
import json
import pathlib
import sys

from fontTools.ttLib import TTFont

ROOT = pathlib.Path(__file__).resolve().parents[2]
GLYPHNAMES = json.loads((ROOT / "fonts/smufl/glyphnames.json").read_text(encoding="utf-8"))

# Core: what nearly every score uses. A font missing any of these is rejected.
CORE = {
    "clefs": ["gClef", "fClef", "cClef", "unpitchedPercussionClef1"],
    "noteheads": ["noteheadBlack", "noteheadHalf", "noteheadWhole", "noteheadXBlack"],
    "flags": ["flag8thUp", "flag8thDown", "flag16thUp", "flag16thDown"],
    "rests": ["restWhole", "restHalf", "restQuarter", "rest8th", "rest16th"],
    "accidentals": ["accidentalFlat", "accidentalNatural", "accidentalSharp"],
    "dynamics": ["dynamicPiano", "dynamicMezzo", "dynamicForte"],
    "articulations": ["articAccentAbove", "articStaccatoAbove", "articTenutoAbove", "articMarcatoAbove",
                      "fermataAbove"],
    "time signatures": ["timeSig0", "timeSig1", "timeSig2", "timeSig3", "timeSig4", "timeSig5", "timeSig6",
                        "timeSig7", "timeSig8", "timeSig9", "timeSigCommon", "timeSigCutCommon"],
    "other": ["augmentationDot", "segno", "coda"],
}
# Extended: missing ones are drawn with Bravura (MuseScore's fallback font), so
# they still appear, but in Bravura's style. Reported, not rejected.
EXTENDED = {
    "clefs": ["gClef8vb", "6stringTabClef"],
    "noteheads": ["noteheadDoubleWhole", "noteheadSlashHorizontalEnds", "noteheadSlashWhiteHalf"],
    "flags": ["flag32ndUp", "flag32ndDown"],
    "rests": ["rest32nd", "restHBar"],
    "accidentals": ["accidentalDoubleSharp", "accidentalDoubleFlat", "accidentalParensLeft"],
    "dynamics": ["dynamicSforzando", "dynamicRinforzando", "dynamicNiente"],
    "articulations": ["articStaccatissimoAbove", "fermataBelow", "brassMuteClosed", "brassMuteOpen"],
    "ornaments": ["ornamentTrill", "ornamentTurn", "ornamentTurnInverted", "ornamentMordent", "ornamentShortTrill"],
    "repeats": ["repeatDots", "repeat1Bar", "repeat2Bars"],
    "tuplets": ["tuplet3", "tuplet5"],
    "other": ["brace", "keyboardPedalPed", "graceNoteSlashStemUp", "ottavaAlta", "metNoteQuarterUp",
              "wiggleArpeggiatoUp"],
}
# Falls, doits, scoops and plops are drawn as lines by MuseScore, not as font glyphs.

def codepoint(name):
    cp = GLYPHNAMES.get(name, {}).get("codepoint", "")
    return int(cp[2:], 16) if cp.startswith("U+") else None


def check(font_path, metadata_path):
    font = TTFont(str(font_path), lazy=True)
    cmap = font.getBestCmap() or {}
    meta = json.loads(pathlib.Path(metadata_path).read_text(encoding="utf-8"))

    def missing_from(groups):
        return {a: [n for n in names if codepoint(n) not in cmap] for a, names in groups.items()
                if any(codepoint(n) not in cmap for n in names)}

    core_missing = missing_from(CORE)
    ext_missing = missing_from(EXTENDED)
    count = lambda g: sum(len(v) for v in g.values())
    smufl_cps = {int(v["codepoint"][2:], 16) for v in GLYPHNAMES.values() if v.get("codepoint", "").startswith("U+")}
    return {
        "font": pathlib.Path(font_path).name,
        "core": count(CORE) - count(core_missing), "coreTotal": count(CORE), "coreMissing": core_missing,
        "ext": count(EXTENDED) - count(ext_missing), "extTotal": count(EXTENDED), "extMissing": ext_missing,
        "smuflGlyphs": sum(1 for cp in cmap if cp in smufl_cps),
        "engravingDefaults": bool(meta.get("engravingDefaults")),
    }


def bundled():
    pairs = []
    for d in sorted((ROOT / "fonts").iterdir()):
        if not d.is_dir():
            continue
        metas = list(d.glob("*metadata*.json")) + [p for p in d.glob("*.json") if "metadata" not in p.name]
        fonts = [p for p in d.glob("*.otf") if "Text" not in p.name] + list(d.glob("*.ttf"))
        if metas and fonts:
            pairs.append((fonts[0], metas[0]))
    return pairs


def main(argv):
    pairs = bundled() if argv[1:] == ["--bundled"] else list(zip(argv[1::2], argv[2::2]))
    failed = False
    print("| Font | Core symbols | Extended symbols | SMuFL glyphs | Engraving defaults | Result | Drawn with Bravura instead |")
    print("|---|---:|---:|---:|---|---|---|")
    for f, m in pairs:
        r = check(f, m)
        ok = not r["coreMissing"] and r["engravingDefaults"]
        failed |= not ok
        ext = "; ".join(f"{a}: {', '.join(n)}" for a, n in r["extMissing"].items()) or "none"
        result = "accepted" if ok else "REJECTED (core: " + "; ".join(
            f"{a}: {', '.join(n)}" for a, n in r["coreMissing"].items()) + ")"
        print(f"| {r['font']} | {r['core']}/{r['coreTotal']} | {r['ext']}/{r['extTotal']} | {r['smuflGlyphs']} | "
              f"{'yes' if r['engravingDefaults'] else 'NO'} | {result} | {ext} |")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
