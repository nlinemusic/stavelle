# Jubelle Engine Development Master Plan

**The central reference for the next-generation Jubelle engine.** Keep it current: every milestone, decision or
change of direction is recorded here (and in detail in `jubelle_engine/docs/`) in the same commit as the work.

| | |
|---|---|
| Last updated | 18 Nov 2026 |
| Current phase | **Building testing build E1: the new engine with condensing** (Phases 1–2 done; foundations in progress) |
| Next testing build | **E1**: new engine and live condensing (`docs/14_testing_build_e1_plan.md`); published only after the Windows and macOS gates pass |
| Branch | `engine/next` (development area: `jubelle_engine/`) |
| Stable release | **Jubelle v4.7.7.261118230**, https://github.com/nlinemusic/stavelle/releases/tag/v4.7.7.261118230 |
| Stable recovery points | tag `v4.7.7.261118230`; branch `checkpoint/stable-4.7.7.261118230` (commit `207614e5`) |
| Condensing | **approved 18 Nov 2026** (configurable groups, labels "Tpt. 1.2"); being implemented in the new engine for E1 |

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

## 2. Protecting the stable release
- `main` keeps producing releases exactly as today. Engine code reaches `main` only in milestones that are **inert
  behind an experimental switch, off by default**, after the full Windows and macOS app checks pass.
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
        │                12 bridge migration · 13 .mscz storage results · 14 testing build E1 plan
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
8. **Kept as they are:** the audio engine (thread, mixer, FluidSynth, VST3, encoders, Jubelle's limiter and
   dynamics), MPE, the draw backend (PDF, SVG, PNG), MuseScore's file readers for every version, the importers,
   the style and property vocabulary, instrument data and SMuFL fonts.
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
- **Integration:** shadow mode (both engines run; results compared automatically) → consumers moved to an
  engine-neutral API one at a time → opt-in per-score switch → default only after Phase 7.

## 6. MuseScore dependencies
| Kept (used by the new engine or alongside it) | Replaced by the new engine |
|---|---|
| framework/audio, mpe, midi, draw, ui, actions, shortcuts, extensions | engraving/dom (score model) |
| engraving/rw readers (all versions) and importexport, via the converter | engraving/editing (undo, commands) |
| style (`Sid`), properties (`Pid`), instruments data, fonts | engraving/rendering/score driver; algorithms ported |
| notation module, adapted into the bridge | engraving/playback event generation (same MPE output) |

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
  (c) AAC-LC US patents have expired. **Approved (18 Nov):** remove FDK; use FFmpeg's native AAC encoder if its
  licensing and Windows/macOS reliability are verified, otherwise the OS encoders; ships in E1.
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
  on reopening, with save → MuseScore save → reopen tests; ships in E1.
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
- **Approved 18 Nov 2026**, and part of testing build E1.

## 12. Development phases and dependencies (detail in `docs/06_roadmap_and_testing.md`)
| Phase | Content | Depends on | Status |
|---|---|---|---|
| 1 | Architecture investigation and technical audit | n/a | **done** (18 Nov) |
| 2 | Architecture and design: engine (2.1), condensing spec (2.2), model mapping (2.3), edit vocabulary (2.4), pipeline interfaces (2.5), bridge plan (2.6) | 1 | **done** (18 Nov) |
| 3 | Foundations: 3.1 standalone build, tests, benchmarks, CI on Windows/macOS/Linux with sanitizers (**done**); 3.2 exec and model core (**started**: thread pool, Fraction, IDs, copy-on-write vector, transactions, change sets, undo history); 3.3 DOM → model converter; 3.4 round trip; 3.5 corpus (public set and private-score protection in place) | 2 | **in progress** |
| 4 | Components in shadow mode: 4.1 edit/undo → 4.2 playback events → 4.3 shaping → 4.4 spacing and breaking → 4.5 vertical and pages → 4.6 display list → 4.7 native I/O → 4.8 bridge and experimental switch | 3 | not started |
| 5 | Jubelle features on the new engine | 4 (per feature) | not started |
| 6 | Automatic condensing in the new engine | approved; for E1 it uses MuseScore's layout as its drawing backend (D-E21) | **in progress (E1)** |
| 7 | Compatibility, performance, reliability, cross-platform; default-engine decision | 5, 6 | not started |

**Order of work, changed 18 Nov 2026 (D-E20):** the next testing build **E1** uses the new engine and includes
condensing. E1 work comes first (`docs/14_testing_build_e1_plan.md`): model core → DOM → model converter and live
sync → condensing in `je::present` → display score for MuseScore's layout → interface → engine store and style
preservation → AAC replacement → full Windows and macOS test pass → E1 pre-release. Then E2 (engine editing, undo,
playback events), then E3 onward (native layout stages in pipeline order, display list, native I/O), each compared in
shadow mode before use. E1's scope (D-E21): the new engine owns the model copy, presentation, condensing and its store;
MuseScore's editing, playback and layout remain the proven backend until replaced.

## 13. Testing requirements
- Every component is compared against MuseScore's engine in **shadow mode**, with documented tolerances, before it
  is used for real.
- Golden corpus: demos, Jubelle templates, test scores, the large orchestra, big band. The **user's own scores** are
  used **locally only** (decision D-E17): kept outside the repository (`~/JubellePrivateScores` or
  `$JUBELLE_PRIVATE_CORPUS`), with `jubelle_engine/corpus/private/` git-ignored as a backstop. A guard check in CI
  fails if one is ever tracked. Tests skip when the scores are absent, and reports name them only "private #n".
  They are never committed, uploaded, published or built into anything.
- Unit, property (undo inverse, incremental equals full, determinism) and benchmark tests, without Qt.
- All existing app checks run with the experimental switch off (the release) and on (the new engine).
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
| 18 Nov 2026 | User's decisions: AAC replacement, style preservation, abbreviated labels, condensing approved; priority changed to testing build E1 (new engine with condensing) |

## 15. Outstanding work
E1 work, in order (`docs/14_…` §3): E1.1 model core · E1.2 converter and live sync · E1.3 condensing in
`je::present` · E1.4 display score · E1.5 interface · E1.6 engine store and style preservation · E1.7 AAC
replacement · E1.8 full Windows and macOS test pass, then the E1 pre-release. No user decisions are pending.

## 16. Important technical decisions
D-E1 same repository, `engine/next` · D-E2 stable checkpoint · D-E3 GPL v3 · D-E4 C++17 core without Qt ·
D-E5 player-centred model, parts as layouts · D-E6 transactions and change sets · D-E7 staged cached pipeline with
windowed breaking · D-E8 keep audio, draw, readers and importers · D-E9 shadow mode → opt-in switch → default ·
D-E10 MuseScore 4.x format; Jubelle data in versioned meta-tag JSON (tested) · D-E11 condensing only in the new
engine, after approval · D-E12 configurable condensing groups, per-section defaults as data · D-E13 changes anchored
by time and IDs · D-E14 performance goals · D-E15 AAC recommendation · D-E16 hidden developer switch ·
D-E17 private scores never in Git · D-E18 abbreviated labels · D-E19 style settings in metadata · D-E20 next testing
build uses the new engine with condensing · D-E21 E1 scope.
Full text and reasons: `jubelle_engine/docs/07_decisions_log.md`.

## 17. Risks
| Id | Risk | Mitigation |
|---|---|---|
| R-T1 | Size: the DOM and layout are ~200k lines with decades of edge cases | port algorithms rather than reinvent them; shadow comparison on a broad corpus; incremental phases |
| R-T2 | Coupling: 317 files use the DOM directly | engine-neutral API introduced consumer by consumer; MuseScore's engine implements it too |
| R-T3 | Layout differences visible to the user | "MuseScore-compatible" mode for the comparison phase; differences reviewed, never silent |
| R-T4 | File compatibility regressions | normalised round-trip tests on the corpus; MuseScore 4.7.7 opens every written file (coexistence test) |
| R-T5 | Threading bugs | snapshots are immutable; ThreadSanitizer in CI; deterministic-result tests |
| R-T6 | Long-running dual maintenance (two engines) | the switch is per score; MuseScore's engine stays the default until Phase 7's gates pass |
| R-T7 | Condensing readability | ported tested classifier, smoothing rules, user's visual review before release |
| R-L1 | FDK AAC licence (missing notice; GPL incompatibility) | notices now; replace with FFmpeg native AAC or OS encoders (awaiting decision) |
| R-C2 | MuseScore saving a Jubelle score drops Jubelle's style values | engine: meta-tag store with style mirror; current release: proposed small fix |
| R-P1 | The user's private scores leaking into Git or reports | kept outside the repository; git-ignore backstop; CI guard; anonymous names in reports |
| R-C1 | MuseScore upstream changes after 4.7.7 | not needed; the fork stays on 4.7.7. Upstream fixes are cherry-picked only when relevant |
