/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 *
 * Object kinds and the engine's field vocabulary (docs/09_model_mapping.md).
 *
 * The structure of a score:
 *   Score ─┬─ Flow ─┬─ Measure          (time grid; ordered by TIME)
 *          │        └─ FlowItem         (clef, key, time signature, tempo, rehearsal mark, …)
 *          ├─ Player ── Staff ── Voice  (a voice belongs to one flow: FLOW)
 *          │                      ├─ Chord ─┬─ Note ── Attachment
 *          │                      │         └─ Attachment
 *          │                      ├─ Rest ── Attachment
 *          │                      └─ Tuplet
 *          ├─ Spanner                  (slur, hairpin, …: anchored by ids and times)
 *          └─ Layout                   (full score, parts, custom; per-layout settings)
 *
 * Field numbers are stable (they appear in change sets and in the engine store); new fields
 * are added, never renumbered. MuseScore's property ids are mapped to these by name in je::io.
 */
#pragma once

#include <cstdint>
#include <string_view>

#include "value.h"

namespace je::model {
enum class Kind : int64_t {
    None = 0,
    Score = 1,
    Flow = 2,
    Measure = 3,
    FlowItem = 4,
    Player = 5,
    Staff = 6,
    Voice = 7,
    Chord = 8,
    Rest = 9,
    Note = 10,
    Tuplet = 11,
    Attachment = 12,
    Spanner = 13,
    Layout = 14,
};

std::string_view kindName(Kind kind);

namespace fields {
constexpr Field ENGINE_BASE = 100000;

// structure (engine-managed)
constexpr Field EXISTS = ENGINE_BASE + 1;     // bool: the object is there
constexpr Field KIND = ENGINE_BASE + 2;       // int64: Kind
constexpr Field PARENT = ENGINE_BASE + 3;     // Id
constexpr Field CHILDREN = ENGINE_BASE + 4;   // IdList, in order

// time (flow time, whole notes: 1/4 = a quarter)
constexpr Field TIME = ENGINE_BASE + 10;      // Fraction: start
constexpr Field DURATION = ENGINE_BASE + 11;  // Fraction: actual (sounding) length in the flow
constexpr Field FLOW = ENGINE_BASE + 12;      // Id: the flow a voice belongs to

// names and numbering
constexpr Field NAME = ENGINE_BASE + 20;        // string
constexpr Field SHORT_NAME = ENGINE_BASE + 21;  // string
constexpr Field NUMBER = ENGINE_BASE + 22;      // int64: player number (Trumpet 1, 2…), voice index (0–3)…
constexpr Field SUBKIND = ENGINE_BASE + 23;     // string: "staccato", "slur", "clef", "tempo"…

// measures and the time grid
constexpr Field TIMESIG_NUM = ENGINE_BASE + 30;    // int64: nominal (the time signature's); DURATION is the actual length
constexpr Field TIMESIG_DEN = ENGINE_BASE + 31;    // int64
constexpr Field REPEAT_START = ENGINE_BASE + 32;   // bool
constexpr Field REPEAT_END = ENGINE_BASE + 33;     // int64: play count at an end repeat (≥ 2)
constexpr Field IRREGULAR = ENGINE_BASE + 34;      // bool: excluded from measure numbering
constexpr Field BREAK = ENGINE_BASE + 35;          // string: line, page, section

// players and instruments
constexpr Field INSTRUMENT = ENGINE_BASE + 40;            // string: instrument id ("trumpet")
constexpr Field FAMILY = ENGINE_BASE + 41;                // string: "brass", "woodwinds", "strings"…
constexpr Field TRANSPOSE_CHROMATIC = ENGINE_BASE + 42;   // int64: semitones, written → sounding
constexpr Field TRANSPOSE_DIATONIC = ENGINE_BASE + 43;    // int64: steps

// notes
constexpr Field PITCH = ENGINE_BASE + 50;    // int64: MIDI pitch (sounding)
constexpr Field TPC = ENGINE_BASE + 51;      // int64: tonal pitch class, concert
constexpr Field TPC2 = ENGINE_BASE + 52;     // int64: tonal pitch class, written
constexpr Field TIE_TO = ENGINE_BASE + 53;   // Id: the note this one is tied to

// chords and rests (and tuplets: BASE_DURATION, TUPLET)
constexpr Field DOTS = ENGINE_BASE + 60;            // int64
constexpr Field TUPLET = ENGINE_BASE + 61;          // Id: the innermost tuplet the event (or tuplet) belongs to
constexpr Field GRACE = ENGINE_BASE + 62;           // bool: a grace chord (takes no time in the voice)
constexpr Field BASE_DURATION = ENGINE_BASE + 63;   // Fraction: written note value without dots (1/8 = eighth);
                                                    // DURATION = BASE × dots × every enclosing tuplet's ratio
constexpr Field GRACE_TYPE = ENGINE_BASE + 64;      // string: acciaccatura, appoggiatura, grace4, grace16, grace32,
                                                    // grace8after, grace16after, grace32after
constexpr Field MEASURE_REST = ENGINE_BASE + 65;    // bool: a full-measure rest
constexpr Field STRETCH = ENGINE_BASE + 67;             // Fraction: a staff's local time signature (MuseScore's
                                                        // stretch, e.g. 3/4 against 9/8 = 2/3): actual = written / STRETCH
constexpr Field EXPLICIT_DURATION = ENGINE_BASE + 66;   // bool: DURATION is given, not the written value (the two
                                                        // chords of a two-note tremolo each sound for half)

// tuplets
constexpr Field RATIO_NUM = ENGINE_BASE + 70;   // int64: 3 in "3 in the time of 2"
constexpr Field RATIO_DEN = ENGINE_BASE + 71;   // int64

// attachments, spanners, flow items
constexpr Field ANCHOR = ENGINE_BASE + 80;       // Id: start object (spanner) or the staff (staff-specific flow item)
constexpr Field END_ANCHOR = ENGINE_BASE + 81;   // Id
constexpr Field TEXT = ENGINE_BASE + 82;         // string: MuseScore rich text (markup kept, escaped as in the file)
constexpr Field INT_VALUE = ENGINE_BASE + 83;    // int64: key (fifths), volta ending, ottava size…
constexpr Field REAL_VALUE = ENGINE_BASE + 84;   // double: tempo (quarter notes per second), velocity change…
constexpr Field VERSE = ENGINE_BASE + 85;        // int64: lyric verse, from 0
constexpr Field SYLLABIC = ENGINE_BASE + 86;     // string: single, begin, middle, end

// layouts (full score, parts)
constexpr Field PLAYERS = ENGINE_BASE + 90;      // IdList: the players a layout shows, in order

// where an object came from in a file (je::io), kept so files round-trip
constexpr Field ORIGIN_EID = ENGINE_BASE + 95;   // string: MuseScore's element id (<eid>), as written
constexpr Field TAG = ENGINE_BASE + 96;          // string: the file element it was read from, where kinds share one
                                                 // representation (Articulation or Ornament, Chord or MeasureRepeat…)
constexpr Field VOICE_INDEX = ENGINE_BASE + 97;  // int64: the voice (0–3) a flow item or spanner was written in
constexpr Field SEQ = ENGINE_BASE + 98;          // int64: order among items at the same time (file order)
constexpr Field AT_END = ENGINE_BASE + 99;       // bool: after the last chord or rest of its measure's voice
constexpr Field HOME_STAFF = ENGINE_BASE + 100;  // Id: where a system-wide item is written (MuseScore keeps them in a staff)

// score-wide data
constexpr Field STYLE = ENGINE_BASE + 110;       // string: style values, "name<TAB>value" lines (values as in the file)
constexpr Field META = ENGINE_BASE + 111;        // string: score properties (title, composer…), "name<TAB>value" lines
constexpr Field ARCHIVE_EXTRAS = ENGINE_BASE + 112;   // string: other score data kept in the archive (mixer and view
                                                      // settings): entries packed as name, size, bytes
constexpr Field MUSESCORE_INSTRUMENT = ENGINE_BASE + 113;   // string: MuseScore's instrument id ("wind.flutes.flute")

// details the model doesn't represent yet (appearance: stems, beams, offsets, colours, accidental display,
// instrument channels…), carried on their object as an XML fragment, and written back only while the object's
// musical fields are unchanged: DETAILS_KEY is their fingerprint when the details were read (decision D-E27)
constexpr Field DETAILS = ENGINE_BASE + 120;
constexpr Field DETAILS_KEY = ENGINE_BASE + 121;
constexpr Field DETAILS_2 = ENGINE_BASE + 122;   // a second fragment (an Instrument's, a tie's, a Part's)
constexpr Field SCORE_HEAD = ENGINE_BASE + 123;  // string: the file's first elements (programVersion…), on the score
constexpr Field SCORE_PRE = ENGINE_BASE + 124;   // string: score settings before the parts (showInvisible, Order…)
constexpr Field SCORE_POST = ENGINE_BASE + 125;  // string: anything after the music
constexpr Field DETAILS_AFTER = ENGINE_BASE + 128;   // string: details that come after an element's notes (a chord's)
constexpr Field TIE_EID = ENGINE_BASE + 129;
constexpr Field START_EVENT = ENGINE_BASE + 130;     // Id: the chord or rest a line starts in (slurs, hammer-ons: MuseScore keeps them in it)
constexpr Field END_STAFF = ENGINE_BASE + 132;       // Id: the staff a line ends on, when not its own (cross-staff)
constexpr Field END_VOICE = ENGINE_BASE + 133;       // int: the voice a line ends in, when not its own
constexpr Field LINK_EID = ENGINE_BASE + 135;        // string: the element (or staff) of the same score this one is linked to (a linked staff)
constexpr Field LINK_MARK = ENGINE_BASE + 136;       // string: before format 4.60, "main" or "linked:<location>" (links by place)
constexpr Field TIE_LINK = ENGINE_BASE + 137;        // string: the tie this note's tie is linked to
constexpr Field GRACE_LENGTH = ENGINE_BASE + 138;    // Fraction: a grace chord's own played length (<duration>), when given
constexpr Field VALUE_TAG = ENGINE_BASE + 139;       // string: the tag a value was read from, when a format has several (a 4.00 key's <accidental>)
constexpr Field END_GRACE = ENGINE_BASE + 134;       // int: the grace chord a line ends on (MuseScore's grace index)
constexpr Field SOURCE_FORMAT = ENGINE_BASE + 127;   // string: the MuseScore format the score was read from ("4.40"), written back
constexpr Field LAYOUT_BREAKS = ENGINE_BASE + 126;   // string: a part layout's own line/page/section breaks (measure index, kind)

// tablature and linked staves (S3)
constexpr Field FRET = ENGINE_BASE + 140;            // int64: a note's fret on a fretted instrument (0 = open string)
constexpr Field STRING = ENGINE_BASE + 141;          // int64: its string, from the highest (0), as MuseScore numbers them
constexpr Field LINKED_STAFF = ENGINE_BASE + 142;    // Id: on a linked staff (standard and tablature of one guitar), the
                                                     // staff it shows the music of; the two always hold the same music
constexpr Field FRETS = ENGINE_BASE + 144;           // int64: a fretted instrument's number of frets (a player's)
constexpr Field STRING_TUNING = ENGINE_BASE + 145;   // string: its open strings' pitches, lowest first ("40 45 50 55 59 64");
                                                     // "*" marks a string that is never fretted (a banjo's drone)
constexpr Field TIE_MARK = ENGINE_BASE + 146;        // string: like LINK_MARK, for the tie that starts at a note (before 4.60)
constexpr Field LINE_DETAILS = ENGINE_BASE + 147;    // string: a note's lines to another note whose other end wasn't found, as written
constexpr Field TIE_GENERATED = ENGINE_BASE + 149;   // bool, on a note: its tie was made by rewriting measures (a time
                                                       // signature change); such pieces join again in a later rewrite,
                                                       // as in MuseScore (not saved: MuseScore doesn't keep it either)
constexpr Field STAFF_MOVE = ENGINE_BASE + 151;      // int64, on a chord or rest: shown on the staff above (-1) or below (+1)
                                                     // in its instrument (cross-staff notation; MuseScore's staffMove)
constexpr Field BASS = ENGINE_BASE + 150;            // int64: a chord symbol's bass note (TPC), "C7/E"
constexpr Field INLINE_PART = ENGINE_BASE + 148;     // bool: a part layout written inside the main score (a plain .mscx's parts)
constexpr Field LINKED_TO = ENGINE_BASE + 143;       // Id: on an element of a linked staff made by Jubelle, its original on
                                                     // the main staff (written as MuseScore's link)

/* Flow items (SUBKIND): clef, keySig, timeSig, tempo, harmony (chord symbol), dynamic, staffText, systemText,
 * rehearsalMark, jump, marker, instrumentChange, breath, fermata, … — ANCHOR = the staff, or absent for the
 * whole system; TIME; TEXT / INT_VALUE / REAL_VALUE; an instrument change also carries INSTRUMENT, NAME,
 * SHORT_NAME and the transposition fields.
 * Attachments (SUBKIND): articulations (staccato, accent, …), ornaments, lyric (VERSE, SYLLABIC, TEXT, DURATION
 * for a melisma), fingering, arpeggio, tremolo, chordLine, …
 * Spanners (SUBKIND): slur, tie-like lines, hairpin, ottava, pedal, trill, volta (INT_VALUE ending, TEXT
 * "1, 2"), textLine, letRing, palmMute, glissando, … — ANCHOR / END_ANCHOR = events or staves; TIME; DURATION. */

//! MuseScore properties without an engine field yet are kept by name, numbered from here (je::io's registry)
constexpr Field NAMED_BASE = 200000;
}

std::string_view fieldName(Field field);
}
