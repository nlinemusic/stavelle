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
