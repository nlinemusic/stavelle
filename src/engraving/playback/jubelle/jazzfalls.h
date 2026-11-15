/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle playback block: falls and doits that sound played, not stretched.
//
// MuseScore times a fall as a share of the written note, so a fall on a whole
// note takes seconds and one on an eighth is a blip. Players don't do that: a
// fall takes about the same time whatever the note, and how it sounds depends
// on the instrument. Here:
//  - its length is in milliseconds, from the symbol (short / medium / long),
//    a little longer on trombone (the slide), a little shorter on saxophone
//    and clarinet, a little longer in a low register, and a little shorter at
//    fast tempos; it is always placed at the end of the note;
//  - saxophone and clarinet falls, and "rough" falls on any instrument, move
//    in semitone steps (fingered or valved); trumpet lip falls and trombone
//    falls are smooth.
// The amount of pitch drop and its curve still come from the playback
// profile and from Format → Articulation playback. If you set a fall's start
// there yourself (score, instrument or passage), your setting is used as it
// is and nothing here changes it. Playback only: the notation never changes.

#pragma once

#include "global/types/string.h"
#include "mpe/events.h"

namespace mu::engraving {
class Chord;
class Note;
}

namespace mu::engraving::jubelle {
enum class FallLength {
    Short,
    Medium,
    Long,
};

enum class FallStyle {
    Smooth,     // the default; brassFallSmooth*
    Lip,        // brassFallLip*
    Rough,      // brassFallRough*: in steps
};

struct FallShape {
    double lengthMs = 0.0;
    bool stepped = false;
};

//! The played length and manner of a fall or doit
FallShape fallShapeFor(const muse::String& instrumentSoundId, FallLength length, FallStyle style, int pitch, double bpm);

//! Moves the fall part of a pitch curve so it lasts `fallMs` at the end of a
//! note `noteMs` long (never starting before 30% of the note), and, when
//! `stepped`, turns it into semitone steps. The amount of the drop and its
//! curve are kept.
muse::mpe::PitchCurve retimeFall(const muse::mpe::PitchCurve& curve, double noteMs, double fallMs, bool stepped);

//! For the note renderer: the note's fall or doit played in real time, or
//! the event unchanged when there is none or you set the fall start yourself.
muse::mpe::NoteEvent applyJazzFall(const Note* note, muse::mpe::NoteEvent event);
}
