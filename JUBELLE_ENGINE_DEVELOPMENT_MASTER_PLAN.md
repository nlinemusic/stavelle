# Jubelle Engine Development Master Plan

**The central reference for the next-generation Jubelle engine.** Keep it current: every milestone, decision or
change of direction is recorded here (and in detail in `jubelle_engine/docs/`) in the same commit as the work.

| | |
|---|---|
| Last updated | 27 Nov 2026 |
| Goal | **A complete, independent Jubelle engine** (D-E22): MuseScore's engine isn't responsible for editing, playback, audio, drawing, layout or files in the finished app |
| Current work | **S7 layout** (style, fonts and the first chord shaping stages done: notes, beams, stems and flags, mirroring, note positions; voice offsets next), then S8 drawing, with S5 playback and S6 audio alongside (S1 model, S2 file handling, S3 editing and S4 undo, autosave and crash recovery done; follow-ups listed in §15) |
| Next testing release | **only when the complete engine passes the gates on Windows and macOS** (`docs/14_full_engine_roadmap.md` §5); a long timeline is accepted |
| Branch | `engine/next` (development area: `jubelle_engine/`) |
| Stable release | **Jubelle v4.7.7.261118230**, https://github.com/nlinemusic/stavelle/releases/tag/v4.7.7.261118230 |
| Stable recovery points | tag `v4.7.7.261118230`; branch `checkpoint/stable-4.7.7.261118230` (commit `207614e5`) |
| Condensing | **approved 18 Nov 2026** (configurable groups, labels "Tpt. 1.2"); subsystem S9 of the new engine |

---

## 1. Objectives
Give Jubelle its own notation engine, designed around its needs, that:
1. edits faster (large orchestral and big band scores feel instant: ≤ 16 ms per edit);
2. lays out and engraves faster and better, including stable system breaks;
3. handles large and complex scores with less memory;
4. plays back efficiently (incremental event updates);
5. has reliable memory management, and strong undo and redo;
6. loads and saves MuseScore 4.x files reliably;
7. supports every existing Jubelle feature;
8. makes **live automatic condensing** a native capability;
9. is modular and maintainable.

**Faster wherever technically possible (user's requirement, 25 Nov 2026):** every subsystem is designed for
performance from the start and benchmarked against the current MuseScore-based Jubelle on the same scores,
machine and operations (loading, editing, selection, undo/redo, saving, playback, engraving, layout, rendering,
memory); bottlenecks are found and fixed, never at the cost of accuracy, notation quality, compatibility or
reliability. Results and method: `jubelle_engine/docs/16_performance.md`.

**What doesn't change:** the Jubelle interface, menus, workflows, file format and features. Users see a faster
Jubelle that can condense, not a different program.

**Independence (decision D-E22, 18 Nov 2026):** in the finished application, every core function is the Jubelle
engine's own: model and data management, editing and note entry, undo and redo, playback and audio processing,
engraving and layout, page rendering and drawing, live condensing, and file handling. MuseScore's engine is a
reference and comparison baseline (in the test tools) and is **removed from the app build**. Its GPL code may be
ported with notices; independent third-party libraries (FluidSynth, FFmpeg, LAME, FLAC, Vorbis, Opus, VST3 SDK,
Qt) stay. Correctness, stability, performance and Jubelle's features come before speed.

## 2. Protecting the stable release
- `main` and the stable release are not changed by engine work. **No new testing release** is published until the
  complete engine passes the gates of `docs/14_full_engine_roadmap.md` §5 on Windows and macOS.
- `engine/next` is merged *from* `main` regularly, so the engine always develops against the current Jubelle.
- The stable release can always be rebuilt from `checkpoint/stable-4.7.7.261118230` / `v4.7.7.261118230`.
- No experimental engine change is ever made on `main`, the checkpoint, or a release.

## 3. Development environment
```text
repository nlinemusic/stavelle
├── main                                  releases (unchanged by engine work)
├── checkpoint/stable-4.7.7.261118230     the stable baseline (never moves)
└── engine/next                           the engine initiative
    ├── JUBELLE_ENGINE_DEVELOPMENT_MASTER_PLAN.md   (this file)
    └── jubelle_engine/
        ├── docs/        01 audit · 02 integration map · 03 architecture · 04 condensing spec
        │                05 licensing · 06 roadmap and testing · 07 decisions log · 08 AAC licensing
        │                09 model mapping · 10 edit vocabulary · 11 pipeline interfaces
        │                12 bridge migration · 13 .mscz storage results · 14 full engine roadmap
        │                15 file handling (S2)
        ├── CMakeLists.txt   standalone build (C++17, no Qt), GoogleTest, sanitizer options
        ├── corpus/      corpus description (the user's scores are never in Git: D-E17)
        ├── model/ edit/ present/ layout/ render/ perform/ io/ style/ exec/   engine modules (plain C++17)
        ├── bridge/      Qt-side integration (INotation*, DOM ⇄ model converter, plugin API)
        └── tests/ bench/
```
Topic branches: `engine/<milestone>` → `engine/next`. Local work uses a separate git worktree for `engine/next`,
so the release tree is never touched.

## 4. Architecture findings (Phase 1 summary; detail in `docs/01_architecture_audit.md`)
1. **The engine is the `engraving` library** (DOM 132k lines, layout 50k, read/write 41k, editing 25k, playback 12k).
   The `notation` module wraps it with `INotation*` interfaces, but **317 files outside the engine use the DOM
   directly** (the score view, Properties, palettes, importers). The engine can't simply be swapped behind an
   interface.
2. **Parts are full copies of the score**, kept in sync by links. That costs memory (115 MB on the 39-staff
   orchestra) and time (every edit is repeated and laid out per open part).
3. **Model and layout are mixed:** layout results live inside the musical objects, and layout modifies the DOM.
   Nothing can be cached, computed on another thread, or shown two ways at once.
4. **One staff holds one instrument's music:** two players' notes can't share a stem. This is the structural
   blocker for live condensing.
5. **Edits re-lay out from the edited system to the point where pages stop changing,** after widening the range to
   every overlapping spanner's full length. This is why one note on the 39 × 285 score costs 121 ms, and undo 137 ms.
6. **Undo** stores pointer-based low-level commands (84 kinds), and every change must be wrapped by hand.
7. **Saving** writes the score and every part in full: 1.3 s on the largest score.
8. **Judged sound in the audit:** the audio engine, MPE, the draw backend, MuseScore's file readers and the
   importers. (The audit proposed keeping them; since D-E22 they are replaced too and serve as comparison
   baselines. Independent third-party libraries, instrument data and SMuFL fonts stay.)
9. **Jubelle's additions** sit mostly *on top* of the DOM (notation-level logic in plain C++ with tests). The
   engine-level changes are small and localised (about 2,000 lines in 38 engraving files). Most features port by
   moving their logic onto the new model's API (`docs/02_jubelle_integration_map.md`).

## 5. The new engine (detail in `docs/03_engine_architecture.md`)
- **Principles:** music, presentation and layout are separate layers; everything visible is derived and cached;
  stable 64-bit IDs; transactions and change sets; plain C++17 without Qt; incremental migration.
- **Modules:** `je::model` (player-centred music, snapshots) · `je::edit` (transactions, undo, hooks) ·
  `je::present` (layouts, staff assignment, **condensing**, divisi) · `je::layout` (staged, cached, parallel
  pipeline) · `je::render` (display list) · `je::perform` (events → MPE) · `je::io` (native 4.x reader/writer) ·
  `je::style` · `je::exec` · `je::bridge` (Qt side).
- **Parts are layouts**, not copies. Per-layout property overrides replace "unlinked" part elements.
- **Layout pipeline:** presentation → shaping per measure-staff → spacing → windowed system breaking → vertical →
  pages → display list. Precise dependencies; no range widening; visible pages first; deterministic.
- **Integration (revised, D-E9):** each subsystem is compared with MuseScore's engine in the test tools; the
  interface moves to the engine API (S10); then MuseScore's engine is removed from the app build.
- **Subsystems** (`docs/14_full_engine_roadmap.md`): S1 model and data · S2 files and serialization · S3 editing and
  note entry · S4 undo and redo · S5 playback · S6 audio processing (`je::audio`) · S7 engraving and layout · S8
  page rendering and drawing · S9 presentation and live condensing · S10 integration.

## 6. MuseScore dependencies (revised 18 Nov 2026, D-E22)
| Replaced by the Jubelle engine (not in the finished app) | Stays: the application around the engine, and third-party libraries |
|---|---|
| engraving: DOM, layout, editing and undo, read/write, playback | the Jubelle interface and its Qt/QML application framework (ui, actions, shortcuts, workspaces), rewired to the engine API (D-E23) |
| framework/mpe and framework/audio's engine (scheduler, mixer, synth hosting, export) | FluidSynth, FFmpeg, LAME, FLAC, Vorbis, Opus, VST3 SDK, Qt |
| framework/draw's engine-facing painting; importexport's DOM-based importers | SMuFL fonts and their metadata; instrument definitions (data) |
| the `notation` module's DOM wrappers | MuseScore's style and property names, kept as the file vocabulary for compatibility |

## 7. Jubelle-specific systems
Engine-level: courtesy accidentals, engraving rules, magnetic layout, overlap checker, jazz look, articulation
playback, swing feel, jazz falls, repeat controls. Notation-level: linked passages, harmony system, chord identifier,
proofread, find and select, condensed copy, part layout, prepare parts, page turns, page alignment, templates, tempo
tapping, timeline markers, drum stems. Audio and app-level features are independent of the engine. Routes and
phases for each: `docs/02_jubelle_integration_map.md`.

## 8. Licensing (detail in `docs/05_licensing.md`)
- Jubelle and the new engine are **GPL v3** (decision D-E3). MuseScore code may be ported, keeping its notices.
- "MuseScore" is a trademark: Jubelle says "based on MuseScore Studio"; the engine is "the Jubelle engine".
- **Risk R-L1, investigated** (`docs/08_aac_encoder_licensing.md`): Jubelle statically links **Fraunhofer FDK AAC
  2.0.3** (inherited from MuseScore) for AAC export and MP4 video sound. (a) Its licence requires its full text with
  binary distributions; the installers don't include it (a gap, easy to fix). (b) The FSF and Debian regard its
  licence as incompatible with every GPL version (no patent licence), so distributing the combined GPL binary is a
  compliance risk for Jubelle, which doesn't hold MuseScore's copyrights. Personal sharing is still distribution.
  (c) AAC-LC US patents have expired. **Done (18 Nov, approved):** FDK is deleted from the source; AAC export and
  the MP4 soundtrack use **FFmpeg's own AAC encoder** (LGPL 2.1+, compatible with GPL v3; loaded at run time from
  the bundled LGPL FFmpeg). Final choice and licensing: `docs/08_…` §7. **AAC and MP4 export checks passed on
  Windows and macOS** (18–19 Nov).
- Dorico, Sibelius and Finale are studied only through their documentation and behaviour, never their code.

## 9. Performance goals and benchmarks
Measured with the same tool as `Jubelle - Engine Baselines (15 Nov 2026)` (39-staff, 285-bar orchestra, engine
only):
| Measure | Today | Goal |
|---|---:|---:|
| Enter one note | 121 ms | ≤ 16 ms |
| Undo | 137 ms | ≤ 16 ms |
| Full layout on open | 1,429 ms | ≤ 700 ms (visible pages ≤ 150 ms) |
| Save | 1,320 ms | ≤ 300 ms, off the main thread |
| Playback update after an edit | part of a 376 ms rebuild | ≤ 10 ms |
| Engine memory | 115 MB | ≤ 70 MB |
| Small scores | n/a | never slower than MuseScore |

## 10. Compatibility requirements
- Read every format Jubelle reads today; write MuseScore 4.x files that MuseScore Studio 4.7.7 opens with the same
  music.
- Parts stay as MuseScore `Excerpts/` in files and become layouts in memory.
- Jubelle-only data, **tested against official MuseScore 4.7.7** (`docs/13_mscz_storage_results.md`): every variant
  opens with identical music, but when MuseScore *saves* the file only **score meta tags** survive (even 120 kB of
  JSON, exactly). A `Jubelle/` ZIP folder, unknown style values, and unknown element tags are dropped. The main
  score's element IDs (`<eid>`) are kept. So the engine stores its data in versioned meta-tag JSON, anchored to the
  main score's IDs and time positions, and mirrors Jubelle style values there. The ZIP folder is for rebuildable
  caches only.
- **Found for the current release:** a Jubelle score saved by MuseScore loses Jubelle's style settings (courtesy
  rules, magnetic layout, engraving rules). **Approved (D-E19):** all 11 are stored in score metadata and restored
  on reopening, with save → MuseScore save → reopen tests; part of the native file handling (S2).
- Templates, plugins, palettes, workspaces and shortcuts keep working.
- Opening and saving without edits leaves the music identical (normalised comparison on the whole corpus).

## 11. Condensing (summary; full specification in `docs/04_condensing_specification.md`)
- **A presentation stage** of `je::present`: players' music is never changed. Condensing produces
  *presentation staves* and *presentation events* that **reference** source notes, so two players can share a stem.
- **Groups are the user's choice** (decision D-E12): any 2–4 compatible players, with automatic staff sharing or the
  user's own staff sets (no hardcoded pairings such as 1+3 / 2+4). Sensible defaults per section come from a data
  table, and every one can be changed per layout.
- **Phrases** (split at shared rests, rehearsal marks, structural changes, manual changes and system starts) are
  classified as tacet, solo (1./2.), unison (a2/a3), chords, two voices, or divisi. The classifier is ported from
  Jubelle's tested condensing planner, with smoothing against label noise.
- **Accidentals** are recomputed per presentation staff (two players share one bar's accidental state); written
  transposition must match.
- **Layout:** two passes (musical phrases, then system starts with a fixed staff set per system), at most 3 passes,
  then freeze. Ties, slurs and hairpins continue correctly across condensed and divisi systems.
- **Editing:** clicks select source notes; edits apply to the source; expand, condense and reset are stored
  condensing changes, anchored by time and IDs (never bar numbers); turning condensing off loses nothing.
- **Parts** are never condensed (combined parts such as "Trumpets 1–2" are simply layouts with condensing on).
  Playback and MusicXML use players' music. MuseScore opens the file with every player on their own staff.
- **Labels** are abbreviated ("Tpt. 1.2", D-E18); full names stay available where the layout uses long names.
- **Approved 18 Nov 2026**; subsystem S9.

## 12. Development phases and dependencies (detail in `docs/06_roadmap_and_testing.md`)
| Phase | Content | Depends on | Status |
|---|---|---|---|
| 1 | Architecture investigation and technical audit | n/a | **done** (18 Nov) |
| 2 | Architecture and design: engine (2.1), condensing spec (2.2), model mapping (2.3), edit vocabulary (2.4), pipeline interfaces (2.5), bridge plan (2.6) | 1 | **done** (18 Nov) |
| 3 | Foundations = S1 and S2's start: 3.1 standalone build, tests, benchmarks, CI on Windows/macOS/Linux with sanitizers (**done**); 3.2 exec and model core (**S1 first increments done**: persistent model, snapshots, structural edits, invariants, bulk builder); 3.3–3.4 file reading and round trip (S2); 3.5 corpus (public set and private-score protection in place) | 2 | **in progress** |
| 4 | Components in shadow mode: 4.1 edit/undo → 4.2 playback events → 4.3 shaping → 4.4 spacing and breaking → 4.5 vertical and pages → 4.6 display list → 4.7 native I/O → 4.8 bridge and experimental switch | 3 | not started |
| 5 | Jubelle features on the new engine | 4 (per feature) | not started |
| 6 | Automatic condensing in the new engine (subsystem S9) | S1 for its logic; S7 for drawing | **approved**; logic follows S1 |
| 7 | Compatibility, performance, reliability, cross-platform; default-engine decision | 5, 6 | not started |

**Order of work (revised 18 Nov 2026, D-E22):** the phases above are now milestones within the ten subsystems of
`docs/14_full_engine_roadmap.md`: S1 model → S2 MuseScore 4.x reader and writer, with the style-setting preservation
(D-E19) → S3 editing and S4 undo → S7 layout and S8 drawing (the largest part, stage by stage) → S9 condensing (logic
as soon as S1 is ready; drawing after S7); S5 playback and S6 audio alongside S7 → S10 integration and removal of
MuseScore's engine from the app → full testing on Windows and macOS → the first testing release of the new engine.

## 13. Testing requirements
- Every component is compared against MuseScore's engine in **shadow mode**, with documented tolerances, before it
  is used for real.
- Golden corpus: demos, Jubelle templates, test scores, the large orchestra, big band. The **user's own scores** are
  used **locally only** (decision D-E17): kept outside the repository (`~/JubellePrivateScores` or
  `$JUBELLE_PRIVATE_CORPUS`), with `jubelle_engine/corpus/private/` git-ignored as a backstop. A guard check in CI
  fails if one is ever tracked. Tests skip when the scores are absent, and reports name them only "private #n".
  They are never committed, uploaded, published or built into anything.
- Unit, property (undo inverse, incremental equals full, determinism) and benchmark tests, without Qt.
- All existing app checks run on the new engine before any testing release; until then development builds keep
  MuseScore's engine only as a comparison baseline (D-E9).
- No test is ever disabled, removed or weakened to pass a phase.

## 14. Completed work
| Date | Work |
|---|---|
| 18 Nov 2026 | `engine/next` branch and `jubelle_engine/` structure; stable checkpoint branch; worktree workflow |
| 18 Nov 2026 | Phase 1 audit: MuseScore architecture, coupling (317 direct DOM users), performance causes, keep/adapt/redesign verdicts |
| 18 Nov 2026 | Jubelle integration map (every feature's hooks, stored data, route and phase) |
| 18 Nov 2026 | Engine architecture v1, condensing specification v1, licensing review, roadmap and testing plan, decisions D-E1 to D-E15 |
| 18 Nov 2026 | User's answers recorded (D-E12 configurable grouping, D-E16 hidden developer switch, D-E17 private scores) |
| 18 Nov 2026 | AAC encoder licensing investigation (`docs/08_…`) |
| 18 Nov 2026 | `.mscz` storage test against official MuseScore 4.7.7 (`docs/13_…`); D-E10 revised |
| 18 Nov 2026 | Phase 2.3–2.6 design: model mapping, edit vocabulary, pipeline interfaces and cache keys, bridge migration plan |
| 18 Nov 2026 | Phase 3.1 done and 3.2 started: standalone engine build; first core code with 24 tests; CI on Windows, macOS and Linux green, ThreadSanitizer and AddressSanitizer/UBSan clean |
| 18 Nov 2026 | User's decisions: AAC replacement, style preservation, abbreviated labels, condensing approved |
| 18 Nov 2026 | Roadmap revised for a complete, independent engine (D-E22): ten subsystems with independent tests; no testing release until the gates pass |
| 18 Nov 2026 | AAC: FFmpeg's own AAC encoder replaces FDK AAC, which is deleted from the source (`docs/08_…` §7); verified locally with FFmpeg 6.1 under sanitizers; **AAC and MP4 export checks passed on Windows and macOS** |
| 18 Nov 2026 | **S1 first increment:** persistent id map, immutable objects, field vocabulary, model with O(1) snapshots, structural edits, invariant checker, score builder; 35 tests (property tests for the map and for random edit/undo/redo sequences; snapshots read on other threads) clean under AddressSanitizer/UBSan and ThreadSanitizer. Orchestra size (39 × 285, 89,324 objects): one note edit or undo ≈ 1 µs, chord insert or delete ≈ 35 µs, snapshot O(1), model heap 41.6 MB |
| 18 Nov 2026 | S1: `BulkBuilder` (whole score at once, for file reading; same model as the transaction path, tested) and one-pass `IdMap` construction; orchestra built in 148 ms; 39 tests |
| 19 Nov 2026 | S1: memory per object reduced (24-byte values with shared text; kind and parent stored directly): orchestra model **41.6 → 23.9 MB**, bulk build 103 ms. Notation coverage: written values, dots and nested tuplets with exact duration arithmetic, grace notes (before and after), voices, full-measure rests, ties, articulations, lyrics (verses, syllables), chord symbols, dynamics, tempo, keys, clefs, instrument changes, rehearsal marks, jumps, repeats with play counts, voltas, slurs, hairpins, part layouts; consistency rules for each, with tests that break each rule. 42 tests |
| 19 Nov 2026 | AAC: **Windows and macOS export checks passed** (AAC-LC at 44.1/48/96 kHz; MP4 sound) |
| 19 Nov 2026 | **S2 started** (`docs/15_…`): ZIP with zlib, lossless XML (byte-for-byte for MuseScore's layout), score file, engine store with the 11-setting style mirror and restore, MuseScore 4.x reader into the model. Repository corpus (2,909 scores): **0 XML content changes**, 627/627 archives, **2,118/2,118 4.x scores read**, 2,104 consistent (14 known test-file issues); ASan clean. Large orchestra: open in 235–360 ms against 918 ms with MuseScore's engine (same machine and file). 47 tests. CI: corpus check on Windows/macOS/Linux; MuseScore 4.7.7 both-ways test |
| 19 Nov 2026 | **S2 compatibility with official MuseScore 4.7.7** (Windows CI): 54 Jubelle-written scores opened by MuseScore with **identical MIDI**; for all 52 4.x originals the engine read the same music back from MuseScore's save; Jubelle's 11 style settings **restored 11 of 11** after a MuseScore save (fixed: the style lives in `score_style.mss`). Corpus check green on Windows, macOS and Linux |
| 19 Nov 2026 | **S2 against MuseScore's own reading** (the comparison baseline, every 4.x score in the repository): **2,079 of 2,118 identical music listings (98.2%)**, up from 1,983 after fixes found by the comparison (multi-measure rest measures, after-grace order, end-of-measure time signatures, measure rests, tremolos in tuplets); the 39 left are documented kinds of importer and edge-case test files |
| 19 Nov 2026 | **S2 against MuseScore's own reading: 2,109 of 2,118 (99.6%)** after reader fixes (voice-1 completion, local time signatures, full tuplets, repeated duration types); the 9 left are listed with causes and gated in CI |
| 19 Nov 2026 | **S2 model-based writer (D-E27, D-E28):** files generated from the model alone, parts generated and linked; **2,118 of 2,118 corpus scores regenerate with identical music and model**; MuseScore 4.7.7's engine reads **2,110 of 2,117** generated files exactly as the originals (7 expected), with no warnings beyond the originals'. Fixed a reader data loss (glissandos and bends inside notes). Save-and-reopen tests (ensemble built in the model, edits after reopening, the 11 style settings); 63 tests clean under ASan/UBSan |
| 19 Nov 2026 | **S2 playback fidelity:** MuseScore's detailed reading and its own MIDI rendering of the generated files added to the comparison; fixed slurs inside chords, cross-voice/staff and grace ties, grace order, unison ties, linked staves within a score, grace lengths. MuseScore reads 2,107 of 2,117 generated files identically down to MIDI (3 importer files listed as not yet understood). Official MuseScore 4.7.7 MIDI check: 46 → 50 of 54, fixes for the rest since |
| 19 Nov 2026 | **S3 started:** editing operations with MuseScore's note-entry rules and the note-input state machine; property tests (random edits stay consistent, undo/redo exactly, save and reopen); 77 tests pass under ASan/UBSan; CI green on Windows, macOS and Linux for the S2 writer |
| 19 Nov 2026 | **S3:** range copy, paste and delete (`edit/clipboard`) and transposition; 82 tests |
| 25 Nov 2026 | **S3 linked staves while editing** (`edit/links`) and **tablature strings and frets** (`edit/tablature`, MuseScore's fretting rules ported): an edit on either staff of a linked pair (a guitar's standard and tablature staves) is made on the other in the same undo step; copies are linked as MuseScore links them (by identity from 4.60, by place with MuseScore's index rules before). Fixed: ties from another voice into a deleted note; staves linked both ways in a file. 91 tests pass under ASan/UBSan; corpus unchanged (2,118/2,118 regenerate; MuseScore reads 2,110/2,117 generated files as the originals); new check, also in CI: all **138 corpus scores with a linked staff, edited on both staves, are read by MuseScore with the same music on the linked staves and the links of the original** (formats 4.00–4.70) |
| 25 Nov 2026 | **S3:** lines and staff items (dynamics, staff texts, slurs, hairpins…) mirrored on linked staves; **links by place (before 4.60) resolved on reading and worked out again on saving, for parts too**: MuseScore reports fewer failed links on Jubelle's regenerated corpus than on the original files (49 → 38, none new); ties keep their links. 92 tests |
| 25 Nov 2026 | **Glissandi, bends and note lines modelled** as lines between two notes (830 of the corpus's 833 pair; the 3 without an end are kept as written): an edit to either note keeps the line whole or removes it whole; copy and paste carries them. 94 tests; corpus and MuseScore checks unchanged or better |
| 25 Nov 2026 | **S2:** parts written inside a plain .mscx are read as part layouts (they were carried as written, so edits didn't reach them), and a plain .mscx is saved as XML with its parts inside, as MuseScore does (it was written as an archive); a part's instruments are found by its staves' links, not only by name; a part staff linked to a linked staff finds its originals as MuseScore does. 95 tests |
| 25 Nov 2026 | **Benchmarks against MuseScore's engine** (`docs/16_performance.md`, same scores and machine): large orchestra open **982 → ~300 ms**, save **1,039 → ~450 ms**, model memory **115 → 32 MB**, undo **109 → 0.03 ms** (MuseScore's includes layout; Jubelle's layout comes with S7); saving was quadratic in score size (104 s on the large orchestra) until fixed today |
| 25 Nov 2026 | **S3 editing commands** (`edit/commands`, MuseScore's rules): enharmonic spelling (concert and written), accidentals, add interval (key, octave rule, along ties), articulations, change and swap voices (grace notes move with their chord), grace notes, lines, slurs, glissandos, key signatures, clefs. Found by the new check and fixed: **note entry on a staff with its own time signature** (a 3/4 staff in a 4/4 score) now uses that staff's values, and such a time signature is saved with its stretch. 108 tests; corpus checks unchanged; new check, also in CI: **every corpus score edited with the commands is read by MuseScore with exactly the edited music** (1,486 of 1,489; the other 3 already differ unedited); edit time on the large orchestra unchanged |
| 25 Nov 2026 | **S3 time signature changes** (`Commands::changeTimeSignature`, ported from MuseScore's `cmdAddTimeSig`, `rewriteMeasures` and `ScoreRange`): the music from the changed bar to the next time signature or section break is rewritten into the new measures, as MuseScore does it (notes split and tied at the new barlines, articulations on the first part, rests joined where MuseScore joins them and split undotted, measure rests, holes in voices 2–4 kept, the last measure filled, tuplets split at their middle, pickups, the same length only renamed, pieces tied by a rewrite joined again by the next); repeats, line, page and section breaks, voltas, rehearsal marks and part layouts' breaks kept. **Compared with MuseScore's own command on every corpus score** (`je_measures_reference.py`, in CI): 5/8 from bar 2, **2,088 of 2,111 the same music**; 3/4 from bar 1, **2,077 of 2,112**; MuseScore reads every saved result with the same music (2,111 of 2,111 outside the known list, CI). Found and fixed on the way: notes with a tie of their own (laissez vibrer, partial ties) given a second tie made MuseScore crash reading the file; measure repeats rewritten as rests. Large orchestra: **6.4 s in MuseScore's engine, 0.53 s in the Jubelle engine**. 114 tests |
| 25 Nov 2026 | **S3 split and join measures** (`Commands::splitMeasure` / `joinMeasures`, MuseScore's SplitJoinMeasure, on the same rewrite as time signatures, now with measures of any lengths): split where a chord, rest or chord symbol starts (not on the first beat, inside a tuplet, on a measure repeat or finer than 1/128); join with the first measure's time signature, the time signatures inside removed and each staff's last moved to the next measure. Against MuseScore's own commands on every corpus score (`je_measures_reference.py`, CI): **split 2,105 of 2,115, join 2,093 of 2,110 the same music** (the rest: staves with their own time signature, left unchanged for now, and a MuseScore oddity after joining across an end-of-measure time signature); MuseScore reads every saved result with the same music. 117 tests |
| 25 Nov 2026 | **S3 measure rewriting, the remaining differences** (on the same rewrite): staves with their own time signature follow MuseScore's rules (measure rests take the global signature; music refuses the change); the staves' old time signatures are given back per staff after the range; a staff's own signature replaced by a global one loses its stretch; tuplets inside tuplets are split with their parents. Time signature against MuseScore's command: **5/8 from bar 2, 2,094 of 2,111; 3/4 from bar 1, 2,089 of 2,112**; split and join unchanged; every saved result read by MuseScore with the same music. The intentional differences are listed in §15. 120 tests |
| 26 Nov 2026 | **S3 explode and implode** (`Commands::explode` / `implode`, MuseScore's `cmdExplode` / `cmdImplode` with its CloneVoice): explode gives each staff below one note of every chord (from the top, reused from the bottom), or, with voices 2–4 in use, moves each voice to the first free voice below from the measure its music starts in; implode on one staff joins voices 2–4 into voice 1 where the chords have the same length, across staves puts the first four voices with music into the first staff. Both refuse staves with different local time signatures, as MuseScore does, and staves linked to each other. Against MuseScore's own commands on every corpus score (`je_measures_reference.py`, CI): **explode 2,096 of 2,118, implode on one staff 2,109 of 2,118, implode across staves 2,014 of 2,118 the same music**; the rest are the intentional differences of §15 (most are guitar scores whose standard staff and tablature are linked) and files MuseScore already reads differently; MuseScore reads every saved result with the same music. 126 tests |
| 26 Nov 2026 | **S3 slashes** (`Commands::slashFill` / `slashRhythm`, MuseScore's `cmdSlashFill` / `cmdSlashRhythm` and `Chord::setSlash`): fill gives every beat (a dotted beat in compound time) a stemless, unplayed slash on the middle line, in each measure's first voice free there (pitch from the clef, key, earlier accidentals in the measure, the instrument's transposition, drum sets and tablature strings as MuseScore finds them); rhythmic slashes toggle the slash look and accent rests in voices 3–4. The look is kept in the elements' details where MuseScore writes it (`setDetailValues`; the details fingerprint is now in the model, `model/detailskey`); linked staves get the slashes too. Found on the way: a chord with an after-grace split by a measure rewrite left the after-grace before its last piece (now after it; CI's time signature check caught it once the files' reading problems were fixed); notes after an **instrument change** took the first instrument's transposition (`Operations::transpositionAt`, now used by note entry, paste, spelling and slashes). Against MuseScore's own commands on every corpus score (`je_measures_reference.py`, CI, slashes marked in both listings): **fill 2,088 of 2,118 (bars 1–2, staff 1) and 2,061 of 2,118 (bars 2–3, staves 1–2), rhythm 2,107 of 2,118 the same music**; MuseScore reads every saved result with the same music and the same slashes. 132 tests |
| 26 Nov 2026 | **S2/S3 fixes found while porting the commands:** a chord's kept appearance (stem direction, stemless, small…) was dropped when a measure was inserted before it, because its time was part of its details' fingerprint (no longer: `model/detailskey`); chord symbols are modelled with their bass (`BASS`) and, from 4.60, read from and written to MuseScore's `<harmonyInfo>` — before, a 4.60 file's chord symbols weren't transposed with the music, and transposing an older file's dropped their slash bass and look. MuseScore's reading of every regenerated file now also compares each chord symbol's root, bass and name (2,107 of 2,117 as the originals, the 10 expected). 134 tests |
| 26 Nov 2026 | **S3 realize chord symbols** (`Commands::realizeChordSymbols`, MuseScore's `cmdRealizeChordSymbols`) on a new **chord-symbol module** (`edit/chordsymbol`, ported from MuseScore's `ParsedChord::parse` and `RealizedHarmony`: quality, extension and modifiers; the ranked intervals; the jazz additions before the next chord; the seven voicings), which S5's playback of chord symbols will use too. Each symbol (fret diagrams' included) is written on its staff and voice for its duration — until the next symbol, to the barline or for its segment — in MuseScore's values, tied across barlines, with each symbol's own settings or the dialog's. **Every corpus chord symbol compared with MuseScore's understanding and its seven voicings, literal and jazz (`je_io_tool chordlist`, `JUBELLE_REFERENCE_HARMONY`): 1,166 of 1,177 identical** (the rest: a 1.x score's chord list ids, a "♭" written in a modifier, which both realize as the root only). Against MuseScore's own command (`je_measures_reference.py`, also run in CI): **with the symbols' own settings 2,106 of 2,118 the same music, with the dialog's (drop 2, to the barline, jazz) 2,105** — the rest are stacked fret diagrams at one time, read differently, and tuplets (below). 136 tests |
| 26 Nov 2026 | **S3 move to staff above / below** (`Commands::moveToStaff`, MuseScore's `moveUp` / `moveDown`, cross-staff notation): a chord or rest shown on the next staff of its instrument, one staff at most either way, between standard staves; the cross-staff move is now modelled (`STAFF_MOVE`, read and written as MuseScore's `<staffMove>`, so it no longer disappears when the chord's music is edited, and MuseScore's reading of every regenerated file compares it too). Against MuseScore's own command (`je_measures_reference.py`, also run in CI): **down from the first staff 2,103 of 2,118, up from the second 2,103 the same** — the rest are files whose parts show only one staff of an instrument (MuseScore refuses a move between a staff in a part and one that isn't; the model doesn't record parts per staff yet) and files MuseScore already reads differently. 137 tests |
| 26 Nov 2026 | **The play order** (`perform/repeatlist`, ported from MuseScore's RepeatList, with Jubelle's "Play this repeat"): repeats and their play counts, first and second endings (overlapping ones merged as MuseScore merges them), D.C. / D.S. with Fine and Coda, section breaks. **The same as MuseScore's on its 72 repeat test files and on all 2,118 corpus scores** (`je_repeats_reference.py`, in CI). Lyric entry uses it now, playback (S5) will. 143 tests |
| 26 Nov 2026 | **S3 lyric entry** (`edit/lyrics`, ported from MuseScore's lyric entry: next syllable, extension line, next and previous word, new verse, verse up and down, and Ctrl+L; with TextBase::endEdit and the lyric checks of its layout): dashes and extension lines across voices, rests and repeats (partial dashes and lines at a repeat or between endings), a lyric left empty removed and one just made for typing taken back with the step that made it. Against MuseScore's own lyric entry, the same keys typed on every corpus score (`je_measures_reference.py … lyrics`, in CI): **2,109, 2,105 and 2,109 of 2,118 the same lyrics and music** (three key sequences; the rest are the 9 files MuseScore already reads differently, and 4 test files of §15's difference 13), and **864 of 864** on MuseScore's repeat test files (`je_repeats_reference.py`) |
| 26 Nov 2026 | **S3 text** (`Commands::addText`, MuseScore's `Score::addText` and the text typed): staff and system texts, expressions, dynamics (a typed name becomes the dynamic), rehearsal marks (the next in the score's sequence), tempo texts (the beat at 80), fingerings. Against MuseScore's own command on every corpus score (in CI): **2,109 of 2,118 the same** for each kind (tempo 2,106: §15's difference 14). Found and fixed on the way: **formatted text was lost when a score was saved from the model** (a lyric or text whose whole content is markup, such as `<b>bold</b>` or a symbol); MuseScore now reads all 2,118 regenerated files with the same lyrics and texts (compared in CI with the detailed reading). And the 25 Nov linked-staff change copied dynamics, staff texts and chord symbols to the other staff of a part, which MuseScore doesn't do; now they stay on their own staff, as lyrics and (on a tablature that hides them) fingerings do, and only copies the file already linked follow. 153 tests |
| 26 Nov 2026 | **S4 undo history, autosave journal and crash recovery** (`edit/history`, `edit/journal`, `edit/session`). The history knows where the score was saved (MuseScore's clean state: undoing back to it makes the score unmodified again; lost when that point is dropped with a redo branch or the history limit) and names the Undo and Redo steps. The **journal** records every committed edit, undo and redo as it happens (each record with its length and a CRC-32, handed to the system at once and put on the disk in the background at most once a second, so editing never waits for the disk); it starts with a copy of the whole model and its undo history, written again on every save. After a crash, `EditSession::recover` rebuilds the score from the journal alone: **exactly the score at the last whole record, with the same ids and the whole undo history, back past the last save** — also if the file was changed or deleted since (the user is told). Opening a score never overwrites a journal a crash left for it. Saving now puts the new file on the disk before it replaces the old one (Windows: an atomic replace, where the old code removed the old file first). **Checked on every corpus score** (`je_io_tool journal`, in CI on all three platforms): edited at random and saved halfway, left without closing, recovered — **2,117 of 2,117 exactly the same model, ids, history and saved file** (the 2,118th has no music to edit). Unit tests cut the journal at every record and at random bytes inside records (the last whole record recovered every time), damage a byte (recovery stops there), and run **10,000 random steps** checked after each one (undo gives exactly the state before, redo the state after) before recovering them. Costs: a journal record about 0.004 ms; starting the journal 0.7 ms for a typical score, 50 ms for Big Score, about 100 ms for the large orchestra (on opening and saving, which take 180 ms and 450 ms there); recovery 2 ms typical, 190 ms for the large orchestra. `je_io_tool recover <journal> <out>` saves a journal's score by hand. 162 tests |
| 26 Nov 2026 | **S7 started: style values and music fonts** (`je_layout`: `style/style`, `style/font`). The style is MuseScore's table of 1,983 values by MuseScore's names (`Sid`, generated with their defaults from MuseScore's definitions by `je_gen_style.py`, so ported layout code reads the same values), read as MuseScore reads a score's style: the names older versions used, the values each format version changes after reading (4.50's narrower spacing, the text styles' music symbol sizes before 4.70, positions from alignments before 4.60, header and footer styles for copyright and page numbers before 4.40, a chord description file of the score's own), and parts starting from the defaults the score was written with. **Every corpus score's style is MuseScore's: 2,118 of 2,118, every value** (`je_style_reference.py`, in CI; MuseScore's conversion of two pre-4.6 chord-symbol values needs text font metrics the reference tool lacks). Music fonts: all 11 of MuseScore's, every symbol (`SymId`, 3,058) with its metrics from the font's outlines through FreeType (MuseScore's bundled 2.14.1; the exact outline box and design advance, as MuseScore measures them), SMuFL anchors, the alternates MuseScore uses (Bravura's oversized noteheads, straight flags…), composed ornaments and Bravura as the fallback; Leland's boxes match its own metadata within 0.005 sp. Found on the way and fixed (S2): an .mscx inside a folder with META-INF/container.xml is a container to MuseScore, whose score_style.mss holds the style (113 corpus scores, the templates among them, were read without it; saved there now too); a score_style.mss in an older format than its score is read by its own version; an archive with a style in both places has them merged as MuseScore reads them. The reference tool now measures music glyphs from the outlines too (`JUBELLE_REALISTIC_METRICS=2`) and lists every chord's layout (`Jubelle_Reference.ChordLayout`), for the chord shaping that comes next. 166 tests |
| 26 Nov 2026 | **S7 shaping, first stage: what each note shows** (`layout/context`, `layout/notes`): each staff's clefs, key signatures and transpositions as the score shows them, and for every note its staff line, notehead and accidental, ported from MuseScore's `cmdUpdateNotes`, `Note::updateAccidental` (the measure's accidental state, ties, unisons, microtonal accidentals, the user's accidentals kept) and `Note::noteHead`, with **Jubelle's automatic courtesy accidentals** (every rule: bars back, the instrument's other staves, all octaves) and cross-measure values. Tables generated from MuseScore's source (noteheads, accidentals, clefs). **Against MuseScore's own layout of every corpus score (`je_layout_reference.py`, in CI): 2,102 of 2,118 the same, every line, notehead and accidental**; the other 16 are 10 files MuseScore already reads differently, ornaments whose note restates an accidental (7 test files; ornament intervals are ported with the ornaments) and one instrument change from a drum set to a pitched instrument. Found on the way: MuseScore 4.60 and later read a key signature's old `<accidental>` as C, and take the key shown from `<actualKey>`, else the concert key. 169 tests |
| 26 Nov 2026 | **S7 beam grouping** (`layout/beams`, ported from MuseScore's `BeamLayout::createBeams`, `beamGraceNotes` and `Groups`): which chords and rests are beamed together, by the time signature's beam groups (MuseScore's defaults or the score's own), the beam modes set on chords and rests, the beat subdivision of x/4 meters, tuplets, grace notes, cross-measure values and beams continued from the previous measure; stemless measures show no beams. **Against MuseScore's own layout of every corpus score: 2,101 of 2,118 the same, every beam as well as every line, notehead and accidental**; the 17 left are the 16 above and one beam across a system break, which MuseScore splits (systems come later in S7). Found on the way: MuseScore lays a measure out again after the next one, which takes an unbeamed rest out of a beam continued into the next measure (vtest beams-2 and beams-19); the engine gets that result directly. 172 tests |
| 27 Nov 2026 | **S7 stem directions** (`layout/stems`, ported from MuseScore's `ChordLayout::computeUp`, the beam direction of `BeamLayout::layout1`, `Measure::hasVoices` and `Chord::noteDistances`): a chord's own direction, its beam's (set on the beam, by moved chords, by the voices, or from the beam's distinct note lines), voices 1 and 3 up and 2 and 4 down where other voices show something, grace notes up, chords moved to another staff, otherwise the notes' balance around the middle line, with **staff type changes** (a 14-line staff from bar 11: its own middle line). **Against MuseScore's layout: 55,980 stem directions compared, every one the same** (2,101 of 2,118 scores, the same 17 known as above). Not yet known, and so not compared: cross-staff beams, beams placed by hand and two-note tremolos (974 chords), which need positions. 175 tests |
| 27 Nov 2026 | **S7 mirrored noteheads** (`layout/chords`, ported from MuseScore's `layoutChords1` and `layoutChords2`): which noteheads go on the other side of the stem, for seconds within a chord and between voices whose stems go the same way (with the combine-voice setting), grace chords on their own, and the user's choice per note. **Against MuseScore's layout: every mirrored notehead the same** on the 2,101 scores (where a chord at the same time has a direction not known yet, its neighbours' are not compared either). 176 tests |
| 27 Nov 2026 | **S7 note positions within a chord** (`layoutNotePositions`, MuseScore's `layoutChords3`, `StemLayout::stemPosX`, `Note::headBodyWidth`, `Chord::mag`): each note's x by its side of the stem (the font's stem anchors and notehead widths, the stem's width over a mirrored head) and y by its line, staff type (line distance, step offset) and the user's offset, for small staves, small and grace notes. **Against MuseScore's layout: 127,585 note positions compared, every one within 0.002 spaces.** Not compared yet: chords whose noteheads aren't known yet (shape and pitch-name schemes), and chord offsets between voices (next). 177 tests |
| 27 Nov 2026 | **S7 stems and flags** (`layoutStemLengths`, MuseScore's `StemLayout::calcDefaultStemLength`, `maxReduction`, `minStaffOverlap`, `Hook::symIdForHookIndex`): which chords have a stem and a flag (traditional or straight), and the length of every stem no beam decides: shortened outside the staff, lengthened to the middle line, for small and grace notes, other line distances and the user's length. **Against MuseScore's layout: 41,748 stem lengths compared, all within 0.002 spaces; every flag the same.** Not yet: beamed stems (the beam's position decides them) and one-chord tremolos (their minimum length). 178 tests |

## 15. Outstanding work
- **S2** follow-ups: part-only content (texts and positions that exist only in a part); the 9 reference differences
  and 7 consistency known issues; older formats (1.x–3.x), MusicXML and MIDI; the kept XML's memory.
- **S3 editing** (now): done: operations, note input, range copy/paste/delete, transposition, linked staves and
  tablature fretting (parts are layouts of the same music, so they follow every edit by design), lines and staff
  texts on linked staves, the editing commands of `edit/commands`, time signature changes, split and join measures,
  explode and implode, slashes, chord-symbol realization, moving to another staff, lyric entry, text; each compared
  with MuseScore's own command on the corpus. The clipboard's text format and text formatting while typing belong
  to S10.
- **S7 layout** (now): style values, music fonts and what each note shows (lines, noteheads, accidentals) done
  (§14), beam grouping, stem directions, mirrored noteheads and note positions in a chord. Next, against MuseScore's
  positions (the reference tool's chord layout listing): voice offsets, accidental and dot placement, beams' positions (and their stems). Not yet in
  the notes stage: drum staves (the drum set's lines and noteheads), tablature (fretting is in `edit/tablature`),
  shape-note and pitch-name notehead schemes, ornaments' notes (an ornament's accidental restating its line), an
  instrument change between a drum set and a pitched instrument.
  Not yet: text fonts' metrics (FreeType for the advances; MuseScore shapes text with HarfBuzz, whose kerning will
  be compared when text layout is ported).
- **S4 undo, autosave and recovery** (done 26 Nov 2026, see §14). Left for S10, where the interface uses them: the
  recovery question when Jubelle starts (`findJournals`: which file, whether it changed since, how much work), the
  journal folder (Jubelle's own data folder), and a journal for a new score not yet saved (it has no file to name:
  the journal can already hold it, the session needs a path to save to). Possible later: writing the journal's
  starting copy in the background (it is the 50–100 ms added to opening and saving the largest scores). Paths with
  characters outside the system code page on Windows are not yet handled by the engine's file functions (the same
  for reading, saving and journals; to be fixed with S10's file dialogs).
- Text, not yet: typing chord symbols (MuseScore's chord-symbol entry and its navigation), frame texts (title,
  composer…), instrument change texts, sticking, harp pedal diagrams and figured bass; system texts, tempos and
  rehearsal marks are added on the top staff only, not also on the score's extra system-object staves; numbered
  rehearsal marks follow measure numbers counting irregular measures out only.
- Lyrics, not yet: copying lyrics to the clipboard (S10); a lyric left empty is taken away in the next step's undo
  record rather than in the one that made it (the music ends the same as MuseScore's).
- Known limits of the commands, to fix when next touched: add interval on drum staves (needs the drum set); a unison
  (C and B♯ in one chord) is not added, MuseScore allows it; changing the voice of notes inside tuplets.
- Slashes, not yet like MuseScore: slash fill on a staff with its own (local) time signature is skipped there; an
  instrument change to or from a drum set is not followed (the player's first instrument decides); un-slashing on a
  drum staff doesn't restore the drum set's noteheads and stem directions. Slash notes are marked not played in their
  details (`<play>0</play>`): S5 playback must honour it. The slash look of an element read from a file is dropped
  with its other details when its music is edited later (D-E27), as for any appearance detail today.
- Realizing chord symbols, not yet like MuseScore: the notes stop at a tuplet in the way (MuseScore writes into it);
  a capo isn't added to the pitches; staves with their own time signature are left alone; "until the next chord
  symbol" doesn't follow repeats (MuseScore's playback order). Fret diagrams' chords are read from their details
  (to be modelled with S7's fret diagrams).
- Measure rewriting (time signatures, split, join), where the engine still differs from MuseScore's own commands:
  staves with their own (local) time signature that hold music are refused, by MuseScore's rule in
  `rewriteMeasures` (they follow where they hold only measure rests); the reference run nevertheless shows MuseScore
  changing 8 such test files, not yet understood. A few single files (a capo score, two slur and bend importer
  files, one dynamics score) are not yet looked at.
- **Intentional differences from MuseScore** (the user's rule, 25 Nov 2026: musical correctness before identical
  results; each is listed here and in `docs/10_edit_vocabulary.md`):
  1. A note split by a measure rewrite keeps its grace notes before the first piece and its after-graces after the
     last one (the tie into an after-grace runs on from the last piece); MuseScore copies the graces before it onto
     every tied piece, so they would sound and show again, and leaves the after-graces after the first piece, in
     the middle of the tied note (13 corpus files differ from MuseScore's time signature change for this).
  2. A tuplet inside a tuplet is split at a new barline only where the barline falls at its middle, as for any
     tuplet; MuseScore halves it wherever the barline falls, which leaves its notes the wrong length. Refused
     otherwise.
  3. Joining measures across an end-of-measure time signature: MuseScore's result gives the following measures a
     stretch of 4/3 that no staff asked for; the engine leaves them in their own time signature.
  4. Damaged files (events that overlap, a tuplet whose music doesn't fill it) are not rewritten; MuseScore
     rewrites them into music of a different length.
  5. Explode and implode between staves linked to each other (a guitar's standard staff and its tablature) are
     refused: the two show the same music, so MuseScore's result takes notes out of both or doubles them.
  6. Explode of chords with no staff below to take the other notes leaves the music as it is; MuseScore removes
     those notes from the score.
  7. Exploding a voice whose music starts after the first measure keeps the destination's music before it, also
     where MuseScore's copy of the source's earlier rests would overwrite notes there.
  8. Implode across staves replaces the first staff's voices over the whole range; MuseScore leaves a voice's old
     notes where its new source has only a measure rest (so a moved voice's notes sound twice) and keeps old rests
     under the new notes (overlapping events), and clones multimeasure rests into voice 2.
  9. Slash fill on a beat cut short by the barline (a pickup, an irregular measure) writes a shorter slash;
     MuseScore writes the whole beat tied into the next measure, which shifts that measure's slashes and can
     overwrite its music in that voice.
  10. Slash fill over a staff and its linked staff (standard and tablature) fills them once; MuseScore fills the
     pair twice, a second set in another voice, and runs out of voices sooner.
  11. Beats inside a tuplet get no slash (MuseScore has no segment there either); where it does write into one, the
     tuplet is left as it is.
  12. Enter (a new verse) on a lyric whose chord already has the next verse goes to that lyric; MuseScore adds a
     second lyric of the same verse to the chord.
  13. A hyphen that crosses a chord without a syllable ("high – ness" over three chords) is kept; MuseScore removes
     such dashes when it opens a score or its repeats change (`Score::undoRemoveStaleTieJumpPoints`), making one
     word two (4 vtest lyric files).
  14. A tempo text added where a staff has its own time signature takes its beat from the score's time signature;
     MuseScore takes the local signature's stretched value, a whole note in 4/4 against a 12/8 staff (3 vtest
     files).
  15. Autosave is a journal of every step instead of MuseScore's copy of the whole score every few minutes: after a
     crash the score comes back as it was at the last edit, not the last autosave, and with its undo history.
- Parts that show only some voices of their instruments (MuseScore's track list, `<Tracklist>`) are written with
  every voice: the voice choice is to be modelled on the part layout (S2 part-only content).
- **App fixes queued until the new engine is complete** (user's decision, 25 Nov 2026: no Windows test builds
  now; these go into the app once the new engine is in place, see §15a).
- **S2** open items: 3 playback differences in importer test files; part-only content.
- The user's private scores: `~/JubellePrivateScores` wasn't found on the connected computer; run
  `je_io_tool corpus <folder> --private` once it is available.
- **S1** follow-ups as S7 needs them (layout fields, typed views).
- Then S3–S10 as in `docs/14_full_engine_roadmap.md`. No user decisions are pending.

## 15a. App fixes reported by the user, scheduled after the new engine (25 Nov 2026)
The user asked (25 Nov, 12:12) not to make Windows builds for these now, and to put them in the plan for after the
new engine is created. Each is checked again against the new engine when it is connected (S5, S6, S8, S10), because
part of the cause may be in code that the new engine replaces.

| Id | Report | What is known | Plan |
|---|---|---|---|
| F1 | **MP3 exports sound worse than MuseScore 4's** (user, 24 Nov: "duller / muffled", "squashed / less dynamic"; MS Basic in both) | Measured on Windows CI against official MuseScore 4.7.7, 8 scores, 44.1 kHz / 128 kbps (runs 45545719551, 45664264474): the MP3 encoding is identical; on loud ensembles Jubelle's safety limiter turns tuttis down by up to 14 dB (loudness range: big band 12.7 → 9.6 LU, Brassed_Up 9.2 → 5.7 LU); MuseScore's louder files clip heavily (big band MP3: 318,568 clipped samples), and that loudness accounts for most of the "duller" impression; quiet scores have identical spectra; Jubelle's exports start 5 ms late (limiter look-ahead). User's choices: compensate the delay; no preference on the limiter approach | **Code written** on branch `investigate/mp3-export` (commit after `main` `baa6fdb5`): exports render once to a temporary file, measure the peak, turn the whole piece down so the limiter does at most 3 dB of work, and remove the 5 ms delay; quiet music is exported exactly as before; playback unchanged; all export formats. 3 new tests pass (standalone, ASan); unit-test CI passed (run 45665903913). **Not yet built or measured on Windows** (build 45665899214 cancelled at the user's request). After the engine: port to S6, rerun `jubelle_audio_export_compare.yml` and confirm the loudness range, no clipping, 0-sample offset and unchanged quiet scores |
| F2 | **Ctrl+S (Save) also opens the drums panel/menu**, every time | Checked: Ctrl+S is assigned only to Save (standard key) in all shortcut files and workflow profiles; the menu keyboard handling and the save code are unchanged from MuseScore or contain nothing that opens a drum window. The likely route is the percussion/drum panel auto-show, which reacts to notation and selection change notifications that saving may send; not reproduced yet | Reproduce on Windows with a test that presses Ctrl+S in the UI self-test and records which panels open; fix in S10 when the interface is connected to the new engine's notifications |
| F3 | **The blue playback cursor falls behind the music** | Not investigated yet. To measure: cursor time against the audio clock over a long score (drift, or a fixed lag such as output latency or the limiter's 5 ms) | Measure and fix with S5 playback (the new engine's timing drives the cursor) |

## 16. Important technical decisions
D-E1 same repository, `engine/next` · D-E2 stable checkpoint · D-E3 GPL v3 · D-E4 C++17 core without Qt ·
D-E5 player-centred model, parts as layouts · D-E6 transactions and change sets · D-E7 staged cached pipeline with
windowed breaking · D-E8 (superseded by D-E22) · D-E9 comparison against MuseScore's engine, which is removed at
the end ·
D-E10 MuseScore 4.x format; Jubelle data in versioned meta-tag JSON (tested) · D-E11 condensing only in the new
engine, after approval · D-E12 configurable condensing groups, per-section defaults as data · D-E13 changes anchored
by time and IDs · D-E14 performance goals · D-E15 FFmpeg's own AAC encoder replaces FDK · D-E16 hidden developer switch ·
D-E17 private scores never in Git · D-E18 abbreviated labels · D-E19 style settings in metadata · D-E20 no testing
release until the complete engine passes its gates · D-E21 (withdrawn) · D-E22 complete, independent engine ·
D-E23 the interface stays and is rewired to the engine · D-E24 model representation · D-E25 lossless XML kept
next to the model · D-E26 known-issues list for corpus checks · D-E27 files generated from the model, details by fingerprint ·
D-E28 part layouts generated and linked.
Full text and reasons: `jubelle_engine/docs/07_decisions_log.md`.

## 17. Risks
| Id | Risk | Mitigation |
|---|---|---|
| R-T1 | Size: the DOM and layout are ~200k lines with decades of edge cases | port algorithms rather than reinvent them; shadow comparison on a broad corpus; incremental phases |
| R-T2 | Coupling: 317 files use the DOM directly | the interface is moved to the engine API consumer by consumer (S10), each move tested |
| R-T3 | Layout differences visible to the user | "MuseScore-compatible" mode for the comparison phase; differences reviewed, never silent |
| R-T4 | File compatibility regressions | normalised round-trip tests on the corpus; MuseScore 4.7.7 opens every written file (coexistence test) |
| R-T5 | Threading bugs | snapshots are immutable; ThreadSanitizer in CI; deterministic-result tests |
| R-T6 | Long-running dual maintenance (two engines) | the stable release is frozen on MuseScore's engine; development builds only; MuseScore's engine removed at the end |
| R-T8 | Feature parity: dozens of Jubelle features and decades of MuseScore behaviour to reproduce | per-feature checklist from the integration map; comparison tests per subsystem; nothing is released until parity is checked |
| R-T9 | Audio engine replacement (real-time safety, plug-in hosting) | offline sample-exact render tests, glitch tests under load, VST3 hosting tested on both platforms before integration |
| R-T7 | Condensing readability | ported tested classifier, smoothing rules, user's visual review before release |
| R-L1 | FDK AAC licence (GPL incompatibility) | **closed** (18–19 Nov): FDK removed, FFmpeg's own LGPL encoder used; AAC and MP4 checks passed on Windows and macOS |
| R-C2 | MuseScore saving a Jubelle score drops Jubelle's style values | the engine store mirrors all 11 settings in meta tags (D-E19, S2), with the Jubelle → MuseScore → Jubelle test |
| R-P1 | The user's private scores leaking into Git or reports | kept outside the repository; git-ignore backstop; CI guard; anonymous names in reports |
| R-C1 | MuseScore upstream changes after 4.7.7 | not needed; the fork stays on 4.7.7. Upstream fixes are cherry-picked only when relevant |
