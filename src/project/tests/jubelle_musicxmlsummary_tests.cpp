/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle: MusicXML summary and import report (Noteflight import).
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

#include "project/internal/musicxmlsummary.h"
#include "project/internal/projectlibrary.h"

using namespace mu::project::jubelle;
namespace pl = mu::project::projectlibrary;
namespace fs = std::filesystem;

namespace {
std::string readFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

const char* SMALL
    =R"(<?xml version="1.0"?><score-partwise version="3.1">
<work><work-title>Tom &amp; Jerry</work-title></work>
<identification><encoding><software>Noteflight version 0.4</software></encoding></identification>
<part-list><score-part id="P1"><part-name>Trumpet in B♭</part-name></score-part>
<score-part id="P2"><part-name>Voice</part-name></score-part></part-list>
<part id="P1"><measure number="1">
<direction><direction-type><dynamics><mf/></dynamics></direction-type></direction>
<direction><direction-type><wedge type="crescendo"/></direction-type></direction>
<harmony><root><root-step>C</root-step></root><kind>major</kind></harmony>
<note><pitch><step>C</step><octave>5</octave></pitch><duration>1</duration><notations><slur type="start"/></notations></note>
<note><grace/><pitch><step>D</step><octave>5</octave></pitch></note>
<note><chord/><pitch><step>E</step><octave>5</octave></pitch><duration>1</duration><notations><slur type="stop"/></notations></note>
<note><rest/><duration>2</duration></note>
<direction><direction-type><wedge type="stop"/></direction-type></direction>
</measure></part>
<part id="P2"><measure number="1">
<direction><direction-type><metronome><beat-unit>quarter</beat-unit><per-minute>120</per-minute></metronome></direction-type></direction>
<direction><direction-type><words>dolce</words></direction-type></direction>
<note><pitch><step>G</step><octave>4</octave></pitch><duration>2</duration><lyric><syllabic>single</syllabic><text>la</text></lyric>
<notations><tuplet type="start"/></notations></note>
<note><pitch><step>A</step><octave>4</octave></pitch><duration>2</duration><lyric><extend/></lyric></note>
</measure></part></score-partwise>)";
}

TEST(Jubelle_MusicXmlSummary, CountsWhatTheFileContains)
{
    const MusicXmlSummary s = summarizeMusicXml(SMALL);
    ASSERT_TRUE(s.isMusicXml);
    EXPECT_TRUE(s.fromNoteflight);
    EXPECT_EQ(s.software, "Noteflight version 0.4");
    EXPECT_EQ(s.title, "Tom & Jerry");
    ASSERT_EQ(s.partNames.size(), 2u);
    EXPECT_EQ(s.partNames[1], "Voice");
    EXPECT_EQ(s.counts.parts, 2);
    EXPECT_EQ(s.counts.notes, 5);        // rests don't count; grace and chord notes do
    EXPECT_EQ(s.counts.lyrics, 1);       // an extender line alone isn't a syllable
    EXPECT_EQ(s.counts.dynamics, 1);
    EXPECT_EQ(s.counts.chordSymbols, 1);
    EXPECT_EQ(s.counts.slurs, 1);
    EXPECT_EQ(s.counts.hairpins, 1);
    EXPECT_EQ(s.counts.tuplets, 1);
    EXPECT_EQ(s.counts.tempoMarks, 1);
    EXPECT_EQ(s.counts.texts, 1);
}

TEST(Jubelle_MusicXmlSummary, OtherFilesAreNotMusicXml)
{
    EXPECT_FALSE(summarizeMusicXml("").isMusicXml);
    EXPECT_FALSE(summarizeMusicXml("<html><note>hello</note></html>").isMusicXml);
    // a part name containing "score-partwise" text isn't the element
    EXPECT_FALSE(summarizeMusicXml("<x>score-partwise</x>").isMusicXml);
}

TEST(Jubelle_MusicXmlSummary, ReportListsOnlyWhatArrivedShort)
{
    const MusicXmlSummary s = summarizeMusicXml(SMALL);
    MusicXmlCounts arrived = s.counts;
    EXPECT_TRUE(shortfalls(s.counts, arrived).empty());
    EXPECT_NE(importReport(s, arrived).find("Everything counted in the file arrived"), std::string::npos);

    arrived.lyrics = 0;
    arrived.notes += 3;   // more than the file (e.g. written-out repeats) is never a shortfall
    const auto missing = shortfalls(s.counts, arrived);
    ASSERT_EQ(missing.size(), 1u);
    EXPECT_EQ(missing[0], "Lyrics (syllables): 0 of 1 arrived");
    EXPECT_NE(importReport(s, arrived).find("Noteflight version 0.4"), std::string::npos);
}

TEST(Jubelle_MusicXmlSummary, RealNoteflightExportsAreRecognised)
{
    const fs::path data = fs::path(JUBELLE_SOURCE_ROOT) / "src/importexport/musicxml/tests/data";
    for (const char* name : { "testNoteflightStartRepeatBarline.xml", "testInstrImport.xml", "testImportDrums.xml" }) {
        const std::string xml = readFile(data / name);
        ASSERT_FALSE(xml.empty()) << name;
        const MusicXmlSummary s = summarizeMusicXml(xml);
        EXPECT_TRUE(s.isMusicXml) << name;
        EXPECT_TRUE(s.fromNoteflight) << name;
        EXPECT_GE(s.counts.parts, 1) << name;
        EXPECT_EQ(size_t(s.counts.parts), s.partNames.size()) << name;
    }
}

TEST(Jubelle_MusicXmlSummary, TheOriginalIsKeptInTheProjectUnchanged)
{
    std::random_device rd;
    const fs::path root = fs::temp_directory_path() / ("jubelle_keeporiginal_" + std::to_string(rd()));
    fs::create_directories(root / "project");
    const fs::path original = root / "My Song.musicxml";
    { std::ofstream(original, std::ios::binary) << SMALL; }

    const pl::Result r = pl::keepOriginal((root / "project").u8string(), original.u8string());
    ASSERT_TRUE(r.ok) << r.error;
    const fs::path copy = root / "project" / pl::IMPORTED_ORIGINALS_FOLDER / "My Song.musicxml";
    EXPECT_EQ(readFile(copy), std::string(SMALL));
    EXPECT_EQ(readFile(original), std::string(SMALL)) << "the original is untouched";
    EXPECT_FALSE(pl::keepOriginal((root / "project").u8string(), (root / "missing.xml").u8string()).ok);
    std::error_code ec;
    fs::remove_all(root, ec);
}
