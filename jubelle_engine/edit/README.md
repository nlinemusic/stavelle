# jubelle_engine/edit

Transactions, change sets, undo/redo, the edit operations behind INotationInteraction, and transaction hooks (linked passages).

Status (S3 in progress, see `../../JUBELLE_ENGINE_DEVELOPMENT_MASTER_PLAN.md`; design: `../docs/03_engine_architecture.md`,
`../docs/10_edit_vocabulary.md`):

| File | What it does |
|---|---|
| `changeset`, `history`, `structure` | transactions with hooks, change sets, undo and redo, structural edits |
| `scorebuilder` | building a score in the model (tests, new scores) |
| `operations` | MuseScore's note-entry rules: chords and rests over existing music, ties, notes, pitches, values, tuplets, measures, transposition |
| `noteinput` | the note-input state machine (letters, octaves, accidentals, chords, values, rests, ties, re-pitch) |
| `clipboard` | range copy, paste and delete |
| `links` | linked staves: an edit on either staff of a linked pair is made on the other in the same transaction (the `linkedStavesHook`); patched in place where the structure is the same, written again where it isn't |
| `commands` | editing commands with MuseScore's rules: enharmonic spelling, accidentals, add interval, articulations, change and swap voices, grace notes, lines, slurs, note lines, key signatures, clefs |
| `tablature` | strings and frets on tablature staves (MuseScore's StringData rules) |

Every editing transaction of the interface is to be given `linkedStavesHook`, so linked staves and tablature stay
consistent whatever command made the change.
