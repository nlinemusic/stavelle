// SPDX-License-Identifier: GPL-3.0-only
// Regression tests for the native reader (S2): each case is one behaviour found by comparing the reader with
// MuseScore's own reading of the repository's scores (docs/15_file_handling.md §2b). The expected listings are what
// MuseScore 4.7.7's engine reads from the same XML.
#include <gtest/gtest.h>

#include <string>

#include "io/mscxreader.h"
#include "io/scorefile.h"
#include "model/invariants.h"

using namespace je;

namespace {
std::string note(int pitch) { return "<Note><pitch>" + std::to_string(pitch) + "</pitch></Note>"; }
std::string chord(const std::string& type, int pitch, const std::string& extra = "")
{
    return "<Chord>" + extra + "<durationType>" + type + "</durationType>" + note(pitch) + "</Chord>";
}
std::string rest(const std::string& type) { return "<Rest><durationType>" + type + "</durationType></Rest>"; }
const std::string TS44 = "<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>";

//! A one-part score; `staves` holds each music staff's measures (XML), compact layout (the reader doesn't need indentation)
std::string score(const std::vector<std::string>& staves, const std::string& version = "4.60")
{
    std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<museScore version=\"" + version + "\"><Score><Division>480</Division>"
                    "<Part id=\"1\">";
    for (size_t i = 0; i < staves.size(); ++i) {
        s += "<Staff id=\"" + std::to_string(i + 1) + "\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff>";
    }
    s += "<trackName>Piano</trackName><Instrument id=\"piano\"><longName>Piano</longName></Instrument></Part>";
    for (size_t i = 0; i < staves.size(); ++i) {
        s += "<Staff id=\"" + std::to_string(i + 1) + "\">" + staves[i] + "</Staff>";
    }
    return s + "</Score></museScore>\n";
}

std::string measure(const std::string& voice1, const std::string& voice2 = "", const std::string& attrs = "",
                    const std::string& inside = "")
{
    std::string m = "<Measure" + attrs + ">" + inside + "<voice>" + voice1 + "</voice>";
    if (!voice2.empty()) {
        m += "<voice>" + voice2 + "</voice>";
    }
    return m + "</Measure>";
}

struct Read {
    std::string listing;
    io::ReadReport report;
    std::vector<std::string> problems;
};

Read read(const std::string& xml)
{
    Read r;
    io::ScoreFile f;
    std::string error;
    EXPECT_TRUE(io::loadBytes(xml, true, f, error)) << error;
    model::Model m;
    IdGenerator ids(1);
    EXPECT_TRUE(io::readScore(f, m, ids, r.report, error)) << error;
    r.listing = io::musicListing(m.snapshot());
    r.problems = model::checkInvariants(m.snapshot());
    return r;
}
}

// MuseScore's layout-only multi-measure rest measure is not music
TEST(ReaderRegression, MultiMeasureRestMeasureIsSkipped)
{
    const std::string mm = "<Measure><multiMeasureRest>2</multiMeasureRest><voice><Rest><durationType>measure</durationType>"
                           "<duration>2/1</duration></Rest></voice></Measure>";
    const Read r = read(score({ mm + measure(TS44 + "<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>")
                                + measure("<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>") }));
    EXPECT_EQ(r.listing, "0 0 0/1 1/1\n0 0 1/1 1/1\n");
    EXPECT_TRUE(r.problems.empty());
}

// several after-graces: MuseScore orders them the other way round from the file (they're written before the main chord)
TEST(ReaderRegression, AfterGracesInMuseScoreOrder)
{
    const Read r = read(score({ measure(TS44 + chord("16th", 67, "<grace16after/>") + chord("32nd", 69, "<grace32after/>")
                                        + chord("whole", 60)) }));
    EXPECT_EQ(r.listing, "0 0 0/1 1/1 60\n0 0 0/1 1/32 g 69\n0 0 0/1 1/16 g 67\n");
    EXPECT_TRUE(r.problems.empty());
}

// a time signature after a measure's music starts with the next measure
TEST(ReaderRegression, EndOfMeasureTimeSignatureStartsNextMeasure)
{
    const Read r = read(score({ measure(TS44 + chord("whole", 60) + "<TimeSig><sigN>3</sigN><sigD>4</sigD></TimeSig>")
                                + measure(chord("half", 62) + chord("quarter", 64)) + measure(chord("quarter", 65)) }));
    EXPECT_EQ(r.listing, "0 0 0/1 1/1 60\n0 0 1/1 1/2 62\n0 0 3/2 1/4 64\n0 0 7/4 1/4 65\n0 0 2/1 1/4\n0 0 9/4 1/4\n");
}

// a measure rest fills its measure (a stored <duration> that disagrees doesn't count)
TEST(ReaderRegression, MeasureRestFillsItsMeasure)
{
    const Read r = read(score({ measure(TS44 + "<Rest><durationType>measure</durationType><duration>3/2</duration></Rest>") }));
    EXPECT_EQ(r.listing, "0 0 0/1 1/1\n");
}

// voice 1 ends at the measure's end: a short one is completed with rests split on the beat; a rewinding <location>
// (a chord symbol placed during the last chord) doesn't make it short
TEST(ReaderRegression, Voice1IsCompletedLikeMuseScore)
{
    const Read shortMeasure = read(score({ measure(TS44 + chord("quarter", 60)) }));
    EXPECT_EQ(shortMeasure.listing, "0 0 0/1 1/4 60\n0 0 1/4 1/4\n0 0 1/2 1/2\n");
    EXPECT_TRUE(shortMeasure.problems.empty());
    const Read rewound = read(score({ measure(TS44 + chord("half", 60) + chord("half", 62)
                                              + "<location><fractions>-1/4</fractions></location><Harmony><root>14</root></Harmony>") }));
    EXPECT_EQ(rewound.listing, "0 0 0/1 1/2 60\n0 0 1/2 1/2 62\n");
    const Read empty = read(score({ measure(TS44 + chord("whole", 60)) + "<Measure></Measure>" }));
    EXPECT_EQ(empty.listing, "0 0 0/1 1/1 60\n0 0 1/1 1/1\n");
}

// two-note tremolo chords sound for half their written value, also inside a tuplet
TEST(ReaderRegression, TremoloDurationsAreScaledByTuplets)
{
    const std::string tup = "<Tuplet><normalNotes>2</normalNotes><actualNotes>3</actualNotes><baseNote>quarter</baseNote></Tuplet>";
    const Read r = read(score({ measure(TS44 + tup + "<Chord><durationType>half</durationType><duration>1/4</duration>" + note(60)
                                        + "</Chord><Chord><durationType>half</durationType><duration>1/4</duration>" + note(64)
                                        + "</Chord>" + chord("quarter", 67) + "<endTuplet/>" + chord("half", 72)) }));
    EXPECT_EQ(r.listing, "0 0 0/1 1/6 60\n0 0 1/6 1/6 64\n0 0 1/3 1/6 67\n0 0 1/2 1/2 72\n");
    EXPECT_TRUE(r.problems.empty()) << (r.problems.empty() ? "" : r.problems.front());
}

// a staff with its own time signature (MuseScore's stretch): its music is scaled, positions (<location>) are not
TEST(ReaderRegression, LocalTimeSignatureStretchesTheStaff)
{
    const std::string ts98 = "<TimeSig><sigN>9</sigN><sigD>8</sigD></TimeSig>";
    const std::string ts34 = "<TimeSig><sigN>3</sigN><sigD>4</sigD><stretchN>2</stretchN><stretchD>3</stretchD></TimeSig>";
    const Read r = read(score({ measure(ts98 + chord("half", 60, "<dots>1</dots>") + chord("quarter", 62, "<dots>1</dots>")),
                                measure(ts34 + chord("half", 67, "<dots>1</dots>")) }));
    EXPECT_EQ(r.listing, "0 0 0/1 3/4 60\n0 0 3/4 3/8 62\n1 0 0/1 9/8 67\n");
    EXPECT_TRUE(r.problems.empty()) << (r.problems.empty() ? "" : r.problems.front());
}

// a full tuplet holds nothing more, even without <endTuplet/>
TEST(ReaderRegression, FullTupletClosesWithoutEndTuplet)
{
    const std::string tup = "<Tuplet><normalNotes>2</normalNotes><actualNotes>3</actualNotes><baseNote>eighth</baseNote></Tuplet>";
    const Read r = read(score({ measure(TS44 + tup + chord("eighth", 60) + chord("eighth", 62) + chord("eighth", 64)
                                        + rest("quarter") + rest("half")) }));
    EXPECT_EQ(r.listing, "0 0 0/1 1/12 60\n0 0 1/12 1/12 62\n0 0 1/6 1/12 64\n0 0 1/4 1/4\n0 0 1/2 1/2\n");
    EXPECT_TRUE(r.problems.empty());
}

// a value given twice: the last one counts, as MuseScore reads in order
TEST(ReaderRegression, RepeatedDurationTypeLastWins)
{
    const Read r = read(score({ measure(TS44 + "<Chord><durationType>half</durationType><durationType>quarter</durationType>"
                                        + note(60) + "</Chord>" + rest("quarter") + rest("half")) }));
    EXPECT_EQ(r.listing, "0 0 0/1 1/4 60\n0 0 1/4 1/4\n0 0 1/2 1/2\n");
}

// a genuine format limitation: a tuplet whose notes overflow it is an invalid file; it's kept as written and reported
TEST(ReaderRegression, OverflowingTupletIsReported)
{
    const std::string tup = "<Tuplet><normalNotes>2</normalNotes><actualNotes>3</actualNotes><baseNote>16th</baseNote></Tuplet>";
    const Read r = read(score({ measure(TS44 + tup + chord("eighth", 60, "<dots>1</dots>") + chord("eighth", 62, "<dots>1</dots>")
                                        + "<endTuplet/>") }));
    bool warned = false;
    for (const std::string& w : r.report.warnings) {
        warned = warned || w.find("last longer than the tuplet") != std::string::npos;
    }
    EXPECT_TRUE(warned);
}
