# 09 · Model mapping: MuseScore DOM → Jubelle engine model (Phase 2.3)

Every `ElementType` in MuseScore 4.7.7 (`engraving/types/types.h`) is mapped to its place in the new model, or marked
as **derived** (rebuilt by presentation or layout, never stored) or **UI-only**. The converter (Phase 3.3) uses this
table as its specification. Its exit criterion is that 100% of the types used by the corpus are mapped and
round-trip.

## 1. Model kinds (new engine)
| Kind | Meaning | Stored in |
|---|---|---|
| **Score** | metadata, styles, flows, players, layouts, Jubelle store | `je::model::Score` |
| **Flow** | the time grid: measures, meter, key, tempo, barlines, repeats, markers | `Flow` |
| **Player** | a performer: name, number, instruments over time, own staves (1 or more, e.g. piano 2) | `Player` |
| **Event** | a chord or rest in a voice of a player's staff, at a `Fraction` time | `Voice` |
| **Note** | a pitch in a chord (pitch, tpc1/tpc2, tie, head, string/fret) | `Event` |
| **Attachment** | something on one event or note (articulation, ornament, fingering, lyric syllable, chord symbol…) | `Event` or `Note` |
| **Spanner** | something over a time range (slur, hairpin, ottava, pedal, line…) | `Score.spanners` (by player/voice and time) |
| **Flow item** | something on the time grid for everyone or a staff (clef, key, time signature, tempo, rehearsal mark, system text, jump, marker, volta, break, spacer) | `Flow` (global) or `Player` (staff-specific) |
| **Frame** | vertical, horizontal, text or fretboard frames | `Flow` (between measures) and per-layout |
| **Layout item** | per-layout data: breaks, locks, spacers, stretch, position offsets, visibility overrides | `Layout.overrides[id]` |
| **Derived** | rebuilt by presentation or layout; never stored | n/a |
| **UI-only** | selection, lasso, shadow note, action icons, indicators | the bridge |

## 2. Mapping table
| MuseScore type(s) | New model | Notes |
|---|---|---|
| SCORE (MasterScore) | **Score** | excerpts (part Scores) become **Layouts** |
| PART | **Player** | `Part` ≈ Player; instrument changes stay on the player |
| STAFF | Player staff (index within player) | presentation staves are derived per layout |
| STAFF_STATE, STAFFTYPE_CHANGE | Flow item (staff-specific) | |
| INSTRUMENT_CHANGE | Player instrument change at a time | |
| INSTRUMENT_NAME | Derived | from player/instrument names and layout naming style |
| MEASURE | Flow measure (time range, meter, number, repeat flags, irregular) | layout properties (stretch, width, breaks) → Layout item |
| HBOX, VBOX, TBOX, FBOX | Frame | content (text, images, fretboard legend) kept |
| MMREST, MMREST_RANGE | Derived | from layout option and empty measures |
| SEGMENT | Derived | columns are built by layout |
| SYSTEM, PAGE, SYSTEM_DIVIDER | Derived | |
| BRACKET, BRACKET_ITEM | Score brackets (by player ranges, per layout) | `BRACKET` drawing derived |
| BAR_LINE | Flow item (barline type at a time; staff span → layout) | |
| CLEF | Flow item (player staff), courtesy clefs derived | |
| KEYSIG, TIMESIG | Flow item (global or per staff for local time signatures) | courtesy and cancel signs derived |
| CHORD | **Event** (chord) | stem direction, beam mode, small, no-stem: properties |
| REST | **Event** (rest) | gap rests, deadslapped: properties |
| NOTE | **Note** | |
| NOTEHEAD, NOTEDOT, STEM, HOOK, LEDGER_LINE, STEM_SLASH | Derived | properties of note/chord control them |
| ACCIDENTAL | Note property (explicit accidental, bracket) | display (courtesy, cancellation) derived |
| BEAM | Event property (beam mode); feathering and user positions → Layout item | beams themselves derived |
| TUPLET | **Tuplet group** in the voice (ratio, time range, nested) | brackets and numbers derived |
| GRACE_NOTES_GROUP | Event (grace) attached to the main event | |
| TIE, LAISSEZ_VIB, PARTIAL_TIE | Note relation (tie to next note ID / l.v. / partial) | segments derived |
| SLUR, TEXTLINE, PEDAL, HAIRPIN, OTTAVA, TRILL, VIBRATO, LET_RING, PALM_MUTE, WHAMMY_BAR, RASGUEADO, HARMONIC_MARK, PICK_SCRAPE, GLISSANDO, NOTELINE, VOLTA, GRADUAL_TEMPO_CHANGE, HAMMER_ON_PULL_OFF, TAPPING_HALF_SLUR, GUITAR_BEND, GUITAR_BEND_HOLD, LYRICSLINE, PARTIAL_LYRICSLINE | **Spanner** (kind, start and end anchors, properties) | every `*_SEGMENT` type is derived |
| ARTICULATION, ORNAMENT, FERMATA, BREATH, ARPEGGIO, CHORDLINE, CHORD_BRACKET, TREMOLO_SINGLECHORD, TREMOLO_TWOCHORD, BEND, TREMOLOBAR, STICKING, BAGPIPE_EMBELLISHMENT, FINGERING, TAPPING, PARENTHESIS | **Attachment** on event or note | two-chord tremolo spans two events (relation) |
| DYNAMIC, EXPRESSION | Attachment at a time (player, voice optional) | |
| LYRICS | Attachment (verse, syllable, hyphen/extender) | |
| HARMONY, FRET_DIAGRAM, HARP_DIAGRAM | Attachment at a time (player or score) | chord realisation derived; a chord symbol's root (`INT_VALUE`), bass (`BASS`) and name (`TEXT`) are modelled (from 4.60 read from and written to `<harmonyInfo>`) |
| FIGURED_BASS, FIGURED_BASS_ITEM | Attachment (time, duration) | |
| STAFF_TEXT, SYSTEM_TEXT, TEMPO_TEXT, REHEARSAL_MARK, PLAYTECH_ANNOTATION, CAPO, STRING_TUNINGS, TRIPLET_FEEL, SOUND_FLAG, PLAY_COUNT_TEXT | Flow item or Attachment (by scope) | tempo text also writes the tempo map |
| MARKER, JUMP | Flow item | playback order derived |
| MEASURE_REPEAT | Event-level repeat marker in a voice range | |
| TEXT (generic), SYMBOL, FSYMBOL, IMAGE | Attachment or Frame content | |
| LAYOUT_BREAK, SPACER, SYSTEM_LOCK_INDICATOR | **Layout item** (per layout) | indicators UI-only |
| MEASURE_NUMBER | Derived (numbering rules) | user-moved positions → Layout item |
| AMBITUS | Attachment (staff, auto range derived) | |
| TAB_DURATION_SYMBOL, DEAD_SLAPPED, GUITAR_BEND_TEXT, HAMMER_ON_PULL_OFF_TEXT, TAPPING_TEXT | Derived | |
| TIME_TICK_ANCHOR | UI-only | |
| SELECTION, LASSO, SHADOW_NOTE, ACTION_ICON, STAFF_VISIBILITY_INDICATOR, STAFF_LINES, ROOT_ITEM, DUMMY | UI-only / derived | |

## 3. Properties
- Every stored object carries properties in **the engine's own field vocabulary** (`model/fields.h`; decision
  D-E24, revised from "by `Pid`" so the engine doesn't depend on MuseScore's numbering). `je::io` maps MuseScore's
  property names to fields when reading and writing, and properties without an engine field yet are kept by name
  (numbered from `NAMED_BASE`), so the Properties panel, style defaults and the file format keep their meaning.
- **Per-layout values** (offsets, placement, visibility, size, autoplace) are stored as overrides
  `Layout.overrides[objectId][Pid]`. The score layout's overrides correspond to the master score; part layouts' to
  the excerpts' unlinked values.
- **Layout-computed values** (MuseScore's `ldata()`: bbox, pos, stem direction when automatic, …) are never stored.

## 4. Identity
- Every stored object has a `je::Id` (64-bit). On import, MuseScore's `<eid>` values (present in 4.x files, one per
  element) seed the IDs, so IDs are stable across load and save and **survive a MuseScore round trip** (verified by
  the storage test, `13_mscz_storage_results.md`). Objects without an `eid` (older formats) get new IDs.
- Derived objects get deterministic derived IDs (`hash(source ID, layout, role)`), so selection survives relayout.

## 5. Jubelle data
| Jubelle data today | New model |
|---|---|
| `Sid::jubelle…` style values | style values (unchanged names); **also mirrored into the Jubelle store** because MuseScore drops unknown style values on save (`13_…`) |
| meta tags (`jubelleSwing`, `jubelleArticulationPlayback`, `jubelleTempoPoints`, `jubelleCondense…`, …) | Jubelle store (meta tag JSON, survives MuseScore saving) |
| `Pid::JUBELLE_PLAY_REPEAT` and other element-level Jubelle attributes | Jubelle store entries keyed by element `eid` (element tags are dropped by MuseScore, `13_…`) |
| linked passages, timeline markers | Jubelle store (relations keyed by IDs and flow times) |
