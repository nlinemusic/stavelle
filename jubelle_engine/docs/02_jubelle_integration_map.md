# 02 · Jubelle integration map: where each feature meets MuseScore, and where it goes

Every feature in release candidate 2 (v4.7.7.261118230), with its current hook into MuseScore, what it stores in
the score, and its route to the new engine. The **Phase** column is the migration phase (`06_roadmap_and_testing.md`).

Legend for **Route:**
- **Native**: rebuilt as a capability of the new engine.
- **Port**: the logic is kept and moved onto the new model's API (most Jubelle logic is already plain C++ with its
  own tests).
- **Bridge**: keeps working unchanged through the engine bridge.
- **Independent**: not tied to the engine at all.

## Engraving and layout
| Feature | Current hook | Stored as | Route | Phase |
|---|---|---|---|---|
| Courtesy accidentals | layout rule; `layoutRange` widened by one measure | `Sid::jubelleCourtesyAccidentals*` | **Native** (an accidental-state rule in the layout stage, with an explicit dependency on the previous measure) | 4 |
| Engraving rules (by instrument, group or score) | `applyEngravingRules(score)` before each layout (walks the whole score when rules exist) | `Sid::jubelleEngravingRules` (text) | **Native**: rules become style overrides resolved once per change, not per layout | 5 |
| Magnetic layout and collision avoidance | passes after autoplace (`magneticlayout`, `overlapchecker`) | `Sid::jubelleMagnetic*`, `jubelleCollisionAvoidance`, nudge settings | **Native**: part of the vertical stage (skylines) | 4–5 |
| Jazz look and engraving presets | style sets (`style/jubelle/jazzlook`) | style values, fonts | **Bridge** (style vocabulary kept) | 4 |
| Title-page alignment, template pages | notation-level (frames, text) | frames and text in the score | **Port** | 5 |
| Page turns | notation-level analysis plus breaks | breaks in parts | **Port** (parts become layouts) | 5 |
| Apply Part Layout, Prepare Parts, Export for Library | notation-level, writes into each excerpt | part styles and breaks | **Port**; simpler when parts are layouts, not copies | 5 |
| Condensed score copy | builds a separate `.mscz` with hidden staves | `jubelleCondensedCopy`, `jubelleCondensedFrom`, `jubelleCondenseOverrides` | **Replaced** by native live condensing (spec `04_…`); the planner's decision logic and tests are reused | 6 |

## Writing and arranging
| Feature | Current hook | Stored as | Route | Phase |
|---|---|---|---|---|
| Linked passages | command hook (`jubelleCmdHook` after each command, same undo step) | link records in the score | **Native**: a model-level relation; propagation runs inside the transaction | 5 |
| Harmonise Melody (big band harmony system) | reads chord symbols, writes notes as one undo step | ordinary notes | **Port** (pure planner + one transaction) | 5 |
| Chord Identifier | analysis over segments | chord symbols | **Port** | 5 |
| Proofread | analysis passes | `jubelleProofreadIntentional` | **Port** (reads the model; condensing-aware checks added in 6) | 5 |
| Find and Select | selection queries | none | **Port** (queries over IDs) | 5 |
| Harmony system test music, templates | tools | `.mscz` templates | **Bridge**; templates must open unchanged | 4 |

## Playback
| Feature | Current hook | Stored as | Route | Phase |
|---|---|---|---|---|
| Articulation playback (score, instrument, passage) | `PlaybackModel` profile per track and tick | `jubelleArticulationPlayback` meta tag, passage lines | **Port** into the new event generator (same MPE) | 4 |
| Swing feels, swing marking | note renderer timing | `jubelleSwing` meta tag, `jubelleFeel` | **Port** | 4 |
| Jazz falls, doits, bends | note renderer and FluidSynth bend shaping | articulations | **Port** (renderer); audio side unchanged | 4 |
| Repeat playback controls | `Pid::JUBELLE_PLAY_REPEAT`, volta changes | element property | **Native** (repeat expansion in the event generator) | 4 |
| Tempo recording | tempo map writes | `jubelleTempoPoints`, tempo texts | **Port** | 5 |
| Dynamics mapping, preset gains, ensemble trim, limiter, dither | audio engine (`framework/audio`) | none | **Independent** (audio side kept) | n/a |
| VST3 key switches, level, bend range | VST module | instrument settings | **Independent** | n/a |
| Drum stems | export over playback tracks | none | **Bridge** | 5 |

## Interface and application
| Feature | Route |
|---|---|
| Menus, workflow profiles, shortcuts, toolbar | **Independent** (the action codes stay; the bridge serves the same actions) |
| Properties (Jubelle section, "use default") | **Bridge**: the inspector models are migrated from DOM pointers to the engine API (Phase 4.8) |
| Timeline and markers | **Port** (`jubellePageClass`, marker store by stable position) |
| Projects, New Score, templates, Learning Center | **Independent** |
| Crash-safe saving, crash reports, Report a Bug | **Independent**; the new engine registers itself in the diagnostics registry under `engine` |
| Updater, release signing | **Independent** |
| Video (MP4), YouTube | **Bridge** (pages rendered through the display list) |
| Audio export | **Bridge** (events through MPE as now) |
| Performance profiles | **Independent**; new engine settings (threads, cache size) are added to them |

## Hooks the new engine must provide (the contract Jubelle needs)
1. **Transaction hooks:** before and after a transaction, inside the same undo step (linked passages, rules).
2. **Derived presentation stages** between model and layout: condensing, and later divisi and cue staves.
3. **Layout rule extension points:** accidentals, placement rules, magnetic rows, collision checks.
4. **Playback rule extension points:** articulation profiles, feel and timing, repeat expansion.
5. **Analysis access:** read-only, versioned snapshots of the model (proofread, chord identifier, harmony planner)
   that can run off the main thread.
6. **Extra data storage:** a namespaced store for Jubelle data that MuseScore ignores (meta tags today, see
   `03_…`, §8).
7. **Diagnostics:** engine identity and version (`muse::jubelle::DiagnosticsRegistry`, id `engine`).
