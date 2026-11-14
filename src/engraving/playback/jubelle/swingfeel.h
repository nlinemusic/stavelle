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

struct SwingSettings {
    static constexpr const char16_t* META_TAG = u"jubelleSwing";

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
};

//! Precedence: a Swing text in the score, then the instrument's feel, then
//! the score's feel. Where none of these chooses a Jubelle feel, MuseScore's
//! own swing is used unchanged.
SwingTiming swingTiming(const Chord* chord);

//! The same timing rule MuseScore uses, with a precise ratio (in percent).
//! `offbeat` is set when the chord is a swung (delayed) note.
SwingTiming swingTimingForRatio(const Chord* chord, int swingUnitTicks, double ratioPercent, bool* offbeat = nullptr);
}
