/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle G10: Learning Center reference data. Ranges and transpositions are
// read from the same instrument data the program uses to write and play the
// instruments, so the reference always agrees with what Jubelle does.

#pragma once

#include <vector>

#include "global/types/string.h"

namespace mu::engraving {
class InstrumentTemplate;
}

namespace mu::notation::jubelle {
//! "C4" (middle C), "B♭3", "F♯5"; flats unless `sharps`
muse::String pitchName(int midiPitch, bool sharps = false);

//! "Concert pitch", "Sounds a major 2nd lower (written C sounds B♭)", "Sounds an octave higher"…
//! `chromatic`: semitones from written to sounding (B♭ trumpet: -2)
muse::String transpositionText(int chromatic);

struct InstrumentReference {
    muse::String id;
    muse::String name;
    muse::String family;
    muse::String writtenRange;       // what a professional can play, as written: "F♯3 – D6"
    muse::String comfortableRange;   // the safe range, as written
    muse::String soundingRange;      // the professional range at concert pitch
    muse::String transposition;
    bool transposing = false;
};

//! Pitched instruments only (drum kits and unpitched percussion have no range)
bool referenceFor(const engraving::InstrumentTemplate& tmpl, InstrumentReference* out);
}
