/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore).
 *
 * (Test copy: the previous, instant-attack limiter, kept only to compare against.)
 * A safety limiter for the master output. It does nothing at all while the
 * music stays below the threshold (about -1 dBFS): the samples pass through
 * unchanged. Only when a peak would go past the threshold does it turn the
 * whole output down just enough, then let it come back up smoothly. Its job
 * is to stop loud tutti passages from clipping, not to change the sound.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace jubelle_test_old {
class JubelleOldSafetyLimiter
{
public:
    static constexpr float THRESHOLD = 0.891251f; // -1 dBFS
    static constexpr float RELEASE_MS = 250.f;

    void setSampleRate(unsigned int sampleRate)
    {
        const double releaseSamples = std::max(1.0, double(sampleRate) * RELEASE_MS / 1000.0);
        // one time constant per release period
        m_releaseCoef = 1.0 - std::exp(-1.0 / releaseSamples);
    }

    bool isLimiting() const { return m_gain < 1.0; }
    float currentGain() const { return float(m_gain); }

    void reset() { m_gain = 1.0; }

    // buffer is interleaved: frames * channels
    void process(float* buffer, std::size_t frames, std::size_t channels)
    {
        if (!buffer || channels == 0) {
            return;
        }

        for (std::size_t f = 0; f < frames; ++f) {
            float* frame = buffer + f * channels;
            float peak = 0.f;
            for (std::size_t c = 0; c < channels; ++c) {
                peak = std::max(peak, std::fabs(frame[c]));
            }

            const double target = peak > THRESHOLD ? double(THRESHOLD) / peak : 1.0;
            if (target < m_gain) {
                m_gain = target; // instant: never let a peak through
            } else if (m_gain < 1.0) {
                // (double precision: in float the recovery would stall just short of 1)
                m_gain += (1.0 - m_gain) * m_releaseCoef;
                if (m_gain > 0.99999) {
                    m_gain = 1.0;
                }
                m_gain = std::min(m_gain, target);
            }

            if (m_gain < 1.0) {
                const float g = float(m_gain);
                for (std::size_t c = 0; c < channels; ++c) {
                    frame[c] *= g;
                }
            }
        }
    }

private:
    double m_gain = 1.0;
    double m_releaseCoef = 1.0 - std::exp(-1.0 / (44100.0 * RELEASE_MS / 1000.0));
};
}
