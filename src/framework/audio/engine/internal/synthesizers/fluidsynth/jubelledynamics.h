/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle (17 Nov 2026): how loud each dynamic marking plays with soundfonts.
//
// MuseScore turns the dynamics ppp..fff into MIDI levels 16, 32, ... 127 in
// equal steps. A soundfont synth turns a MIDI level into loudness on a curve
// (40*log10(level/127) dB, the SoundFont 2 standard), so those equal steps are
// not equal to the ear: mf to f was only +3.2 dB, f to ff +2.7, ff to fff +2.2,
// while pp to ppp was -12. Loud markings barely differed.
//
// Here the loudness of each marking is chosen in decibels first, relative to
// mf, and then turned into the MIDI level that gives it:
//
//      ppp   pp     p    mp   mf   f    ff   fff
//      -22  -15.5  -10   -5    0   +5   +9   +12   dB (before the instrument's response)
//
// mf itself, and music with no dynamic at all, sound exactly as loud as
// before (a make-up gain compensates for mf's lower MIDI level), so the
// calibrated instrument levels stay valid.
//
// Instruments respond differently: brass and percussion have a wide dynamic
// range, flutes a narrow one. The steps are scaled by family.
//
// Plain C++, no dependencies, so it can be tested on its own.

#pragma once

#include <algorithm>
#include <cmath>
#include <string>

namespace muse::audio::jubelle {
enum class DynamicsFamily {
    Generic,
    Brass,
    Reeds,       // saxophones, clarinets, oboes, bassoons
    Flutes,      // flutes, piccolo, recorders, whistles
    Strings,
    Keyboards,
    Percussion,
    Voices,
};

//! dB relative to mf at ppp, pp, p, mp, mf, f, ff, fff
constexpr double DYNAMIC_DB[8] = { -22.0, -15.5, -10.0, -5.0, 0.0, 5.0, 9.0, 12.0 };
//! music with no dynamic marking: as loud as it was in MuseScore (3.9 dB under mf)
constexpr double NATURAL_DB = -3.9;
//! the MIDI level of mf (MuseScore: 80). Lower, so there is room above it.
constexpr int MF_MIDI_LEVEL = 64;
//! what mf lost by moving from 80 to 64 on the SoundFont curve, given back as gain
inline double makeupGain()
{
    return std::pow(10.0, (40.0 * std::log10(80.0 / double(MF_MIDI_LEVEL))) / 20.0);   // +3.9 dB
}

inline double familySpan(DynamicsFamily f)
{
    switch (f) {
    case DynamicsFamily::Brass: return 1.15;
    case DynamicsFamily::Percussion: return 1.10;
    case DynamicsFamily::Reeds: return 0.95;
    case DynamicsFamily::Flutes: return 0.85;
    case DynamicsFamily::Voices: return 0.90;
    case DynamicsFamily::Strings:
    case DynamicsFamily::Keyboards:
    case DynamicsFamily::Generic:
        break;
    }
    return 1.0;
}

//! `category`: 0 keyboards, 1 strings, 2 winds, 3 percussion, 4 voices (MuseScore's
//! sound categories); `id`: the instrument's sound id in lower case ("trumpet", ...)
inline DynamicsFamily familyFor(int category, const std::string& id)
{
    auto has = [&id](const char* s) { return id.find(s) != std::string::npos; };
    switch (category) {
    case 0: return DynamicsFamily::Keyboards;
    case 1: return DynamicsFamily::Strings;
    case 3: return DynamicsFamily::Percussion;
    case 4: return DynamicsFamily::Voices;
    case 2:
        if (has("flute") || has("piccolo") || has("recorder") || has("whistle") || has("fife") || has("ocarina")) {
            return DynamicsFamily::Flutes;
        }
        if (has("sax") || has("clarinet") || has("oboe") || has("bassoon") || has("horn_english") || has("english")
            || has("heckelphone") || has("harmonica") || has("accordion")) {
            return DynamicsFamily::Reeds;
        }
        return DynamicsFamily::Brass;   // trumpets, horns, trombones, tubas, bugles…
    default:
        break;
    }
    return DynamicsFamily::Generic;
}

//! dB relative to mf for a position on the dynamics scale: 0 = ppp, 1 = pp, … 7 = fff
//! (fractions are positions inside a crescendo or diminuendo)
inline double dynamicDb(double steps, DynamicsFamily family)
{
    steps = std::clamp(steps, 0.0, 7.0);
    const int lower = std::min(6, int(std::floor(steps)));
    const double frac = steps - lower;
    const double db = DYNAMIC_DB[lower] + (DYNAMIC_DB[lower + 1] - DYNAMIC_DB[lower]) * frac;
    return db * familySpan(family);
}

//! the MIDI level (1..127) that sounds `db` decibels from mf on the SoundFont curve
inline int midiLevelForDb(double db)
{
    const double level = double(MF_MIDI_LEVEL) * std::pow(10.0, db / 40.0);
    return std::clamp(int(std::lround(level)), 1, 127);
}

inline int midiLevel(double steps, DynamicsFamily family)
{
    return midiLevelForDb(dynamicDb(steps, family));
}

inline int naturalMidiLevel()
{
    return midiLevelForDb(NATURAL_DB);
}

// Instruments whose loudness comes from the note velocity (piano and other
// keyboards, guitars, harp, mallets, drums: MuseScore's "no single-note
// dynamics" instruments). In a soundfont the velocity does more than set the
// level: it chooses the sample layer and opens the brightness filter. Played
// at the lower MIDI levels of the scale above, with the loss made up by gain,
// these instruments sounded duller than in MuseScore (measured with MS Basic
// piano: a six-note chord at mf had 6 dB less energy above 4 kHz). So their
// velocities follow MuseScore's own scale exactly (16 per marking; unmarked
// music at MuseScore's 64), and the make-up gain is taken back off their
// channels (velocityInstrumentAttenuationCb), so they sound and balance exactly
// as in MuseScore.

//! MuseScore's MIDI level for a position on the dynamics scale (0 = ppp … 7 = fff)
inline int museScoreLevel(double steps)
{
    return std::clamp(int(std::lround(16.0 + steps * 16.0)), 16, 127);
}

//! the make-up gain in centibels, as channel attenuation (+ = quieter)
inline double velocityInstrumentAttenuationCb()
{
    return 400.0 * std::log10(80.0 / double(MF_MIDI_LEVEL));   // 38.8 cB = 3.9 dB
}

//! VST3 instruments: the note velocity (0..1) for a position on the dynamics scale.
//! MuseScore sent ppp..fff in equal steps from almost 0 to 1, so ppp was nearly silent in
//! most sample libraries and the loud steps were small. Here the same decibel steps as the
//! built-in sounds are used, on the usual velocity curve (40*log10(v)), with fff = 1.
//! A library's own velocity response still applies on top: it is the library's sound.
inline double vstVelocity(double steps)
{
    const double db = dynamicDb(steps, DynamicsFamily::Generic) - DYNAMIC_DB[7];
    return std::clamp(std::pow(10.0, db / 40.0), 0.01, 1.0);
}
}
