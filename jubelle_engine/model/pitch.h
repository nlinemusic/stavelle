/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 *
 * Pitch spelling: tonal pitch classes on the line of fifths (MuseScore's numbering, so files need no
 * conversion): C = 14, G = 15, D = 16 … F = 13, B♭ = 12 …; a sharp adds 7, a flat takes 7.
 */
#pragma once

#include <cstdint>

namespace je::model {
//! The plain spelling of a MIDI pitch in a key (fifths, -7 … 7): flats in flat keys, sharps otherwise
inline int64_t defaultTpc(int64_t pitch, int64_t key)
{
    static const int sharp[12] = { 14, 21, 16, 23, 18, 13, 20, 15, 22, 17, 24, 19 };
    static const int flat[12] = { 14, 9, 16, 11, 18, 13, 8, 15, 10, 17, 12, 19 };
    const int pc = int(((pitch % 12) + 12) % 12);
    return key < 0 ? flat[pc] : sharp[pc];
}

//! The pitch class (0 … 11) of a spelling
inline int pitchClassOfTpc(int64_t tpc)
{
    // C (14) is 0; each fifth is 7 semitones
    return int((((tpc - 14) * 7) % 12 + 12) % 12);
}

//! The diatonic steps of a transposition given only in semitones (a whole step down: -1; a major sixth down: -5)
inline int64_t diatonicOf(int64_t chromatic)
{
    const double d = double(chromatic) * 7.0 / 12.0;
    return int64_t(d < 0 ? d - 0.5 : d + 0.5);
}

//! The spelling moved by an interval (diatonic steps, semitones): along the line of fifths by 7·c − 12·d
inline int64_t transposeTpc(int64_t tpc, int64_t diatonic, int64_t chromatic)
{
    return tpc + 7 * chromatic - 12 * diatonic;
}
}
