/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore).
 *
 * Dither for 16-bit exports. Rounding to 16 bits turns the quietest sounds
 * (reverb tails, fades, pp endings) into a grainy, distorted noise that moves
 * with the music. A tiny constant noise of about one 16-bit step (TPDF, the
 * standard choice) added before rounding replaces that distortion with a
 * steady hiss at about -96 dBFS, far below anything audible in music, so the
 * quiet sounds stay clean. 24-bit exports don't need it and don't get it.
 *
 * The noise is the same on every export (fixed seed), so exporting a score
 * twice gives identical files.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace muse::audio::dsp {
class JubelleTpdfDither
{
public:
    //! The float sample as a 16-bit integer, dithered
    int32_t toInt16(float value)
    {
        constexpr double MAX = 32767.0;
        const double scaled = double(std::clamp(value, -1.0f, 1.0f)) * MAX + (uniform() - uniform());
        return int32_t(std::clamp(std::lround(scaled), -32768L, 32767L));
    }

    void reset() { m_state = SEED; }

private:
    static constexpr uint32_t SEED = 0x4A75626Cu;   // fixed: repeatable exports

    //! uniform in [0, 1): a small, fast generator (xorshift32) is plenty for dither
    double uniform()
    {
        m_state ^= m_state << 13;
        m_state ^= m_state >> 17;
        m_state ^= m_state << 5;
        return double(m_state) / 4294967296.0;
    }

    uint32_t m_state = SEED;
};
}
