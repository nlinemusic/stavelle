# jubelle_engine/layout

The staged layout pipeline (S7): shaping, measure spacing, windowed system breaking, vertical stacking and collision
avoidance, pages. Cached and incremental; measure-level stages run in parallel. Design:
`../docs/03_engine_architecture.md`, `../docs/11_pipeline_interfaces.md`.

Status: **started** — style values and music fonts are in `../style`; chord shaping is next, compared with
MuseScore's chord layout (the reference tool's `Jubelle_Reference.ChordLayout`).
