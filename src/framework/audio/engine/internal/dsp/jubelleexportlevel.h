/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle: the level of exported audio (25 Nov 2026).
 *
 * Playback keeps the safety limiter as it is (jubellesafetylimiter.h). An export can look at the whole piece first,
 * so it does better: the piece is rendered once, its loudest peak measured before any limiting, and the whole piece
 * is turned down just enough that the limiter has at most EXPORT_MAX_LIMITING_DB of work on the loudest peaks.
 * Loud ensembles keep their dynamics (the limiter used to turn tuttis down by up to 14 dB, which flattens them),
 * nothing clips, and music that never reaches the limiter is exported exactly as before. The limiter's
 * look-ahead delay is taken out, so an export starts exactly at 0 (as MuseScore's does), not 5 ms late.
 *
 * Plain C++, no dependencies, so it can be tested on its own.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "jubellesafetylimiter.h"

namespace muse::audio::dsp {
//! The most the limiter turns the loudest peaks of an export down by
constexpr float EXPORT_MAX_LIMITING_DB = 3.f;

//! The gain for the whole export, given its loudest sample before limiting
inline float exportGain(float preLimiterPeak)
{
    const float ceiling = JubelleSafetyLimiter::THRESHOLD * std::pow(10.f, EXPORT_MAX_LIMITING_DB / 20.f);
    if (!(preLimiterPeak > ceiling)) {
        return 1.f;   // within reach of the limiter's light touch: as before
    }
    return ceiling / preLimiterPeak;
}

//! The second step of an export: the gain, then the limiter, with its delay taken out
class JubelleExportLevel
{
public:
    JubelleExportLevel(unsigned int sampleRate, std::size_t channels, float gain)
        : m_channels(std::max<std::size_t>(1, channels)), m_gain(gain)
    {
        m_limiter.setSampleRate(sampleRate);
        m_skip = m_limiter.latencySamples();
        m_tail = m_skip;
    }

    //! Turns `frames` frames of `buffer` (interleaved) into output in place; returns how many frames of output it
    //! holds at the start of `buffer` (fewer at the very start, while the delay is taken out)
    std::size_t process(float* buffer, std::size_t frames)
    {
        const std::size_t n = frames * m_channels;
        if (m_gain != 1.f) {
            for (std::size_t i = 0; i < n; ++i) {
                buffer[i] *= m_gain;
            }
        }
        m_limiter.process(buffer, frames, m_channels);
        const std::size_t drop = std::min(m_skip, frames);
        m_skip -= drop;
        if (drop > 0) {
            std::copy(buffer + drop * m_channels, buffer + n, buffer);
        }
        return frames - drop;
    }

    //! The last frames, still in the limiter's delay at the end: up to `maxFrames` into `buffer`; returns how many
    std::size_t flush(float* buffer, std::size_t maxFrames)
    {
        const std::size_t frames = std::min(m_tail, maxFrames);
        std::fill(buffer, buffer + frames * m_channels, 0.f);
        const std::size_t out = process(buffer, frames);
        m_tail -= frames;
        return out;
    }

    bool finished() const { return m_tail == 0; }
    float gain() const { return m_gain; }

private:
    JubelleSafetyLimiter m_limiter;
    std::size_t m_channels = 2;
    float m_gain = 1.f;
    std::size_t m_skip = 0;   // output frames still to drop (the limiter's delay)
    std::size_t m_tail = 0;   // input frames of silence still to feed at the end
};
}
