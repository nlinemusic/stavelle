/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 *
 * Names shared by editing and files: articulations (the model names them plainly, "accent", or by MuseScore's
 * symbol, "articAccentAbove", as read from a file), and the diatonic spelling of pitches in a key.
 */
#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace je::model {
//! MuseScore's symbol for an articulation the model names plainly ("accent"); MuseScore's own names pass through
inline std::string articulationSymbol(const std::string& name)
{
    static const std::map<std::string, std::string> symbols {
        { "accent", "articAccentAbove" }, { "staccato", "articStaccatoAbove" }, { "staccatissimo", "articStaccatissimoAbove" },
        { "tenuto", "articTenutoAbove" }, { "marcato", "articMarcatoAbove" }, { "portato", "articTenutoStaccatoAbove" },
        { "accentStaccato", "articAccentStaccatoAbove" }, { "marcatoStaccato", "articMarcatoStaccatoAbove" },
        { "tenutoAccent", "articTenutoAccentAbove" }, { "stress", "articStressAbove" }, { "unstress", "articUnstressAbove" },
        { "upBow", "stringsUpBow" }, { "downBow", "stringsDownBow" }, { "harmonic", "stringsHarmonic" },
        { "openString", "brassMuteOpen" }, { "stopped", "brassMuteClosed" }, { "snapPizzicato", "pluckedSnapPizzicatoAbove" },
        { "trill", "ornamentTrill" }, { "mordent", "ornamentMordent" }, { "shortTrill", "ornamentShortTrill" },
        { "turn", "ornamentTurn" }, { "invertedTurn", "ornamentTurnInverted" },
    };
    auto it = symbols.find(name);
    return it == symbols.end() ? name : it->second;
}

//! The same articulation whichever way it is named, placement aside (an accent above or below is one accent)
inline std::string articulationKind(const std::string& name)
{
    std::string s = articulationSymbol(name);
    for (const char* end : { "Above", "Below" }) {
        const std::string e(end);
        if (s.size() > e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0) {
            s.resize(s.size() - e.size());
        }
    }
    return s;
}

inline bool isOrnamentSymbol(const std::string& symbol)
{
    return symbol.rfind("ornament", 0) == 0;
}

//! The letter of a spelling: C = 0, D = 1 … B = 6
inline int letterOfTpc(int64_t tpc)
{
    // F C G D A E B along the line of fifths (F = 13)
    static const int letters[7] = { 3, 0, 4, 1, 5, 2, 6 };
    return letters[((tpc - 13) % 7 + 7) % 7];
}

//! The accidental of a spelling: −2 … 2 (flats negative)
inline int accidentalOfTpc(int64_t tpc)
{
    // naturals are 13 (F) … 19 (B); each 7 further is one more sharp, each 7 less one more flat
    const int64_t d = tpc - 13;
    return int(d >= 0 ? d / 7 : -((-d + 6) / 7));
}

//! The spelling of a letter with an accidental
inline int64_t tpcOf(int letter, int accidental)
{
    static const int naturals[7] = { 14, 16, 18, 13, 15, 17, 19 };   // C D E F G A B
    return naturals[((letter % 7) + 7) % 7] + 7 * accidental;
}

//! The accidental a key gives a letter (fifths −7 … 7)
inline int keyAccidental(int letter, int64_t key)
{
    // sharps in the order F C G D A E B, flats B E A D G C F
    static const int sharpOrder[7] = { 3, 0, 4, 1, 5, 2, 6 };
    static const int flatOrder[7] = { 6, 2, 5, 1, 4, 0, 3 };
    for (int i = 0; i < 7; ++i) {
        if (key > i && sharpOrder[i] == letter) {
            return 1;
        }
        if (key < -i && flatOrder[i] == letter) {
            return -1;
        }
    }
    return 0;
}
}
