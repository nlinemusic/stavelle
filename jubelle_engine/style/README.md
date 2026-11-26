# jubelle_engine/style

Style values and music fonts (S7), in the `je_layout` library.

- `style.h` — a score's style: MuseScore's values by MuseScore's names (`Sid`), read as MuseScore reads a score's
  style (old names, version changes, part defaults). `sid.h` and `styledefaults.cpp` are generated from MuseScore's
  definitions by `tools/jubelle_checks/je_gen_style.py` (from `tools/jubelle_checks/data/musescore_style_defaults.txt`,
  dumped by the reference tool); compared with MuseScore on every corpus score by `je_style_reference.py`.
- `font.h` — music fonts: every symbol (`SymId`, `symid.h` and `symbols.cpp`, generated) with metrics from the
  font's outlines (FreeType, MuseScore's bundled copy), SMuFL anchors, alternates, composed glyphs, Bravura fallback.
- `json.h` — a small JSON reader for the fonts' metadata.

Units are MuseScore's: engraving units (1/1200 inch) for the spatium and symbol metrics, staff spaces for spatium
values.
