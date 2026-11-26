/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine: tests for the style values and the music fonts (S7).
 */
#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <sstream>

#include "edit/changeset.h"
#include "edit/scorebuilder.h"
#include "io/mscxreader.h"
#include "io/mscxwriter.h"
#include "io/details.h"
#include "io/xml.h"
#include "model/fields.h"
#include "model/packing.h"
#include "model/model.h"
#include "style/font.h"
#include "style/json.h"
#include "style/style.h"

using namespace je;
using namespace je::style;

TEST(Style, DefaultsAreMuseScores)
{
    const Style s;
    EXPECT_DOUBLE_EQ(s.spatium(), 1.75 * DPMM);
    EXPECT_DOUBLE_EQ(s.value(Sid::pageWidth), 210.0 / INCH);
    EXPECT_DOUBLE_EQ(s.value(Sid::staffDistance), 6.5);
    EXPECT_DOUBLE_EQ(s.value(Sid::stemWidth), 0.10);
    EXPECT_TRUE(s.boolean(Sid::pageTwosided));
    EXPECT_EQ(s.text(Sid::musicalSymbolFont), "Leland");
    EXPECT_EQ(Style::sid("staffUpperBorder"), Sid::staffUpperBorder);
    EXPECT_EQ(Style::sid("noSuchValue"), Sid::NOSTYLE);
    EXPECT_EQ(s.point(Sid::lyricsPosBelow), (PointF { 0.0, 3.0 }));
    EXPECT_EQ(s.color(Sid::lyricsOddFrameBgColor), 0xFFFFFF00u);
    for (int i = 0; i < int(Sid::STYLES); ++i) {
        EXPECT_EQ(Style::sid(Style::def(Sid(i)).name), Sid(i));
        EXPECT_TRUE(s.isDefault(Sid(i)));
    }
}

TEST(Style, ValuesAreReadAsMuseScoreReadsThem)
{
    Style s;
    EXPECT_TRUE(s.read("Spatium", "2", {}, 470));
    EXPECT_DOUBLE_EQ(s.spatium(), 2 * DPMM);
    EXPECT_TRUE(s.read("staffDistance", "7.5", {}, 470));
    EXPECT_DOUBLE_EQ(s.value(Sid::staffDistance), 7.5);
    EXPECT_TRUE(s.read("lyricsPosAbove", "", { { "x", "0.5" }, { "y", "-3" } }, 470));
    EXPECT_EQ(s.point(Sid::lyricsPosAbove), (PointF { 0.5, -3.0 }));
    EXPECT_TRUE(s.read("staffPosAbove", "", { { "x", "0" }, { "y", "-2.5" } }, 430));   // the pre-4.4 name
    EXPECT_EQ(s.point(Sid::staffTextPosAbove), (PointF { 0.0, -2.5 }));
    EXPECT_TRUE(s.read("lyricsOddFontBold", "1", {}, 470));   // the old font flags into the font style
    EXPECT_EQ(s.integer(Sid::lyricsOddFontStyle) & 1, 1);
    EXPECT_FALSE(s.read("notAStyleValue", "1", {}, 470));
    s.finishReading(450);
    EXPECT_DOUBLE_EQ(s.value(Sid::spacingDensity), 1.30);   // 4.50's narrower spacing
    EXPECT_TRUE(s.boolean(Sid::scaleRythmicSpacingForSmallNotes));
    Style old;
    old.finishReading(440);
    EXPECT_FALSE(old.boolean(Sid::scaleRythmicSpacingForSmallNotes));   // didn't exist before 4.5
    EXPECT_EQ(old.text(Sid::tiePlacementChord), "outside");
}

TEST(Style, AScoresStyleComesFromItsFile)
{
    model::Model m;
    IdGenerator ids(1);
    edit::ScoreBuilder b(m, ids);
    edit::Transaction tx(m, "New score");
    b.newScore(tx);
    tx.set(m.score(), model::fields::STYLE, model::Value(model::Text(model::packPairs({
        { "Spatium", "2" }, { "staffDistance", "8" }, { "defaultsVersion", "450" }, { "staffPosAbove", "\x01" } }))));
    io::xml::Document d;
    std::string error;
    ASSERT_TRUE(io::xml::parse("<Style><lyricsPosBelow x=\"0\" y=\"4.5\"/></Style>", d, error)) << error;
    tx.set(m.score(), model::fields::DETAILS_2, model::Value(model::Text(io::fragment({ &d.root.children[0] }))));
    tx.set(m.score(), model::fields::SOURCE_FORMAT, model::Value(model::Text("4.50")));
    tx.commit();
    EXPECT_EQ(formatVersion(m.snapshot()), 450);
    const Style s = scoreStyle(m.snapshot());
    EXPECT_DOUBLE_EQ(s.spatium(), 2 * DPMM);
    EXPECT_DOUBLE_EQ(s.value(Sid::staffDistance), 8.0);
    EXPECT_EQ(s.point(Sid::lyricsPosBelow), (PointF { 0.0, 4.5 }));
    EXPECT_DOUBLE_EQ(s.value(Sid::spacingDensity), 1.30);   // 4.50's defaults
    EXPECT_EQ(scoreStyle(m.snapshot(), Id(999999)) == Style(), true);   // no such layout: the defaults
}

TEST(MusicFont, LelandsSymbolsHaveTheirOutlinesMetrics)
{
    auto leland = MusicFont::byName("Leland");
    ASSERT_TRUE(leland);
    const double sp = STYLE_DEFS[int(Sid::spatium)].a;
    EXPECT_TRUE(leland->isValid(SymId::noteheadBlack));
    EXPECT_TRUE(leland->hasAnchor(SymId::noteheadBlack, SmuflAnchor::stemUpSE));
    // each glyph's box against the font's own metadata (written from the same outlines, to 3 decimals)
    std::ifstream in(defaultFontsDir() + "/leland/leland_metadata.json");
    std::stringstream ss;
    ss << in.rdbuf();
    json::Value meta;
    std::string error;
    ASSERT_TRUE(json::parse(ss.str(), meta, error)) << error;
    const json::Value* boxes = meta.get("glyphBBoxes");
    ASSERT_TRUE(boxes);
    int compared = 0;
    for (int i = 1; i < SYM_COUNT; ++i) {
        const json::Value* b = boxes->get(SYMBOL_CODES[i].name);
        if (!b || !leland->isValid(SymId(i)) || leland->code(SymId(i)) != SYMBOL_CODES[i].smufl) {
            continue;
        }
        const json::Value* ne = b->get("bBoxNE");
        const json::Value* sw = b->get("bBoxSW");
        const RectF r = leland->bbox(SymId(i));
        EXPECT_NEAR(r.left() / sp, sw->array[0].number, 0.0051) << SYMBOL_CODES[i].name;
        EXPECT_NEAR(r.right() / sp, ne->array[0].number, 0.0051) << SYMBOL_CODES[i].name;
        EXPECT_NEAR(-r.top() / sp, ne->array[1].number, 0.0051) << SYMBOL_CODES[i].name;
        EXPECT_NEAR(-r.bottom() / sp, sw->array[1].number, 0.0051) << SYMBOL_CODES[i].name;
        ++compared;
    }
    EXPECT_GT(compared, 300);
    // a symbol Leland lacks comes from Bravura
    auto bravura = MusicFont::fallback();
    ASSERT_TRUE(bravura);
    int fromBravura = 0;
    for (int i = 1; i < SYM_COUNT; ++i) {
        if (!leland->isValid(SymId(i)) && bravura->isValid(SymId(i))) {
            EXPECT_EQ(leland->bbox(SymId(i)).width(), bravura->bbox(SymId(i)).width());
            ++fromBravura;
        }
    }
    EXPECT_GT(fromBravura, 0);
    // Bravura's noteheads are its oversized alternates, as MuseScore uses them
    EXPECT_EQ(bravura->code(SymId::noteheadBlack), 0xF4BEu);
}

TEST(MusicFont, EveryMuseScoreFontLoads)
{
    for (const MusicFontInfo& info : musicFonts()) {
        auto f = MusicFont::byName(info.name);
        ASSERT_TRUE(f) << info.name;
        EXPECT_TRUE(f->isValid(SymId::gClef) || MusicFont::fallback()->isValid(SymId::gClef)) << info.name;
        EXPECT_GT(f->bbox(SymId::noteheadBlack).width(), 0.0) << info.name;
    }
    EXPECT_FALSE(MusicFont::byName("No Such Font"));
}
