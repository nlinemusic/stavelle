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
constexpr Field TIMESIG_NUM = ENGINE_BASE + 30;   // int64
constexpr Field TIMESIG_DEN = ENGINE_BASE + 31;   // int64

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

// chords and rests
constexpr Field DOTS = ENGINE_BASE + 60;     // int64
constexpr Field TUPLET = ENGINE_BASE + 61;   // Id: the tuplet the event belongs to
constexpr Field GRACE = ENGINE_BASE + 62;    // bool

// tuplets
constexpr Field RATIO_NUM = ENGINE_BASE + 70;   // int64: 3 in "3 in the time of 2"
constexpr Field RATIO_DEN = ENGINE_BASE + 71;   // int64

// attachments, spanners, flow items
constexpr Field ANCHOR = ENGINE_BASE + 80;       // Id: start object (spanner) or the staff (staff-specific flow item)
constexpr Field END_ANCHOR = ENGINE_BASE + 81;   // Id
constexpr Field TEXT = ENGINE_BASE + 82;         // string
constexpr Field INT_VALUE = ENGINE_BASE + 83;    // int64: clef type, key (fifths), tempo (×1000 beats per second)…

//! MuseScore properties without an engine field yet are kept by name, numbered from here (je::io's registry)
constexpr Field NAMED_BASE = 200000;
}

std::string_view fieldName(Field field);
}
