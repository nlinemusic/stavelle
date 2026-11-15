/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle: realistic big-band music for judging the Big Band template's layout
// (your requirement, 15 Nov 2026: test with real notation, not empty scores).
//
// 40 bars in concert F, medium swing, written into a score made from the Big
// Band template, in five passages of different density:
//   A  1-8   ensemble hits: accented chords, marcatos, a long note with a fall
//   B  9-16  sax soli: five-part eighth-note lines with sixteenth runs, slurs
//   C 17-24  shout chorus: full band, syncopated eighths and sixteenths,
//            accents, marcatos, falls, doits, ff
//   D 25-32  ballad: sustained pads, pp, sparse rhythm section
//   E 33-40  swing backgrounds: sax melody, brass pads, two chords per bar
// Every passage has chord symbols on piano and guitar (guitar in slashes), a
// walking bass and a drum part; rehearsal marks, tempo text, dynamics and
// hairpins are throughout.

#pragma once

#include <string>
#include <vector>

namespace mu::engraving {
class MasterScore;
}

namespace jubelle_templates {
struct Passage {
    std::string name;
    int firstBar = 0;    // 1-based, inclusive
    int lastBar = 0;
    bool dense = false;  // eighth- and sixteenth-note writing
};

const std::vector<Passage>& bigBandPassages();

//! Writes the music into a score made from the Big Band template (17 parts in
//! the template's order). Adds measures as needed. Returns false on failure.
bool writeBigBandTestMusic(mu::engraving::MasterScore* score);
}
