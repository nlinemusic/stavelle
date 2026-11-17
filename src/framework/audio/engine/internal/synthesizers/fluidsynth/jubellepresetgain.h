/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore).
 *
 * Even instrument levels for the built-in soundfont (decision P1, 15 Nov 2026).
 * MS Basic's presets were made at very different loudness: at the same
 * dynamic, a trombone came out about 14 dB louder than a jazz guitar. The
 * table (measured by tools/jubelle_gain) brings every preset to about
 * -20 dBFS per instrument at mf; dynamics, velocity and the mixer are then
 * applied on top exactly as before.
 *
 * Only MS Basic is corrected: the table means nothing for other soundfonts.
 */
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>

#include "jubellepresetgains.h"

namespace muse::audio::jubelle {
// + = louder, in centibels (10 cB = 1 dB). 0 when the preset isn't in the table.
inline int presetCorrectionCentibels(int bank, int program)
{
    for (const PresetGain& g : MS_BASIC_PRESET_GAINS) {
        if (g.bank == bank && g.program == program) {
            return g.correctionCentibels;
        }
    }
    return 0;
}

// Ensemble trim (P1 headroom, approved 15 Nov 2026): the more instruments a
// score has, the more the whole band is turned down, as a mixing engineer
// would set the faders. Up to 4 instruments: unchanged. Beyond that
// -15*log10(n/4) dB, at most -15 dB (big band, 17: about -9.4 dB).
// Measured: a 17-piece big band tutti peaked at +5 dBFS (mf) and +16 (fff)
// without it; with it about -4 and +7, so the safety limiter only has real
// work at the loudest tuttis.
inline double ensembleTrimDb(int instruments)
{
    if (instruments <= 4) {
        return 0.0;
    }
    return std::max(-15.0, -15.0 * std::log10(double(instruments) / 4.0));
}

// How many instruments are playing with the built-in sounds right now
// (one FluidSynth per instrument)
inline std::atomic<int>& builtInSoundInstruments()
{
    static std::atomic<int> count { 0 };
    return count;
}

// True when the file is the built-in soundfont (MS Basic.sf3 / .sf2)
inline bool isBuiltInSoundFont(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    for (char& c : name) {
        if (c >= 'A' && c <= 'Z') {
            c = char(c - 'A' + 'a');
        }
    }
    return name == "ms basic.sf3" || name == "ms basic.sf2";
}
}
