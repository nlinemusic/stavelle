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

## 5. Private scores
`je_io_tool corpus ~/JubellePrivateScores --private` reads the user's own scores locally, names them only
"private #n" and writes nothing into the repository (D-E17). On 19 Nov the folder wasn't found on the connected
computer (`nick-pc`, Windows), so the private run hasn't happened yet.
