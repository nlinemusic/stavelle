# 15 · File handling (S2): reading and writing MuseScore 4.x files without MuseScore's engine

_Started 19 Nov 2026. Module `je::io` (`jubelle_engine/io/`), tool `je_io_tool`. No Qt, no MuseScore code._

## 1. Design
| Layer | What it does | Files |
|---|---|---|
| Archive | `.mscz` ZIP read and written with zlib: entry order, names, timestamps kept; CRCs checked; deflate or stored | `zip.{h,cpp}` |
| XML | a **lossless** tree: values kept exactly as written (still escaped); rich text kept verbatim; written back in MuseScore's layout, so an unchanged file comes back byte for byte | `xml.{h,cpp}` |
| Score file | every entry of the archive; the main score and the parts (`Excerpts/…`) as XML trees; the main score found through `META-INF/container.xml` | `scorefile.{h,cpp}` |
| Engine store | versioned JSON in the meta tag `jubelleEngine` (D-E10), mirroring Jubelle's 11 style settings and restoring them when MuseScore dropped them (D-E19) | `scorefile.{h,cpp}` |
| Reader | MuseScore 4.x XML → the Jubelle model (S1), through the bulk builder | `mscxreader.{h,cpp}` |

**Read into the model:** parts → players and staves (instrument, names, transposition, staff type); the measure
grid (actual and nominal lengths, time signatures, repeats with play counts, irregular measures, breaks); every
voice's chords, rests (full-measure rests), notes (pitch, spelling), written values, dots, nested tuplets, grace notes
(all eight kinds, before and after), two-note tremolo durations, ties (resolved by MuseScore's relative locations,
also across bars), articulations, ornaments, lyrics (verse, syllable, melisma), fingering, arpeggios, tremolos,
fermatas; clefs, keys, time signatures, tempo, dynamics, chord symbols, staff and system text, rehearsal marks, jumps,
markers, instrument changes, breaths, expressions; spanners (slurs, hairpins, voltas, ottavas, pedals, trills, lines)
with their extents; part layouts with their players; MuseScore's element ids.

**Kept only in the XML for now** (nothing is lost; listed by the corpus report): layout and appearance details
(beams, stems, note heads, accidentals' display, offsets, colours, frames), tablature string and fret, linked-staff
markers, figured bass, stickings, harp pedal diagrams. They move into the model as the subsystems that need them
(layout S7, drawing S8) are built.

**Writing now** writes the kept XML (with the engine store). Generating the XML from the model, so that edits made
in the engine are saved, is the next S2 step; it will be checked against this exact writer: for every corpus file,
the generated XML must equal the kept XML.

## 2. Results (19 Nov 2026, the repository's 2,909 scores)
| Check | Result |
|---|---|
| read as XML and archives | 2,908 (one is a MuseScore 1.x `.msc` inside an archive) |
| XML written back byte for byte | 2,672; the other 236 (older layouts) with identical content; **0 changed** |
| archives round-tripped (every entry equal) | 627 of 627 |
| MuseScore 4.x scores read into the model | **2,118 of 2,118** |
| consistent with every model rule | 2,104; the 14 others are in `corpus/known_issues.txt` (below) |
| read into the model | 15,229 measures, 62,450 chords, 70,651 notes, 30,407 rests, 905 tuplets, 1,098 grace chords, 1,403 ties, 1,656 spanners, 1,658 lyrics |
| AddressSanitizer/UBSan, whole corpus | clean |

**Known issues (14 scores, all test files):** tuplet positions in 1.14-era conversions and importer test outputs
(Capella, Guitar Pro "dotted tuplets", a MusicXML rounding-error test, TablEdit dotted rests), ties in three
linked-staff test files and one visual test. They will be settled against MuseScore's own reading of the same files
(next step). CI fails on any failure outside the list, and when a listed file starts passing.

## 2b. Against MuseScore's own reading (the comparison baseline)
MuseScore's engine (in the test tool only) reads every MuseScore 4.x score of the repository and lists its music in
the same form as `je_io_tool listing` (staff, voice, time, duration, grace, pitches; MuseScore's invisible gap rests
left out). `tools/jubelle_checks/je_reference_compare.py` compares the two listings file by file; the CI job
`je_reference` of "Jubelle: Platform Reliability Tests" (input `engine_reference=yes`) runs it on Linux.

| Run (19 Nov 2026) | Identical music |
|---|---|
| first comparison | 1,983 of 2,118 |
| multi-measure rest measures skipped (MuseScore's layout-only measures), after-graces in MuseScore's order | 2,037 |
| end-of-measure time signatures; measure rests fill their measure; gap rests left out of MuseScore's listing | 2,078 |
| two-note tremolos inside tuplets | 2,079 of 2,118 (98.2%) |
| voice 1 completed with rests as MuseScore does (judged by the last chord or rest, not a rewound cursor); local time signatures (stretch); full tuplets close; a repeated duration type: the last counts | 2,106 of 2,118 (99.4%) |
| a stretched (local) time signature on the first staff gives the measure its global length; completing rests and measure rests on such a staff carry its stretch | **2,109 of 2,118 (99.6%)** |

**The 9 remaining** are listed with their causes in `corpus/reference_differences.txt`; the CI job fails on any
other difference and when a listed file starts matching. Five are invalid files (four tuplets whose notes overflow
them, which MuseScore repairs by its own heuristics while Jubelle keeps them as written and warns; grace chords with no
duration type, which MuseScore reads as negative durations). One is a supported case not yet handled (a hole inside a
tuplet in voice 1). Three are not yet understood (a hidden-staff test, a Guitar Pro line-elements test, a dotted
1/128 rest split). The stretch fix also explained the two copy-paste files and `beams-32`.

## 2c. Writing from the model (D-E27, D-E28)
`io/mscxwriter` generates the score file from the model alone: parts, staves, instruments, measures, every voice's
chords, rests, notes, tuplets (nested), grace notes, ties, articulations, lyrics, the flow items, spanners, frames,
style, metadata, part layouts (generated and linked, D-E28) and the archive's other entries. What the model doesn't
hold yet (an element's appearance) travels with its object as a fragment and a fingerprint of its music, and is
written only while the music is unchanged (its place in time is not part of the fingerprint, since 26 Nov: a
chord's stem direction stays when a measure is inserted before it; a chord symbol's look stays when it is
transposed). Chord symbols are modelled with their root, bass and name, from 4.60 inside MuseScore's
`<harmonyInfo>` (a polychord, with several, is carried as written). Objects made by editing are translated from the engine's own vocabulary
(a slur anchored to a chord, "accent", "Cmaj7", notes without a spelling, a transposition in semitones only).

| Check (19 Nov 2026, Linux; CI runs the first on Windows, macOS and Linux) | Result |
|---|---|
| `je_io_tool regenerate`: read → generate → read again; music, every model field, consistency, the second generation byte-identical | **2,118 of 2,118** |
| appearance details written / dropped without an edit | 55,784 / 0 |
| MuseScore 4.7.7's engine reads the generated file exactly as the original (`je_generated_compare.py`) | **2,110 of 2,117** (7 expected: files Jubelle itself reads differently) |
| MuseScore warnings reading the generated files | none beyond those it gives for the originals (and none of the originals' 84 broken part links) |
| the same in detail: ties, slurs and other lines, articulations, spellings, every line and annotation (`JUBELLE_REFERENCE_DETAIL`), and **MuseScore's own MIDI rendering** (`JUBELLE_REFERENCE_MIDI`) | **2,107 of 2,117** (the 7 above, and 3 importer test files listed in `corpus/generated_playback_differences.txt`, not yet understood) |
| official MuseScore 4.7.7 (Windows CI): MIDI export of the original and of the generated file identical | 50 of 54 on 19 Nov (4 found: linked tablature staves and bends, fixed since; rerun pending) |
| save-and-reopen tests (`tests/io_writer_tests.cpp`): an ensemble score built in the model; second save identical; edits after reopening; removed and added music; the 11 Jubelle style settings; ties and slurs in MuseScore's places | 7 of 7 |

Found and fixed on the way: note-to-note lines (glissandos, bends) were dropped by the reader; tie identities;
details after a chord's notes and after a note's pitch (MuseScore finds a note's lines by its place);
multi-measure-rest measures referred to by system locks; spanner ends at a barline; slurs and hammer-ons kept
inside their chords; ties and lines to another voice, staff or grace note; grace notes in MuseScore's order and
numbering; unison ties; linked staves within a score (a tablature staff played twice); grace notes' own lengths.
Not yet modelled, so not written for an edited score: part-only content (texts or positions only in a part).

Each fixed behaviour has a regression test (`tests/io_reader_regression_tests.cpp`) whose expected listing is what
MuseScore's engine reads from the same XML.

## 3. Performance (this machine, the same files; MuseScore's engine measured with the existing baseline tool)
| Score | MuseScore's engine: read (no layout) | Jubelle engine: open (parse + model) | Jubelle engine: memory |
|---|---:|---:|---:|
| Large orchestra (39 staves × 285 bars, 14 MB XML) | 918 ms | 235–360 ms | XML 64.4 MB + model 25.4 MB |
| Big Score (26 × 216) | 141 ms | 113 ms | XML 34.6 MB + model 13.8 MB |

MuseScore's engine holds 115 MB for the large score (with layout). The Jubelle engine's kept XML is now its largest
cost; it shrinks when the generated writer lets modelled elements leave the XML (and with shared element names).
Writing is not compared yet: MuseScore generates the whole file (1,279 ms), Jubelle writes the kept XML (35–42 ms).

## 4. Compatibility tests (CI)
- `Jubelle: Platform Reliability Tests` (Windows, macOS, Linux): the corpus check above on every push to `engine/**`,
  plus the large-score timings.
- `Stavelle: coexistence with MuseScore`, input `engine_compat=yes` (Windows, official MuseScore 4.7.7):
  `tools/jubelle_checks/je_musescore_compat.py` — for a fixed sample of 4.x scores, MuseScore's MIDI of each
  original and of the Jubelle-written file must be identical; MuseScore saves the Jubelle-written file and the
  Jubelle engine must read the same music from it; a score with all 11 Jubelle style settings goes through a
  MuseScore save and the engine must restore all 11.

**Results (19 Nov 2026, official MuseScore 4.7.7 on Windows; runs 44462568622, 44463377685):**
- 54 scores (6 fixed: the demos, Big Score, the Big Band test chart, Jubelle's feature score; 48 from `vtest/scores`):
  **MuseScore opened every Jubelle-written file and its MIDI was identical to the original's in all 54**;
  MuseScore saved every one; **for all 52 MuseScore 4.x originals the Jubelle engine read exactly the same music back
  from MuseScore's save**. (Two demos are MuseScore 3.01 files, which the native reader doesn't read yet; for them
  that check doesn't apply.)
- **Jubelle's 11 style settings: MuseScore's save dropped them; the Jubelle engine restored 11 of 11.** The first
  run restored 0: in a 4.x archive the style lives in `score_style.mss`, not in the score's `<Style>`. The engine
  store now follows `score_style.mss` (fixed the same night; the second run passed).
- The corpus check passes on Windows, macOS and Linux (no failure outside the known-issues list); the large orchestra
  opens in 194–412 ms on the CI machines.

## 5. Private scores
`je_io_tool corpus ~/JubellePrivateScores --private` reads the user's own scores locally, names them only
"private #n" and writes nothing into the repository (D-E17). On 19 Nov the folder wasn't found on the connected
computer (`nick-pc`, Windows), so the private run hasn't happened yet.
