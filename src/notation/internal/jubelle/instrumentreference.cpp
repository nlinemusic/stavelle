/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "instrumentreference.h"

#include <cstdlib>

#include "engraving/dom/instrtemplate.h"

using namespace mu::engraving;
using muse::String;

namespace mu::notation::jubelle {
String pitchName(int midiPitch, bool sharps)
{
    static const char16_t* FLATS[] = { u"C", u"D♭", u"D", u"E♭", u"E", u"F", u"G♭", u"G", u"A♭", u"A", u"B♭", u"B" };
    static const char16_t* SHARPS[] = { u"C", u"C♯", u"D", u"D♯", u"E", u"F", u"F♯", u"G", u"G♯", u"A", u"A♯", u"B" };
    const int pc = ((midiPitch % 12) + 12) % 12;
    const int octave = (midiPitch - pc) / 12 - 1;   // MIDI 60 = C4
    return String(sharps ? SHARPS[pc] : FLATS[pc]) + String::number(octave);
}

String transpositionText(int chromatic)
{
    if (chromatic == 0) {
        return u"Concert pitch (sounds as written)";
    }
    static const char16_t* INTERVALS[] = {
        u"", u"a minor 2nd", u"a major 2nd", u"a minor 3rd", u"a major 3rd", u"a perfect 4th", u"a tritone",
        u"a perfect 5th", u"a minor 6th", u"a major 6th", u"a minor 7th", u"a major 7th"
    };
    const int size = std::abs(chromatic);
    const int octaves = size / 12;
    const int rest = size % 12;
    String interval;
    if (octaves == 1) {
        interval = u"an octave";
    } else if (octaves == 2) {
        interval = u"two octaves";
    } else if (octaves > 2) {
        interval = String::number(octaves) + u" octaves";
    }
    if (rest != 0) {
        interval += (interval.isEmpty() ? String() : String(u" and ")) + String(INTERVALS[rest]);
    }
    String text = u"Sounds " + interval + (chromatic < 0 ? u" lower" : u" higher");
    if (rest != 0) {
        // the instrument's key: what a written C sounds as
        String key = pitchName(60 + chromatic);
        while (!key.isEmpty() && (key.at(key.size() - 1).isDigit() || key.at(key.size() - 1).unicode() == u'-')) {
            key = key.left(key.size() - 1);
        }
        text += u" (written C sounds " + key + u")";
    }
    return text;
}

bool referenceFor(const InstrumentTemplate& tmpl, InstrumentReference* out)
{
    if (!out || tmpl.useDrumset || tmpl.maxPitchP <= tmpl.minPitchP || tmpl.maxPitchP <= 0) {
        return false;
    }
    const int chromatic = tmpl.transpose.chromatic;
    auto range = [](int low, int high) {
        return pitchName(low, false) + u" – " + pitchName(high, false);
    };
    out->id = tmpl.id;
    out->name = tmpl.trackName;
    out->family = tmpl.family ? tmpl.family->name : String();
    // the data holds sounding pitches; written = sounding minus the transposition
    out->writtenRange = range(tmpl.minPitchP - chromatic, tmpl.maxPitchP - chromatic);
    out->comfortableRange = tmpl.maxPitchA > tmpl.minPitchA ? range(tmpl.minPitchA - chromatic, tmpl.maxPitchA - chromatic) : out->writtenRange;
    out->soundingRange = range(tmpl.minPitchP, tmpl.maxPitchP);
    out->transposition = transpositionText(chromatic);
    out->transposing = chromatic != 0;
    return true;
}
}
