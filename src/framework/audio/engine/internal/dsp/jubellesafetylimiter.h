/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore).
 *
 * A safety limiter for the master output. Its job is to stop loud tutti
 * passages from clipping, not to change the sound:
 *  - below the threshold (about -1 dBFS) the music passes through unchanged
 *    (only delayed by the look-ahead, 5 ms);
 *  - it looks 5 ms ahead, so when a peak is coming it turns the whole output
 *    down gradually over those 5 ms, reaching just the needed amount exactly
 *    when the peak arrives. The earlier version turned the level down within
 *    a single sample, which is heard as a click or crackle on loud attacks;
 *  - afterwards it comes back up smoothly (200 ms);
 *  - every channel is turned down together, so the stereo image stays.
 * No sample ever leaves above the threshold.
 *
 * (Gain computer: the gain each sample needs, its minimum over the look-ahead
 * window, a release that only ever comes back up slowly, then an average over
 * the window; the average of values that are each at most the gain a sample
 * needs is itself at most that gain, so the peak is always covered.)
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace muse::audio::dsp {
class JubelleSafetyLimiter
{
public:
    static constexpr float THRESHOLD = 0.891251f; // -1 dBFS
    static constexpr float RELEASE_MS = 200.f;
    static constexpr float LOOKAHEAD_MS = 5.f;
    static constexpr std::size_t MAX_CHANNELS = 8;

    JubelleSafetyLimiter() { setSampleRate(44100); }

    //! Allocates; call outside the audio callback (Mixer::setOutputSpec)
    void setSampleRate(unsigned int sampleRate)
    {
        const double releaseSamples = std::max(1.0, double(sampleRate) * RELEASE_MS / 1000.0);
        m_releaseCoef = 1.0 - std::exp(-1.0 / releaseSamples);
        m_window = std::max<std::size_t>(1, std::size_t(std::lround(double(sampleRate) * LOOKAHEAD_MS / 1000.0)));
        m_delay.assign(m_window * MAX_CHANNELS, 0.f);
        m_box.assign(m_window, 1.0);
        m_minIdx.assign(m_window + 1, 0);
        m_minVal.assign(m_window + 1, 1.0);
        reset();
    }

    //! How many samples the output is behind the input
    std::size_t latencySamples() const { return m_window - 1; }

    bool isLimiting() const { return m_gain < 1.0; }
    float currentGain() const { return float(m_gain); }

    void reset()
    {
        std::fill(m_delay.begin(), m_delay.end(), 0.f);
        std::fill(m_box.begin(), m_box.end(), 1.0);
        m_boxSum = double(m_window);
        m_minHead = m_minTail = 0;
        m_env = 1.0;
        m_gain = 1.0;
        m_pos = 0;
        m_n = 0;
    }

    // buffer is interleaved: frames * channels
    void process(float* buffer, std::size_t frames, std::size_t channels)
    {
        if (!buffer || channels == 0 || m_window == 0) {
            return;
        }
        if (channels > MAX_CHANNELS) {
            channels = MAX_CHANNELS;   // (never happens: the mixer is stereo)
        }

        for (std::size_t f = 0; f < frames; ++f) {
            float* frame = buffer + f * channels;

            // the gain this sample needs
            float peak = 0.f;
            for (std::size_t c = 0; c < channels; ++c) {
                peak = std::max(peak, std::fabs(frame[c]));
            }
            const double need = peak > THRESHOLD ? double(THRESHOLD) / peak : 1.0;

            // its minimum over the look-ahead window (monotonic queue)
            pushMin(m_n, need);
            while (m_minIdx[m_minHead] + m_window <= m_n) {
                m_minHead = (m_minHead + 1) % m_minIdx.size();
            }
            const double windowMin = m_minVal[m_minHead];

            // release: comes back up slowly, goes down at once (the average below smooths it)
            m_env = std::min(windowMin, m_env + (1.0 - m_env) * m_releaseCoef);
            if (m_env > 0.99999 && windowMin >= 1.0) {
                m_env = 1.0;
            }

            // average over the window: the gradual turn-down
            m_boxSum += m_env - m_box[m_pos];
            m_box[m_pos] = m_env;
            m_gain = std::min(1.0, m_boxSum / double(m_window));
            if (m_gain > 0.999999) {
                m_gain = 1.0;                         // (rounding in the running sum)
                if (m_env >= 1.0 && m_n % m_window == 0) {
                    m_boxSum = double(m_window);      // and no drift over hours
                }
            }

            // delay line: write this frame, read the one from the start of the window
            float* slot = &m_delay[m_pos * MAX_CHANNELS];
            const std::size_t readPos = (m_pos + 1) % m_window;
            const float* out = &m_delay[readPos * MAX_CHANNELS];
            float delayed[MAX_CHANNELS];
            float delayedPeak = 0.f;
            for (std::size_t c = 0; c < channels; ++c) {
                delayed[c] = (m_window == 1) ? frame[c] : out[c];
                delayedPeak = std::max(delayedPeak, std::fabs(delayed[c]));
            }
            for (std::size_t c = 0; c < channels; ++c) {
                slot[c] = frame[c];
            }

            double g = m_gain;
            if (delayedPeak * g > THRESHOLD) {
                g = THRESHOLD / delayedPeak;   // (rounding only: never let a peak through)
            }
            for (std::size_t c = 0; c < channels; ++c) {
                frame[c] = g < 1.0 ? float(delayed[c] * g) : delayed[c];
            }

            m_pos = (m_pos + 1) % m_window;
            ++m_n;
        }
    }

private:
    void pushMin(std::size_t index, double value)
    {
        const std::size_t cap = m_minIdx.size();
        while (m_minTail != m_minHead) {
            const std::size_t last = (m_minTail + cap - 1) % cap;
            if (m_minVal[last] < value) {
                break;
            }
            m_minTail = last;
        }
        m_minIdx[m_minTail] = index;
        m_minVal[m_minTail] = value;
        m_minTail = (m_minTail + 1) % cap;
    }

    std::size_t m_window = 1;
    double m_releaseCoef = 0.0;
    std::vector<float> m_delay;
    std::vector<double> m_box;
    double m_boxSum = 1.0;
    std::vector<std::size_t> m_minIdx;
    std::vector<double> m_minVal;
    std::size_t m_minHead = 0;
    std::size_t m_minTail = 0;
    double m_env = 1.0;
    double m_gain = 1.0;
    std::size_t m_pos = 0;
    std::size_t m_n = 0;
};
}
