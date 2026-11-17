/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle (17 Nov 2026): importing MusicXML from Noteflight and other programs.
//
// A quick count of what a MusicXML file contains (notes, lyrics, dynamics,
// chord symbols, slurs…) and which program wrote it. After MuseScore's own
// MusicXML importer has converted the file, the same things are counted in
// the score; anything that arrived short is listed in the import report, so
// you know what to check. Plain C++ (a tag scanner, not a full XML parser).

#pragma once

#include <string>
#include <vector>

namespace mu::project::jubelle {
struct MusicXmlCounts {
    int parts = 0;
    int notes = 0;          // sounding notes, including grace notes and tied continuations
    int lyrics = 0;         // syllables
    int dynamics = 0;
    int chordSymbols = 0;
    int slurs = 0;
    int hairpins = 0;
    int tuplets = 0;
    int tempoMarks = 0;
    int texts = 0;          // words in directions (expression and staff text, rehearsal marks)
};

struct MusicXmlSummary {
    bool isMusicXml = false;
    std::string software;       // as written in the file, "" if not given
    bool fromNoteflight = false;
    std::string title;
    std::vector<std::string> partNames;
    MusicXmlCounts counts;
};

MusicXmlSummary summarizeMusicXml(const std::string& xml);

//! One line per kind of item of which fewer arrived than the file contains,
//! e.g. "Lyrics: 118 of 120 arrived". Empty if everything counted arrived.
std::vector<std::string> shortfalls(const MusicXmlCounts& inFile, const MusicXmlCounts& inScore);

//! The whole report as text (for the message and for "Import report.txt")
std::string importReport(const MusicXmlSummary& file, const MusicXmlCounts& inScore);
}
