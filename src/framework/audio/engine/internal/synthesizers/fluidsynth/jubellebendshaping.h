/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle: more natural falls, doits and other pitch gestures with the
// built-in (FluidSynth) sounds. Playback block, approved 15 Nov 2026.
//
// Two problems found in MuseScore's handling of a note's pitch curve:
//  1. The pitch bend was reset at the exact moment the note ended, while the
//     note's release was still sounding, so the end of every fall jumped back
//     up to the written pitch ("snap back"). The reset now waits for the
//     release (RELEASE_US), or happens just before the next note on that
//     channel starts, whichever is sooner.
//  2. A falling note kept full loudness to the end. For gestures that leave the
//     note (falls, doits, slides out) the channel volume (controller 7, unused
//     otherwise by this synthesizer) now follows the gesture down, to about
//     -16 dB at the end, and is restored with the pitch.
//
//  3. The pitch path was straight lines between the pattern's points (every
//     10% of the note), so a fall changed speed abruptly at each point, which
//     sounds mechanical. It is now a smooth
//     monotone curve through the same points (no overshoot, so glissandos and
//     bends still end exactly where they should).
//
// Header-only and free of audio-engine dependencies, so it is tested on its own
// (tools/jubelle_engine_tests).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

#include "midi/midievent.h"

namespace muse::audio::jubelle {
inline constexpr int64_t RELEASE_US = 350000;          // 0.35 s: a typical horn release
inline constexpr uint32_t PITCH_BEND_CENTER = 8192;
inline constexpr uint8_t CHANNEL_VOLUME_CC = 7;
inline constexpr uint32_t FULL_VOLUME = 127;
inline constexpr double FADE_DEPTH = 0.6;             // volume at the end of a fall: 40% (about -16 dB)

struct PendingReset {
    int64_t at = 0;                // when the note ends
    midi::channel_t channel = 0;
    bool volume = false;           // the gesture also faded the channel volume
};

//! Channel volume for a point of a gesture: progress 0 (at the written pitch) to 1 (fully bent)
inline uint32_t fadedVolume(double progress)
{
    progress = std::clamp(progress, 0.0, 1.0);
    const double v = FULL_VOLUME * (1.0 - FADE_DEPTH * std::pow(progress, 1.2));
    return static_cast<uint32_t>(std::lround(v));
}

inline midi::Event pitchBendEvent(midi::channel_t channel, uint32_t value)
{
    midi::Event e(midi::Event::Opcode::PitchBend, midi::Event::MessageType::ChannelVoice10);
    e.setChannel(channel);
    e.setData(value);
    return e;
}

inline midi::Event volumeEvent(midi::channel_t channel, uint32_t value)
{
    midi::Event e(midi::Event::Opcode::ControlChange, midi::Event::MessageType::ChannelVoice10);
    e.setChannel(channel);
    e.setIndex(CHANNEL_VOLUME_CC);
    e.setData(value);
    return e;
}

struct CurvePoint {
    double time = 0.0;
    double value = 0.0;
};

//! Monotone cubic (Fritsch-Carlson) samples through the points: for each
//! segment, max(minSamples, |change| / valueStep) samples, from its start;
//! the last point is included.
inline std::vector<CurvePoint> smoothCurve(const std::vector<CurvePoint>& pts, size_t minSamples, double valueStep)
{
    std::vector<CurvePoint> out;
    const size_t n = pts.size();
    if (n < 2) {
        return pts;
    }
    std::vector<double> h(n - 1), delta(n - 1), m(n, 0.0);
    for (size_t i = 0; i + 1 < n; ++i) {
        h[i] = std::max(pts[i + 1].time - pts[i].time, 1e-9);
        delta[i] = (pts[i + 1].value - pts[i].value) / h[i];
    }
    m[0] = delta[0];
    m[n - 1] = delta[n - 2];
    for (size_t i = 1; i + 1 < n; ++i) {
        m[i] = (delta[i - 1] * delta[i] <= 0) ? 0.0 : (delta[i - 1] + delta[i]) / 2.0;
    }
    for (size_t i = 0; i + 1 < n; ++i) {
        if (delta[i] == 0.0) {
            m[i] = m[i + 1] = 0.0;
            continue;
        }
        const double a = m[i] / delta[i];
        const double b = m[i + 1] / delta[i];
        const double s2 = a * a + b * b;
        if (s2 > 9.0) {
            const double t = 3.0 / std::sqrt(s2);
            m[i] = t * a * delta[i];
            m[i + 1] = t * b * delta[i];
        }
    }
    for (size_t i = 0; i + 1 < n; ++i) {
        const double change = std::abs(pts[i + 1].value - pts[i].value);
        const size_t samples = std::max(minSamples, static_cast<size_t>(valueStep > 0 ? change / valueStep : 0));
        for (size_t k = 0; k < samples; ++k) {
            const double t = double(k) / double(samples);
            const double t2 = t * t, t3 = t2 * t;
            const double v = (2 * t3 - 3 * t2 + 1) * pts[i].value + (t3 - 2 * t2 + t) * h[i] * m[i]
                             + (-2 * t3 + 3 * t2) * pts[i + 1].value + (t3 - t2) * h[i] * m[i + 1];
            out.push_back({ pts[i].time + t * h[i], v });
        }
    }
    out.push_back(pts.back());
    return out;
}

//! Puts each pending reset after the note's release, or just before the next
//! note-on on its channel if that comes sooner (so the next note never starts bent).
template<typename Key>
void placeResets(std::map<Key, std::vector<midi::Event> >& events, const std::vector<PendingReset>& resets,
                 int64_t releaseUs = RELEASE_US)
{
    for (const PendingReset& r : resets) {
        Key when = static_cast<Key>(r.at + releaseUs);
        bool beforeNoteOn = false;
        for (auto it = events.lower_bound(static_cast<Key>(r.at)); it != events.end() && it->first <= when; ++it) {
            const bool noteOnHere = std::any_of(it->second.begin(), it->second.end(), [&](const midi::Event& e) {
                return e.opcode() == midi::Event::Opcode::NoteOn && e.channel() == r.channel;
            });
            if (noteOnHere) {
                when = it->first;
                beforeNoteOn = true;
                break;
            }
        }
        std::vector<midi::Event> reset { pitchBendEvent(r.channel, PITCH_BEND_CENTER) };
        if (r.volume) {
            reset.push_back(volumeEvent(r.channel, FULL_VOLUME));
        }
        std::vector<midi::Event>& slot = events[when];
        slot.insert(beforeNoteOn ? slot.begin() : slot.end(), reset.begin(), reset.end());
    }
}
}
