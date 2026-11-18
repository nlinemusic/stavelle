# Jubelle: Harmonise Melody (Big Band Harmony and Orchestration System)

**Menu:** Tools › Arranging › Harmonise Melody…  
**Source:** the Pandemonium Big Band lessons (Elliot Deutsch), as summarised in the approved proposal. Rules not in the lessons are not used.

**How it works:** deterministic and offline. The same settings always write the same notes.

## Using it
1. Write the melody (usually Trumpet 1) and the chord symbols (usually piano or guitar).
2. Select the melody bars **on the melody staff**. Voice 1 of the top staff of the selection is the melody.
3. Open the dialog and choose a technique per section and a passing-note tool.
4. **Preview** writes everything as one undo step and plays the passage.
   - **Regenerate** writes it again with changed settings.
   - **Discard** restores the score exactly.
   - **Keep** leaves it. One Undo also removes it.
5. **Save as version** (up to four) and click a version to compare.

### Defaults (from the approved proposal and your decisions)
| Setting | Default |
|---|---|
| Trumpets | Closed, or trumpet triads above concert A5 |
| Trombones | An octave below the trumpets |
| Saxophones | Full band: on the trombone notes, baritone on roots |
| Passing notes | Choose for each note (planing, else diatonic, else diminished) |
| Alterations on a resolving V | On, ♭9♯5 preferred when the melody allows |
| Half-step slide | **Off** |
| Altos fill high trumpet triads | Off |
| Changed chord symbols also on the piano staff | On (piano, else guitar, else the top staff) |
| A staff that already has notes | Left alone |

**Speed rule** (lesson 6, your thresholds):
- notes under 0.2 s count as fast;
- spread voicings need most notes longer than 0.4 s;
- short notes for the baritone are under 0.3 s.

The dialog's "Good to know" list explains anything this score can't use. For example:
- a missing baritone;
- fewer than four trumpets;
- N.C. or an unreadable symbol;
- chords, grace notes or other voices in the melody.

## Where the code is
| File | What it does |
|---|---|
| `src/notation/internal/jubelle/harmony/harmonyrules.*` | The rule engine, Qt-free |
| `src/notation/internal/jubelle/harmony/harmonyrunner.*` | Reads the score and writes the parts |
| `src/notationscene/qml/MuseScore/NotationScene/harmonisemelodymodel.*` | The dialog model |
| `src/notationscene/qml/MuseScore/NotationScene/HarmoniseMelodyDialog.qml` | The dialog |

| Tests | Where they run |
|---|---|
| `src/notation/tests/jubelle_harmonyrules_tests.cpp` | Platform tests |
| `src/notation/tests/jubelle_harmonyrunner_tests.cpp` | Engine tests |

## Behaviour that matters
**Chords**
- Read with MuseScore's chord list.
- Where there is no symbol, the chord the rhythm section plays is named by the Chord Identifier. Such chords are never altered or substituted.
- Polychords, N.C. and unreadable symbols are **not guessed**: the melody is in unison there, and the dialog and the result say where.

**Writing**
- The rhythm and markings are copied from the melody as Copy and Paste does: ties, tuplets, articulations, slurs, dynamics, hairpins, breaths.
- Lyrics, chord symbols, other text, fingering, ottavas and grace notes are not copied.
- Only the pitches are then set. Notes are written at concert pitch, and MuseScore shows each transposing part in its written key.

**Spelling:** each player's line is spelled as a whole:
1. the fewest steps that look wrong;
2. then the fewest B♯, C♭, E♯ and F♭;
3. then the chord's own spelling.

There are never double accidentals. Steps that no spelling can fix are counted in the result, and Proofread points them out. Proofread itself gained lesson 8's check: "looks like a third, sounds like a second".

**Ranges:**
- the instrument's professional range (the same data Proofread uses);
- the lessons' thresholds.

Notes outside a range are listed after Preview, never silently moved.

## Limitations
- The melody is taken from voice 1, top note.
- Existing notes: *Leave alone* or *Replace* only.
- Voicings are chosen note by note, with no voice-leading tie-break (your decision).
- The diatonic tool offers only the lesson's usual passing chord.
