# 10 · Edit vocabulary: `INotationInteraction` → transactions (Phase 2.4)

`INotationInteraction` (215 methods) is the score view's contract with the engine. The new engine implements the
same interface in the bridge. Each method is in one of five classes, and the table says how it is served.

| Class | Served by | Undo |
|---|---|---|
| **Q: Query** (hit testing, selection state, availability) | `je::render` hit testing, selection model in the bridge | n/a |
| **S: Selection and navigation** | bridge selection model over IDs | n/a |
| **E: Edit** (changes the music) | one `je::edit` **transaction** made of model operations | one undo step |
| **L: Layout edit** (changes only a layout) | transaction on `Layout` overrides | one undo step |
| **I: Interactive edit** (drag, grip, text editing) | a **transaction kept open** during the gesture, committed at the end; previews use a scratch snapshot | one undo step at the end |

## Model operations (the transaction vocabulary)
`addEvent`, `removeEvent`, `setEventDuration`, `splitEvent`, `addNote`, `removeNote`, `setNotePitch`
(pitch + spelling), `setTie`, `addAttachment`, `removeAttachment`, `addSpanner`, `setSpannerRange`,
`removeSpanner`, `setProperty(id, Pid, value)`, `setLayoutOverride(layout, id, Pid, value)`, `insertTime`,
`removeTime`, `addMeasures`, `removeMeasures`, `setFlowItem`, `addPlayer`, `removePlayer`, `setInstrument`,
`setStyle`, `storeJubelle(key, value)`. Each records **before/after** values by ID. Composite edits (paste,
explode, transpose, harmonise) are sequences of these.

## Mapping of the interface (grouped)
| Methods | Class | Notes |
|---|---|---|
| `hitElement`, `hitElements`, `hitStaff`, `hitElementContext`, `contextItem`, `shadowNoteRect`, `isHitGrip`, `moveSelectionAvailable`, `canAddTupletToSelectedChordRests`, `canInsertClef`, `canAddImageToItem`, `isEditAllowed`, `textEditingAllowed`, `isOutgoingDragElementAllowed`, `selectedMeasure` | Q | display-list hit testing; IDs returned |
| `select`, `selectAll`, `selectSection`, `selectFirstElement`, `selectLastElement`, `clearSelection`, `selectTopOrBottomOfChord`, `findAndSelectChordRest`, `moveChordNoteSelection`, `moveSelection`, `expandSelection`, `addToSelection`, `selectTopStaff`, `selectEmptyTrailingMeasure`, `moveSegmentSelection`, `navigateTo…` (lyrics, harmony, figured bass, text), `selectText`, `showItem` | S | selection over IDs; on condensed staves maps presentation → source (spec `04_…` §10) |
| `noteInput`, shadow-note methods | E (via note input state) | note input is a state machine in the bridge emitting transactions |
| `applyPaletteElement`, `dropSingle`, `dropRange`, `startDrop…`, `updateDrop…`, `endDrop` | E | palette element → operations by kind |
| `copySelection`, `pasteSelection`, `repeatSelection`, `copyLyrics`, `swapSelection`, `deleteSelection`, `removeSelectedMeasures`, `removeSelectedRange`, `removeEmptyTrailingMeasures`, `splitSelectedMeasure`, `joinSelectedMeasures`, `addBoxes` | E | clipboard keeps MuseScore's MSCX fragment format (interoperable) |
| `movePitch`, `transpose`, `transposeSemitone`, `transposeDiatonicAlterations`, `changeEnharmonicSpelling`, `spellPitches…`, `addIntervalToSelectedNotes`, `swapVoices`, `changeSelectedElementsVoice…`, `moveChordRestToStaff`, `swapChordRest`, `explodeSelectedStaff`, `implodeSelectedStaff`, `regroupNotesAndRests`, `increaseDecreaseDuration`, `putRest`, `putRestToSelection`, `fillSelectionWithSlashes`, `replaceSelectedNotesWithSlashes`, `realizeSelectedChordSymbols`, `extendToNextNote` | E || done in `edit/operations` and `edit/commands`: transpose, enharmonic spelling, add interval, change and swap voices, durations, rests; still to do: spell pitches, move to staff, swap chord/rest, explode/implode, regroup, slashes, realize chord symbols |
| `addTieToSelection`, `addTiedNoteToChord`, `addLaissezVibToSelection`, `addSlurToSelection`, `addHammerOnPullOffToSelection`, `addOttavaToSelection`, `addHairpinsToSelection`, `addHairpinOnGripDrag`, `addAnchoredLineToSelectedNotes`, `addGuitarBend`, `addTupletToSelectedChordRests`, `addBeamToSelectedChordRests`, `addGraceNotesToSelectedNotes`, `addBracketsToSelection`, `toggleAccidentalForSelection`, `changeAccidental`, `toggleArticulation…`, `toggleOrnament`, `toggleDotsForSelection`, `addFret`, `addFiguredBass`, `addFretboardDiagram`, `addTimeSignature`, `insertClef`, `addMelisma`, `addLyricsVerse`, `addTextToTopFrame`, `addTextToItem`, `addImageToItem`, `toggleDynamicPopup`, `autoFlipHairpinsType`, `resequenceRehearsalMarks` | E || done in `edit/operations` and `edit/commands`: ties, slurs, ottavas, hairpins, glissandos and other note lines, tuplets, grace notes, accidentals, articulations, clefs and key signatures; still to do: time signatures with rebarring, lyrics, text, figured bass, fretboard diagrams, brackets, beams |
| `toggleVisible`, `flipSelection`, `flipSelectionHorizontally`, `nudge`, `nudgeAnchors`, `toggleSnapToPrevious/Next`, `toggleAutoplace`, `addStretch`, `resetStretch`, `resetTextStyleOverrides`, `resetBeamMode`, `resetShapesAndPosition`, `resetToDefaultLayout`, `moveElementAnchors`, `updateTimeTickAnchors` | L (or E where the property is musical, e.g. beam mode) | per-layout overrides |
| `toggleLayoutBreak`, `toggleLayoutBreakAvailable`, `moveMeasureToPrevSystem/NextSystem`, `toggleSystemLock`, `toggleScoreLock`, `makeIntoSystem`, `applySystemLock`, `addRemoveSystemLocks` | L | |
| `startDrag`, `drag`, `endDrag`, `startEditGrip`, `endEditGrip`, `startEditElement`, `changeEditElement`, `editElement`, `endEditElement`, `startEditText`, `editText`, `endEditText`, `changeTextCursorPosition`, `toggleBold/Italic/Underline/Strike/SubScript/SuperScript`, `startOutgoingDrag…`, `endOutgoingDrag` | I | open transaction; text editing edits a text attachment's rich text |
| `undo`, `redo`, `undoRedoToIndex` | n/a | `je::edit` history |
| Jubelle: `identifyChords`, `chordIdentifierScope`, `removeChordToneHighlight`, `harmonyScope`, `harmoniseMelody` | E (planner on a snapshot, then one transaction) | ported Jubelle logic |
| `scoreConfig`, `setScoreConfig`, `execute`, `getLocation`, `checkAndShowError`, `toggleDebugShowGapRests`, signals (`…Changed`) | bridge | |

## Rules
1. Every E/L/I method produces **exactly one undo step** (as today), including all hook work (linked passages).
2. An operation that fails validation **rolls back** the whole transaction; the model is never half-changed.
3. Each transaction names the MuseScore action it corresponds to, so the undo history text stays the same.
4. Comparison test (Phase 4.1): scripted sequences of these methods on both engines must give equal models.
5. **Linked staves** (S3, `edit/links`): every editing transaction carries `linkedStavesHook`. After the command's own
   changes, each linked staff is brought in line with its main staff in the same transaction: an edit on a linked
   staff is first made on the main staff. Measures whose music differs are patched in place when their structure is
   the same (the objects, their identities and the file's appearance details stay; only a changed object's own
   details are dropped by their fingerprint), and written again when it isn't. Ties across barlines are reconnected,
   and lines anchored to replaced copies move to the new copies. A copy made by Jubelle carries its original
   (`LINKED_TO`), which the writer turns into MuseScore's link: `<linkedTo>` with the original's identity from 4.60,
   `<linkedMain/>` and `<linked>` with the `<indexDiff>` MuseScore's reader needs before 4.60. Tablature staves then get
   strings and frets (`edit/tablature`, MuseScore's StringData rules) for new notes and notes whose pitch changed.
   Not yet: lines and staff texts added on one staff of a pair appearing on the other.
