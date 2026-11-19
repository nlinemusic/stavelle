# 11 · Layout pipeline interfaces, cache keys and dependency rules (Phase 2.5)

Each stage is a pure function of a **snapshot**, the **layout settings** and earlier stage results. Results are cached
by key; a change set invalidates keys through the **dependency index**.

| Stage | Unit | Inputs | Output | Cache key |
|---|---|---|---|---|
| ① Presentation | (layout, group, phrase) | players' events in range, layout's staff assignment and condensing settings and changes | `StaffPlan`, presentation staves and events | hash(events in range, group settings, changes in range, style subset) |
| ② Shaping | (layout, presentation staff, measure) | presentation events of the measure, clef/key/time in force, accidental state at the measure start, style | chord shapes (heads, stems, beams, accidentals, dots, ties' local parts), the skyline of the measure-staff | hash(events, context in force, accidental-state-in hash, style subset, font) |
| ③ Spacing | (layout, measure) | ② for every staff in the measure, column elements | segment columns, minimum width, spring constants | hash of the ② keys + column elements + spacing style |
| ④ System breaking | (layout, window of measures) | ③ widths, breaks and locks, page/system sizes | system list (measure ranges) | hash(③ keys in window, breaks, locks, page settings) |
| ⑤ Vertical | (layout, system) | ② results placed by ③, spanners on the system, attachments, staff distances, Jubelle magnetic rows and collision rules | placed items, staff positions, system height | hash(system range, ② keys, spanner shapes in range, vertical style) |
| ⑥ Pages | (layout, page) | ⑤ heights, frames, page style | page list with system positions | hash(⑤ heights, frames, page style) |
| ⑦ Display list | (layout, page) | ⑤ and ⑥ | primitives with IDs | hash(⑥ page, ⑤ keys) |

## Dependency rules (what a change invalidates)
| Change | Invalidates |
|---|---|
| a note or event in measure m, player p | ① phrase(s) containing it; ② (staff(p), m) and (staff(p), m+1) only if the accidental state at the end of m changed; ③ m; ④ the window containing m; ⑤ the system containing m; ⑥ the page if heights changed |
| a spanner | ⑤ the systems it is drawn on (start to end), **not** ②/③ of the measures it spans (unless it affects spacing, e.g. glissando, arpeggio, lyrics lines) |
| clef, key, time signature at m | ② from m to the next change of the same kind on that staff; ③ those measures |
| a layout override (offset, placement) | ⑤ of the system containing the item |
| a break or lock | ④ the window(s) concerned |
| style | the stages whose key includes that style subset (style values are grouped by the stage that reads them) |
| condensing setting or change | ① phrases from the change's time to the next change of that group; then as a note change |

## Execution
- **Order:** visible pages first (the view tells the scheduler which pages are on screen), then the rest in score
  order in the background. Export, print and save of PDFs wait for "layout complete".
- **Parallelism:** ② per (staff, measure) and ⑤ per system run on `je::exec` workers; ④ and ⑥ are sequential per
  layout, and cheap.
- **Windowed breaking (④):** optimise breaks inside a window (default: two pages' worth of systems). If the last
  break of the window moves, the next window is re-run; otherwise the cascade stops. This bounds edit cost on large
  scores.
- **Compatibility mode:** for the Phase 4 comparison, ④ can run MuseScore's greedy rule exactly, so results can be
  compared like for like. The stable window method is measured separately and becomes the default only after review.
- **Determinism:** results don't depend on thread order (pure stages, stable reductions); tested by running
  shuffled schedules.

## Memory
- Cache budget per score (default 64 MB for stage results, configurable with the performance profiles); least
  recently used entries of off-screen pages are dropped first and recomputed on demand.
- Snapshots share unchanged nodes (copy-on-write per measure × voice), so keeping the previous snapshot for undo or
  background work costs only what changed.
