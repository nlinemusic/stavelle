# Jubelle Engine Development Master Plan

**The central reference for the next-generation Jubelle engine.** Keep it current: every milestone, decision or
change of direction is recorded here (and in detail in `jubelle_engine/docs/`) in the same commit as the work.

| | |
|---|---|
| Last updated | 18 Nov 2026 |
| Goal | **A complete, independent Jubelle engine** (D-E22): MuseScore's engine isn't responsible for editing, playback, audio, drawing, layout or files in the finished app |
| Current work | **S1 model and data management** (design done; foundations in place) → S2 file handling |
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
  the bundled LGPL FFmpeg). Final choice and licensing: `docs/08_…` §7. Windows and macOS checks are running.
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
| 18 Nov 2026 | AAC: FFmpeg's own AAC encoder replaces FDK AAC, which is deleted from the source (`docs/08_…` §7); verified locally with FFmpeg 6.1 under sanitizers; Windows and macOS export checks running |
| 18 Nov 2026 | **S1 first increment:** persistent id map, immutable objects, field vocabulary, model with O(1) snapshots, structural edits, invariant checker, score builder; 35 tests (property tests for the map and for random edit/undo/redo sequences; snapshots read on other threads) clean under AddressSanitizer/UBSan and ThreadSanitizer. Orchestra size (39 × 285, 89,324 objects): one note edit or undo ≈ 1 µs, chord insert or delete ≈ 35 µs, snapshot O(1), model heap 41.6 MB |
| 18 Nov 2026 | S1: `BulkBuilder` (whole score at once, for file reading; same model as the transaction path, tested) and one-pass `IdMap` construction; orchestra built in 148 ms; 39 tests |

## 15. Outstanding work
- **S1** (in progress; foundations done): typed views and remaining kinds' rules (tuplets, grace notes,
  spanners, flow items, layouts, tempo and meter maps); reduce memory per object (41.6 MB for the orchestra: smaller
  values and interned strings). The bulk builder for loading files is done: the orchestra builds in 148 ms
  (0.84–0.98 s through transactions), giving exactly the same model (tested).
- **S2** next: native MuseScore 4.x reader and writer; engine store; style-setting preservation with the
  Jubelle → MuseScore → Jubelle test; round trips on the corpus.
- AAC: done in code; confirm the Windows (run 44447159173) and macOS (run 44447162973) export checks.
- Then S3–S10 as in `docs/14_full_engine_roadmap.md`. No user decisions are pending.

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
D-E23 the interface stays and is rewired to the engine · D-E24 model representation.
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
| R-L1 | FDK AAC licence (GPL incompatibility) | **closed in code** (18 Nov): FDK removed, FFmpeg's own LGPL encoder used; closes when the Windows and macOS export checks pass |
| R-C2 | MuseScore saving a Jubelle score drops Jubelle's style values | the engine store mirrors all 11 settings in meta tags (D-E19, S2), with the Jubelle → MuseScore → Jubelle test |
| R-P1 | The user's private scores leaking into Git or reports | kept outside the repository; git-ignore backstop; CI guard; anonymous names in reports |
| R-C1 | MuseScore upstream changes after 4.7.7 | not needed; the fork stays on 4.7.7. Upstream fixes are cherry-picked only when relevant |
