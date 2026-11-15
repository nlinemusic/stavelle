/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "jazzfalls.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

#include "articulationplayback.h"

#include "dom/articulation.h"
#include "dom/chord.h"
#include "dom/chordline.h"
#include "dom/instrument.h"
#include "dom/masterscore.h"
#include "dom/note.h"
#include "dom/part.h"
#include "dom/tempo.h"
#include "types/symnames.h"

using namespace muse::mpe;

namespace mu::engraving::jubelle {
using muse::String;

// Lengths when nothing else applies (ms): close to what players do at a
// medium tempo; Short is a quick drop-off, Long a long, falling-away gesture
static constexpr double SHORT_MS = 150.0;
static constexpr double MEDIUM_MS = 300.0;
static constexpr double LONG_MS = 500.0;
static constexpr double EARLIEST_START = 0.30;   // a fall never starts before 30% of its note

FallShape fallShapeFor(const String& id, FallLength length, FallStyle style, int pitch, double bpm)
{
    FallShape shape;
    shape.lengthMs = length == FallLength::Short ? SHORT_MS : (length == FallLength::Long ? LONG_MS : MEDIUM_MS);

    const bool trombone = id.startsWith(u"brass.trombone") || id.startsWith(u"brass.sackbutt");
    const bool fingered = id.startsWith(u"wind.reed.saxophone") || id.startsWith(u"wind.reed.clarinet");
    if (trombone) {
        shape.lengthMs *= 1.25;   // a slide fall takes a little longer
    } else if (fingered) {
        shape.lengthMs *= 0.85;   // fingered falls are quicker
    }
    if (pitch >= 76) {
        shape.lengthMs *= 0.9;    // high notes fall off faster
    } else if (pitch <= 52) {
        shape.lengthMs *= 1.15;   // low notes take a little longer
    }
    if (bpm > 0) {
        shape.lengthMs *= std::clamp(std::pow(120.0 / bpm, 0.25), 0.85, 1.15);
    }

    // Saxophone and clarinet fingered falls, and rough falls on any instrument,
    // move by semitones; lip falls and the trombone's slide are smooth
    shape.stepped = style == FallStyle::Rough || (fingered && style != FallStyle::Lip);
    return shape;
}

PitchCurve retimeFall(const PitchCurve& curve, double noteMs, double fallMs, bool stepped)
{
    if (curve.size() < 2 || noteMs <= 0.0 || fallMs <= 0.0) {
        return curve;
    }

    // Where the movement starts: the last point at the starting pitch before the first change
    std::vector<std::pair<double, double> > points;
    for (auto it = curve.cbegin(); it != curve.cend(); ++it) {
        points.emplace_back(double(it->first) / HUNDRED_PERCENT, double(it->second));
    }
    const double startValue = points.front().second;
    size_t moveFrom = points.size();
    for (size_t i = 1; i < points.size(); ++i) {
        if (points[i].second != startValue) {
            moveFrom = i - 1;
            break;
        }
    }
    if (moveFrom >= points.size()) {
        return curve;   // nothing moves
    }
    const double f0 = points[moveFrom].first;
    if (f0 >= 1.0) {
        return curve;
    }

    const double newStart = std::clamp(1.0 - fallMs / noteMs, EARLIEST_START, 0.97);
    auto mapped = [&](double p) { return newStart + (p - f0) * (1.0 - newStart) / (1.0 - f0); };
    auto valueAt = [&](double p) {   // the original fall, linearly, at position p (original time)
        for (size_t i = moveFrom + 1; i < points.size(); ++i) {
            if (p <= points[i].first) {
                const double a = points[i - 1].first, b = points[i].first;
                const double t = b > a ? (p - a) / (b - a) : 1.0;
                return points[i - 1].second + t * (points[i].second - points[i - 1].second);
            }
        }
        return points.back().second;
    };
    auto key = [](double p) { return duration_percentage_t(std::lround(std::clamp(p, 0.0, 1.0) * HUNDRED_PERCENT)); };

    PitchCurve out;
    out[0] = pitch_level_t(std::lround(startValue));
    out[key(newStart)] = pitch_level_t(std::lround(startValue));

    if (!stepped) {
        for (size_t i = moveFrom + 1; i < points.size(); ++i) {
            out[key(mapped(points[i].first))] = pitch_level_t(std::lround(points[i].second));
        }
        return out;
    }

    // In semitone steps: hold each semitone, then move to the next one quickly
    const int SAMPLES = 200;
    const duration_percentage_t edge = ONE_PERCENT / 4;
    auto quantize = [&](double v) { return pitch_level_t(std::lround(v / PITCH_LEVEL_STEP) * PITCH_LEVEL_STEP); };
    pitch_level_t held = quantize(startValue);
    for (int s = 1; s <= SAMPLES; ++s) {
        const double p = f0 + (1.0 - f0) * s / SAMPLES;
        const pitch_level_t q = quantize(valueAt(p));
        if (q != held) {
            const duration_percentage_t k = key(mapped(p));
            out[std::max<duration_percentage_t>(key(newStart), k - edge)] = held;
            out[k] = q;
            held = q;
        }
    }
    out[HUNDRED_PERCENT] = held;
    return out;
}

// What the fall symbol says about its length and manner
static bool fallFromSymbols(const Chord* chord, FallLength& length, FallStyle& style)
{
    for (const Articulation* a : chord->articulations()) {
        const muse::AsciiStringView name = SymNames::nameForSymId(a->symId());
        const std::string n(name.ascii() ? name.ascii() : "");
        if (n.rfind("brassFall", 0) != 0 && n.rfind("brassDoit", 0) != 0 && n.rfind("brassLift", 0) != 0) {
            continue;
        }
        length = n.find("Short") != std::string::npos ? FallLength::Short
                 : (n.find("Long") != std::string::npos ? FallLength::Long : FallLength::Medium);
        style = n.find("Rough") != std::string::npos ? FallStyle::Rough
                : (n.find("Lip") != std::string::npos ? FallStyle::Lip : FallStyle::Smooth);
        return true;
    }
    return false;
}

// Your own fall start (score, instrument or passage) wins
static bool fallStartSetByYou(const Score* score, const Part* part, int tick)
{
    static std::mutex mutex;
    static String cachedText;
    static ArticulationPlaybackSettings cached;

    const String text = score->masterScore()->metaTag(String(ArticulationPlaybackSettings::META_TAG));
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (text != cachedText) {
            cached = ArticulationPlaybackSettings::fromText(text);
            cachedText = text;
        }
        if (cached.effective(part->id().toUint64()).get(ArtParam::FallStart).has_value()) {
            return true;
        }
    }
    const std::vector<PassageAt> passages = passagesAt(score, part, tick);
    return !passages.empty() && mergedPassageValues(passages).get(ArtParam::FallStart).has_value();
}

muse::mpe::NoteEvent applyJazzFall(const Note* note, muse::mpe::NoteEvent event)
{
    const ArticulationMap& arts = event.expressionCtx().articulations;
    const bool quick = arts.contains(ArticulationType::QuickFall);
    if (!quick && !arts.contains(ArticulationType::Fall) && !arts.contains(ArticulationType::Doit)) {
        return event;
    }
    const Chord* chord = note ? note->chord() : nullptr;
    const Part* part = chord ? chord->part() : nullptr;
    const Score* score = chord ? chord->score() : nullptr;
    if (!part || !score || !score->masterScore() || event.pitchCtx().pitchCurve.size() < 2) {
        return event;
    }
    if (fallStartSetByYou(score, part, chord->tick().ticks())) {
        return event;
    }

    // MuseScore plays every lip and rough fall as a "quick fall", whatever its
    // length; the symbol says how long it really is. Without one, quick = short.
    FallLength length = quick ? FallLength::Short : FallLength::Medium;
    FallStyle style = FallStyle::Smooth;
    fallFromSymbols(chord, length, style);

    const Instrument* instrument = part->instrument(chord->tick());
    const double bpm = score->tempomap()->tempo(chord->tick().ticks()).val * 60.0;
    const FallShape shape = fallShapeFor(instrument ? instrument->musicXmlId() : String(), length, style, note->ppitch(), bpm);

    const double noteMs = double(event.arrangementCtx().actualDuration) / 1000.0;
    PitchContext pitch = event.pitchCtx();
    pitch.pitchCurve = retimeFall(pitch.pitchCurve, noteMs, shape.lengthMs, shape.stepped);
    ArrangementContext arrangement = event.arrangementCtx();
    ExpressionContext expression = event.expressionCtx();
    return muse::mpe::NoteEvent(std::move(arrangement), std::move(pitch), std::move(expression));
}
}
