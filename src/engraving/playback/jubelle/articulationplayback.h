/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle Phase 5A: user-adjustable articulation playback.
//
// The settings change how articulations SOUND (note length, loudness, the
// shape of falls, doits, scoops, plops and glissandos, and the speed of
// turns). They never change the written score. They are stored in the score
// as a single metaTag, which MuseScore 4.7.7 reads and writes back untouched,
// so files stay fully compatible.

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "global/types/string.h"
#include "mpe/mpetypes.h"

namespace mu::engraving {
class EngravingItem;
class Part;
class Score;
class TextLine;
}

namespace mu::engraving::jubelle {
//! One adjustable value. The ranges are deliberately limited so that very fast
//! or very slow tempos still sound sensible.
enum class ArtParam : int {
    StaccatoLength = 0,      // % of the written note
    StaccatissimoLength,     // %
    TenutoLength,            // %
    MarcatoLength,           // % (rooftop accent)
    AccentLoudness,          // % louder than an unaccented note
    MarcatoLoudness,         // %
    FallStart,               // % of the note at which a fall/doit begins
    FallRange,               // semitones
    FallCurve,               // -1 slows down, 0 even, +1 speeds up
    ScoopLength,             // % of the note's length used before the note
    ScoopRange,              // semitones
    GlissandoStart,          // % of the note at which the slide begins
    GlissandoCurve,          // -1 / 0 / +1
    TurnSpeed,               // % of normal speed (jazz turn)
    Count
};

struct ArtParamInfo {
    ArtParam param;
    const char* key;   // stored name, never translated
    int minValue;
    int maxValue;
    int defaultValue;  // what MuseScore plays when nothing is set
};

const std::array<ArtParamInfo, size_t(ArtParam::Count)>& artParams();
const ArtParamInfo& artParamInfo(ArtParam p);

//! A set of values. Unset values mean "use the default".
struct ArticulationPlaybackValues {
    std::array<std::optional<int>, size_t(ArtParam::Count)> values;

    std::optional<int> get(ArtParam p) const { return values[size_t(p)]; }
    int valueOrDefault(ArtParam p) const;
    void set(ArtParam p, std::optional<int> v);   // clamps to the allowed range
    bool isEmpty() const;
    //! This set's values, with `base` filling the unset ones
    ArticulationPlaybackValues over(const ArticulationPlaybackValues& base) const;

    bool operator==(const ArticulationPlaybackValues& o) const { return values == o.values; }
};

struct ArticulationPlaybackSettings {
    static constexpr const char16_t* META_TAG = u"jubelleArticulationPlayback";

    ArticulationPlaybackValues scoreValues;
    std::map<uint64_t, ArticulationPlaybackValues> partValues;   // key: Part::id()

    bool isEmpty() const;
    ArticulationPlaybackValues effective(uint64_t partId) const;

    muse::String toText() const;
    static ArticulationPlaybackSettings fromText(const muse::String& text);

    //! Read from / write to the score's main (master) score
    static ArticulationPlaybackSettings read(const Score* score);
    //! Undoable; call inside startCmd/endCmd
    void write(Score* score) const;

    bool operator==(const ArticulationPlaybackSettings& o) const
    {
        return scoreValues == o.scoreValues && partValues == o.partValues;
    }
};

//! A copy of `base` with the values applied. Returns `base` itself when
//! nothing needs changing.
muse::mpe::ArticulationsProfilePtr applyArticulationPlayback(const muse::mpe::ArticulationsProfilePtr& base,
                                                             const ArticulationPlaybackValues& values);

// ---------------------------------------------------------------------------
// Level 3: passages (decided 14 Nov 2026)
//
// A passage override is a hidden (never printed) MuseScore text line on an
// instrument's staff, over the passage. Its text holds the values, e.g.
//   "Jubelle playback: staccato=25% accentLoudness=+15%"
// so it moves with the music, copies and pastes, undoes and saves like any
// other line. For each articulation separately, a note uses the passage
// covering it (the later-starting one where passages overlap), else the
// instrument value, else the global value, else MuseScore's.
// ---------------------------------------------------------------------------

constexpr const char16_t* PASSAGE_PREFIX = u"Jubelle playback:";

//! The line text for a passage with these values
muse::String passageText(const ArticulationPlaybackValues& values);
//! The values in a passage line's text; nullopt if it isn't one
std::optional<ArticulationPlaybackValues> parsePassageText(const muse::String& text);
//! A Jubelle passage line (or a segment of one)
const TextLine* passageLine(const EngravingItem* item);

struct PassageAt {
    const TextLine* line = nullptr;
    ArticulationPlaybackValues values;
};
//! The passages on this part covering `tick`, earliest-starting first
std::vector<PassageAt> passagesAt(const Score* score, const Part* part, int tick);
//! Passage values merged (later-starting wins per articulation)
ArticulationPlaybackValues mergedPassageValues(const std::vector<PassageAt>& passages);

//! Turn speed for a part at a tick (1.0 = normal). Cheap to call repeatedly.
double turnSpeedFactor(const Score* score, const Part* part, int tick);
double turnSpeedFactor(const Score* score, uint64_t partId);
}
