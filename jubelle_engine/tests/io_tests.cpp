// SPDX-License-Identifier: GPL-3.0-only
#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "io/mscxreader.h"
#include "io/scorefile.h"
#include "model/invariants.h"

using namespace je;

TEST(Xml, RoundTripsMuseScoreLayoutByteForByte)
{
    const std::string text = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<museScore version=\"4.60\">\n  <Score>\n"
                             "    <metaTag name=\"workTitle\">A &amp; B</metaTag>\n    <Staff id=\"1\">\n      <Measure>\n"
                             "        <voice>\n          <Chord>\n            <durationType>quarter</durationType>\n"
                             "            <acciaccatura/>\n            </Chord>\n          </voice>\n        </Measure>\n"
                             "      </Staff>\n    <text><b>Bold</b> plain</text>\n    <empty></empty>\n    </Score>\n  </museScore>\n";
    io::xml::Document d;
    std::string error;
    ASSERT_TRUE(io::xml::parse(text, d, error)) << error;
    EXPECT_EQ(io::xml::write(d), text);
    EXPECT_EQ(d.root.child("Score")->child("metaTag")->text(), "A & B");
    EXPECT_EQ(d.root.child("Score")->child("text")->raw, "<b>Bold</b> plain");
}

TEST(Xml, RejectsMalformedInput)
{
    io::xml::Document d;
    std::string error;
    EXPECT_FALSE(io::xml::parse("<a><b></a>", d, error));
    EXPECT_FALSE(io::xml::parse("<a x=1/>", d, error));
    EXPECT_FALSE(io::xml::parse("", d, error));
    EXPECT_EQ(io::xml::decode("&#x266F;&#9837;&lt;"), "\xE2\x99\xAF\xE2\x99\xAD<");
}

TEST(Zip, WritesAndReadsBack)
{
    std::vector<io::zip::Entry> in(3);
    in[0].name = "META-INF/container.xml";
    in[0].data = "<container><rootfiles><rootfile full-path=\"s.mscx\"/></rootfiles></container>";
    in[1].name = "s.mscx";
    in[1].data = std::string(20000, 'x') + "end";
    in[2].name = "Thumbnails/thumbnail.png";
    in[2].data = std::string("\x89PNG\r\n", 6) + std::string(100, '\0');
    in[2].stored = true;
    const std::string bytes = io::zip::write(in);
    std::vector<io::zip::Entry> out;
    std::string error;
    ASSERT_TRUE(io::zip::read(bytes, out, error)) << error;
    ASSERT_EQ(out.size(), 3u);
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(out[i].name, in[i].name);
        EXPECT_EQ(out[i].data, in[i].data);
    }
    std::string damaged = bytes;
    damaged[60] ^= 0x55;   // inside the first entry's data: the checksum catches it
    EXPECT_FALSE(io::zip::read(damaged, out, error));
}

namespace {
const char* SCORE = R"(<?xml version="1.0" encoding="UTF-8"?>
<museScore version="4.60">
  <Score>
    <Division>480</Division>
    <Style>
      <jubelleMagneticLayout>1</jubelleMagneticLayout>
      <jubelleNudgeLimit>0.75</jubelleNudgeLimit>
      <pageWidth>8.5</pageWidth>
      </Style>
    <metaTag name="workTitle">Test</metaTag>
    <Part id="1">
      <Staff id="1">
        <StaffType group="pitched">
          <name>stdNormal</name>
          </StaffType>
        </Staff>
      <trackName>Horn in F</trackName>
      <Instrument id="horn">
        <longName>Horn in F</longName>
        <shortName>Hn.</shortName>
        <instrumentId>brass.french-horn</instrumentId>
        <transposeDiatonic>-4</transposeDiatonic>
        <transposeChromatic>-7</transposeChromatic>
        </Instrument>
      </Part>
    <Staff id="1">
      <Measure>
        <voice>
          <TimeSig>
            <sigN>2</sigN>
            <sigD>4</sigD>
            </TimeSig>
          <Tempo>
            <tempo>2</tempo>
            <text>q = 120</text>
            </Tempo>
          <Tuplet>
            <normalNotes>2</normalNotes>
            <actualNotes>3</actualNotes>
            <baseNote>eighth</baseNote>
            </Tuplet>
          <Chord>
            <durationType>eighth</durationType>
            <Lyrics>
              <syllabic>begin</syllabic>
              <text>Hal</text>
              </Lyrics>
            <Note>
              <pitch>60</pitch>
              <tpc>14</tpc>
              </Note>
            </Chord>
          <Chord>
            <durationType>eighth</durationType>
            <Note>
              <pitch>62</pitch>
              <tpc>16</tpc>
              </Note>
            </Chord>
          <Rest>
            <durationType>eighth</durationType>
            </Rest>
          <endTuplet/>
          <Chord>
            <durationType>eighth</durationType>
            <acciaccatura/>
            <Note>
              <pitch>66</pitch>
              <tpc>20</tpc>
              </Note>
            </Chord>
          <Chord>
            <durationType>quarter</durationType>
            <Articulation>
              <subtype>articStaccatoAbove</subtype>
              </Articulation>
            <Note>
              <pitch>64</pitch>
              <tpc>18</tpc>
              <Spanner type="Tie">
                <Tie>
                  </Tie>
                <next>
                  <location>
                    <measures>1</measures>
                    <fractions>-1/4</fractions>
                    </location>
                  </next>
                </Spanner>
              </Note>
            </Chord>
          </voice>
        </Measure>
      <Measure>
        <endRepeat>2</endRepeat>
        <voice>
          <Chord>
            <durationType>half</durationType>
            <Note>
              <pitch>64</pitch>
              <tpc>18</tpc>
              </Note>
            </Chord>
          </voice>
        </Measure>
      </Staff>
    </Score>
  </museScore>
)";
}

TEST(MscxReader, ReadsTupletsGracesTiesLyricsAndRepeats)
{
    io::ScoreFile f;
    std::string error;
    ASSERT_TRUE(io::loadBytes(SCORE, true, f, error)) << error;
    EXPECT_EQ(io::xml::write(f.main), SCORE);
    model::Model m;
    IdGenerator ids(1);
    io::ReadReport r;
    ASSERT_TRUE(io::readScore(f, m, ids, r, error)) << error;
    const auto problems = model::checkInvariants(m.snapshot());
    EXPECT_TRUE(problems.empty()) << (problems.empty() ? "" : problems.front());
    EXPECT_EQ(r.measures, 2);
    EXPECT_EQ(r.tuplets, 1);
    EXPECT_EQ(r.graces, 1);
    EXPECT_EQ(r.ties, 1);
    EXPECT_EQ(r.lyrics, 1);
    EXPECT_EQ(r.chords, 5);
    EXPECT_EQ(r.rests, 1);
    EXPECT_TRUE(r.warnings.empty()) << r.warnings.front();
    // triplet eighths 1/12 each, then the grace (no time), a quarter at 1/4, then the tied half in bar 2
    EXPECT_EQ(io::musicListing(m.snapshot()), "0 0 0/1 1/12 60\n0 0 1/12 1/12 62\n0 0 1/6 1/12\n0 0 1/4 1/8 g 66\n"
                                              "0 0 1/4 1/4 64\n0 0 1/2 1/2 64\n");
}

TEST(EngineStore, RestoresJubelleStyleAfterMuseScoreDropsIt)
{
    io::ScoreFile f;
    std::string error;
    ASSERT_TRUE(io::loadBytes(SCORE, true, f, error)) << error;
    io::updateEngineStore(f);
    EXPECT_NE(io::metaTag(f, "jubelleEngine").find("jubelleNudgeLimit"), std::string::npos);

    // what MuseScore 4.7.7 does when it saves: unknown style values are dropped, meta tags kept (docs/13)
    io::ScoreFile museScoreSaved;
    ASSERT_TRUE(io::loadBytes(io::saveBytes(f), true, museScoreSaved, error)) << error;
    auto& kids = museScoreSaved.score()->child("Style")->children;
    kids.erase(std::remove_if(kids.begin(), kids.end(), [](const io::xml::Node& n) { return n.name.rfind("jubelle", 0) == 0; }),
               kids.end());
    ASSERT_EQ(museScoreSaved.score()->child("Style")->child("jubelleMagneticLayout"), nullptr);

    const auto restored = io::restoreFromEngineStore(museScoreSaved);
    EXPECT_EQ(restored.size(), 2u);
    EXPECT_EQ(museScoreSaved.score()->child("Style")->childText("jubelleMagneticLayout"), "1");
    EXPECT_EQ(museScoreSaved.score()->child("Style")->childText("jubelleNudgeLimit"), "0.75");
    EXPECT_EQ(museScoreSaved.score()->child("Style")->childText("pageWidth"), "8.5");
    // a value in the file wins over the store; nothing else is restored
    EXPECT_TRUE(io::restoreFromEngineStore(museScoreSaved).empty());
}
