/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle Phase 5B: rhythmic feels (swing), modelled on Dorico's.
//
// A feel says how much swung notes are swung, either at every tempo or
// depending on tempo (more swing when slow, straighter when fast). Feels are
// chosen for the whole score, for one instrument, or from a point in the
// score with MuseScore's own Swing text. Playback only: the written notes
// never change.
//
// Storage, all compatible with MuseScore 4.7.7:
//  - the score and instrument choices and any edited or custom feels are one
//    score metaTag, which MuseScore keeps untouched;
//  - a feel chosen on a Swing text is an extra attribute on its <swing> tag;
//    the tag's ordinary ratio holds an equivalent fixed ratio, which is what
//    MuseScore itself plays.

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "global/types/string.h"

namespace mu::engraving {
class Chord;
class Score;
}

namespace mu::engraving::jubelle {
struct SwingFeel {
    muse::String id;        // stored name, never translated ("basie", "custom1", ...)
    muse::String name;      // shown name (built-ins are translated when shown)
    bool builtIn = false;

    int unit = 8;           // 8 = swing eighths, 16 = swing sixteenths

    bool tempoDependent = false;
    double lowTempo = 80.0;      // quarter notes per minute
    double highTempo = 240.0;
    double lowTempoRatio = 2.0;  // long:short, e.g. 2.0 = 2:1 (triplet swing)
    double highTempoRatio = 1.5;
    double ratio = 2.0;          // when not tempo-dependent

    int offbeatLoudness = 100;   // % of the normal loudness for swung offbeats
    int offbeatLength = 100;     // % of their swung length

    // Swing v2 (R5, 15 Nov 2026). All off by default, so older feels play as before.
    int minShortMs = 0;              // the short note is never shorter than this (0 = no limit)
    bool roleAware = false;          // drums and rhythm section, and horns, play differently:
    double hornDelayLowMs = 0.0;     //   horns' on-beat notes this much behind (at lowTempo..highTempo);
    double hornDelayHighMs = 0.0;    //   their offbeats stay with the drums
    double rhythmSlowMs = 0.0;       //   rhythm section: + behind / - ahead of the band at 100 bpm and slower,
    double rhythmFastMs = 0.0;       //   aligned at 120-180, and this at 220 bpm and faster
    int guitarQuarterLength = 100;   //   guitar quarter notes, % of their length (a short, light "four")
    bool hornArticulation = false;   // Basie-style horn phrasing, in playback only

    //! The long:short ratio played at a tempo, with the minimum short note applied
    double playedRatioAt(double bpm) const;
    //! Horns' delay on the beat, and the rhythm section's placement, at a tempo (ms)
    double hornDelayAt(double bpm) const;
    double rhythmPlacementAt(double bpm) const;

    //! The long:short ratio played at a tempo (quarter notes per minute)
    double ratioAt(double bpm) const;
    //! Long note as a percentage of the beat (MuseScore's "swing ratio")
    static double ratioToPercent(double ratio);
    static double percentToRatio(double percent);

    void clampValues();
    bool operator==(const SwingFeel& o) const;
};

//! Straight, Light, Medium, Heavy, 2:1, 3:1, Basie
const std::vector<SwingFeel>& builtInFeels();

//! Swing v2: how an instrument takes part in a role-aware feel
enum class SwingRole {
    Horn,       // winds, brass, voices, strings, anything else: the front line
    Drums,      // drum kit and other unpitched percussion
    Guitar,
    Bass,       // double bass, bass guitar
    Keyboard,   // piano, organ, vibraphone...: with the rhythm section
};
SwingRole swingRoleFor(const muse::String& instrumentSoundId, bool drumStaff);

//! Jubelle Phase 5C: the automatic swing marking near the first tempo marking
enum class SwingMarkingStyle {
    Off = 0,           // no automatic marking
    Text,              // e.g. "Basie swing"
    TextAndEquation,   // the text and the rhythmic equation (default)
    Custom,            // your own description
};

struct SwingSettings {
    static constexpr const char16_t* META_TAG = u"jubelleSwing";

    SwingMarkingStyle markingStyle = SwingMarkingStyle::TextAndEquation;
    muse::String markingText;      // for Custom
    muse::String markingEid;       // the marking Jubelle placed (its element id), if any

    muse::String scoreFeel;                       // empty = not set (MuseScore's style swing applies)
    std::map<uint64_t, muse::String> partFeels;   // key: Part::id(); per-instrument choice
    std::vector<SwingFeel> feels;                 // edited built-ins and custom feels

    //! The feel with this id: an edited copy, a custom feel, or the built-in one
    const SwingFeel* feel(const muse::String& id) const;
    //! Built-ins (as edited) followed by custom feels
    std::vector<SwingFeel> allFeels() const;
    //! Stores a feel; an unedited built-in is not stored
    void setFeel(const SwingFeel& feel);
    void removeFeel(const muse::String& id);
    muse::String newCustomId() const;

    bool isEmpty() const;
    muse::String toText() const;
    static SwingSettings fromText(const muse::String& text);
    static SwingSettings read(const Score* score);
    //! Undoable; call inside startCmd/endCmd
    void write(Score* score) const;

    bool operator==(const SwingSettings& o) const;
};

//! How one chord is swung
struct SwingTiming {
    bool jubelleFeel = false;      // false = leave it to MuseScore's own swing
    double startShift = 0.0;       // fraction of the written duration to start later
    double durationFactor = 1.0;   // multiplies the written duration
    double loudnessFactor = 1.0;   // multiplies the loudness
    bool offbeat = false;          // a swung (delayed) note
};

//! Precedence: a Jubelle Swing text in the score, then the instrument's feel,
//! then the score's feel. An ordinary MuseScore "Swing" text switches swing on
//! with the chosen feel (or MuseScore's ratio if none is chosen); "Straight"
//! texts play straight. Where nothing chooses a Jubelle feel, MuseScore's own
//! swing is used unchanged.
SwingTiming swingTiming(const Chord* chord);

//! The same timing rule MuseScore uses, with a precise ratio (in percent).
//! `offbeat` is set when the chord is a swung (delayed) note.
SwingTiming swingTimingForRatio(const Chord* chord, int swingUnitTicks, double ratioPercent, bool* offbeat = nullptr);

//! A feel applied to one chord at a tempo, for an instrument's role
SwingTiming swingTimingForFeel(const Chord* chord, const SwingFeel& feel, double bpm, SwingRole role);
}
