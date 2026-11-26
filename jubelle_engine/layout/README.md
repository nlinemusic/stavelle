# jubelle_engine/layout

The staged layout pipeline (S7): shaping, measure spacing, windowed system breaking, vertical stacking and collision
avoidance, pages. Cached and incremental; measure-level stages run in parallel. Design:
`../docs/03_engine_architecture.md`, `../docs/11_pipeline_interfaces.md`.

Status: **started**. Style values and music fonts are in `../style`.

- `context` — what every stage needs: staves in score order, measures, each staff's clefs, keys and transpositions
  as shown.
- `notes` — stage ② begins: every note's staff line, notehead and accidental (MuseScore's accidental state, with
  Jubelle's courtesy accidentals). Compared with MuseScore's layout of every corpus score by
  `tools/jubelle_checks/je_layout_reference.py` (the reference tool's `Jubelle_Reference.ChordLayout`).
