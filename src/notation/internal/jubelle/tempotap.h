/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G6 (Phase 6.4, approved): Live Tempo Recording, tapping only.
//
// You tap (a key, the Tap button, or a MIDI note or pedal) on the beats you
// choose while the score plays. The time between taps becomes the tempo of
// that beat; light smoothing keeps real rubato but ignores tiny jitter.
//
// Storage (approved): hidden MuseScore tempo markings, one per point that
// changes the tempo, so MuseScore 4.7.7 plays the same tempo and nothing is
// printed. Jubelle recognises its own points (their ids are listed in the
// score's metaTag "jubelleTempoPoints"), so re-recording a section replaces
// only its own points there; your printed tempo markings are never touched,
// and the tempo after the section is restored so it joins smoothly.

#pragma once

#include <vector>

#include "global/types/string.h"
#include "engraving/types/fraction.h"

namespace mu::engraving {
class Score;
class TempoText;
}

namespace mu::notation::jubelle {
struct TapSettings {
    engraving::Fraction beat = engraving::Fraction(1, 4);   // what each tap stands for (written note value)
    double smoothing = 0.3;     // 0 = every tap as played, towards 1 = smoother
    double jitterPercent = 2.0; // tempo changes smaller than this don't make a new point
    double minBpm = 20.0;       // taps outside these tempos are ignored as mistakes
    double maxBpm = 400.0;
};

struct TempoPoint {
    engraving::Fraction tick;
    double bpm = 120.0;         // quarter notes per minute
};

//! Turns tap times (seconds, in order) into tempo points. The first tap is
//! on `startTick`; each further tap is one `beat` later in the score.
std::vector<TempoPoint> tapsToTempo(const std::vector<double>& tapSecs, const engraving::Fraction& startTick, const TapSettings& s);

//! The tempo of the last few taps, for follow-me playback (0 if not enough taps)
double liveTempo(const std::vector<double>& tapSecs, const TapSettings& s);

constexpr const char16_t* TEMPO_POINTS_TAG = u"jubelleTempoPoints";

//! Jubelle's own recorded points in the score, in order
std::vector<engraving::TempoText*> recordedTempoPoints(const engraving::Score* score);
bool isRecordedTempoPoint(const engraving::Score* score, const engraving::TempoText* t);

//! Replaces Jubelle's own points in [from, to) with `points` (hidden tempo
//! markings), and restores the tempo in effect at `to` so the rest of the
//! score plays as before. Undoable: call inside a command.
void applyTempoPoints(engraving::Score* score, const std::vector<TempoPoint>& points, const engraving::Fraction& from,
                      const engraving::Fraction& to);

//! Edits one recorded point (undoable)
void setRecordedPointTempo(engraving::Score* score, engraving::TempoText* point, double bpm);
//! Removes recorded points in [from, to) (undoable)
void clearRecordedTempo(engraving::Score* score, const engraving::Fraction& from, const engraving::Fraction& to);
}
