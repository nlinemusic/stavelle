/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle: checks of the templates as installed (share/templates), plus the
// page-class and score-only-style mechanisms.

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <memory>
#include <iostream>
#include <map>
#include <sstream>

#include "engraving/dom/bracketItem.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/mscore.h"
#include "engraving/dom/page.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/system.h"
#include "engraving/style/defaultstyle.h"
#include "engraving/style/style.h"

#include "notation/internal/jubelle/templatepage.h"
#include "templatebuilder.h"
#include "bigbandtestmusic.h"
#include "fontproviderstub.h"

using namespace mu::engraving;
using muse::String;
namespace jt = mu::notation::jubelle;

namespace {
const std::string TEMPLATES = std::string(MU_ROOT_DIR) + "/share/templates/";

std::unique_ptr<MasterScore> bigBand()
{
    jubelle_templates::loadInstrumentData();
    return std::unique_ptr<MasterScore>(jubelle_templates::readTemplate(TEMPLATES + "05-Jazz/02-Big_Band/02-Big_Band.mscx"));
}

// Layout tests need real symbol sizes (the engine tool runs them in a second
// pass with realistic font metrics; see main.cpp)
#define REQUIRE_REALISTIC_METRICS() \
    if (!muse::draw::FontProviderStub::realisticMetrics()) { GTEST_SKIP() << "needs realistic metrics"; }

bool near(double a, double b, double tol = 0.01) { return std::abs(a - b) < tol; }

//! Thick (column 0) bracket starting at this staff: its span, or 0
size_t sectionBracket(const Staff* s)
{
    for (const BracketItem* b : s->brackets()) {
        if (b->column() == 0 && b->bracketType() == BracketType::NORMAL) {
            return b->bracketSpan();
        }
    }
    return 0;
}

bool hasBrace(const Staff* s)
{
    for (const BracketItem* b : s->brackets()) {
        if (b->bracketType() == BracketType::BRACE) {
            return true;
        }
    }
    return false;
}
}

TEST(Jubelle_Templates, AllOriginalTemplatesAreListedAndPresent)
{
    // 41 MuseScore templates in categories.json, each present; plus My First Score
    std::ifstream f(TEMPLATES + "categories.json");
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string json = ss.str();
    size_t count = 0;
    for (size_t pos = json.find(".mscx\""); pos != std::string::npos; pos = json.find(".mscx\"", pos + 1)) {
        const size_t start = json.rfind('"', pos - 1) + 1;
        const std::string rel = json.substr(start, pos + 5 - start);
        EXPECT_TRUE(std::ifstream(TEMPLATES + rel).good()) << rel;
        ++count;
    }
    EXPECT_GE(count, 41u);
    EXPECT_TRUE(std::ifstream(TEMPLATES + "My_First_Score.mscx").good());
}

TEST(Jubelle_Templates, BigBandInstrumentsAndOrder)
{
    auto s = bigBand();
    ASSERT_TRUE(s);
    const std::vector<std::pair<const char16_t*, const char16_t*> > expected = {
        { u"alto-saxophone", u"Alto Sax 1" }, { u"alto-saxophone", u"Alto Sax 2" },
        { u"tenor-saxophone", u"Tenor Sax 1" }, { u"tenor-saxophone", u"Tenor Sax 2" },
        { u"baritone-saxophone", u"Baritone Sax" },
        { u"bb-trumpet", u"Trumpet 1" }, { u"bb-trumpet", u"Trumpet 2" },
        { u"bb-trumpet", u"Trumpet 3" }, { u"bb-trumpet", u"Trumpet 4" },
        { u"trombone", u"Trombone 1" }, { u"trombone", u"Trombone 2" }, { u"trombone", u"Trombone 3" },
        { u"bass-trombone", u"Bass Trombone" },
        { u"piano", u"Piano" }, { u"electric-guitar", u"Guitar" },
        { u"double-bass-plucked", u"Bass" }, { u"", u"Drums" },
    };
    ASSERT_EQ(s->parts().size(), expected.size());
    EXPECT_EQ(s->nstaves(), 18u) << "17 instruments, piano on two staves";
    for (size_t i = 0; i < expected.size(); ++i) {
        const Part* p = s->parts()[i];
        if (*expected[i].first) {
            EXPECT_EQ(p->instrument()->id(), String(expected[i].first)) << i;
        } else {
            EXPECT_TRUE(p->hasDrumStaff());
        }
        EXPECT_EQ(p->longName(), String(expected[i].second)) << i;
        EXPECT_EQ(p->partName(), String(expected[i].second)) << i;
        EXPECT_FALSE(p->shortName().isEmpty()) << i;
    }
}

TEST(Jubelle_Templates, BigBandTranspositionsAndClefs)
{
    auto s = bigBand();
    ASSERT_TRUE(s);
    auto chrom = [&](size_t i) { return s->parts()[i]->instrument()->transpose().chromatic; };
    EXPECT_EQ(chrom(0), -9);    // alto: E-flat, sounds a major sixth lower
    EXPECT_EQ(chrom(2), -14);   // tenor: B-flat, a major ninth lower
    EXPECT_EQ(chrom(4), -21);   // baritone: E-flat, an octave and a sixth lower
    EXPECT_EQ(chrom(5), -2);    // trumpet in B-flat
    EXPECT_EQ(chrom(9), 0);     // trombone, concert pitch
    EXPECT_EQ(chrom(12), 0);    // bass trombone
    EXPECT_EQ(chrom(14), 0);    // guitar: the octave is in its clef (treble 8vb)
    EXPECT_EQ(s->parts()[14]->staff(0)->defaultClefType().concertClef, ClefType::G8_VB);
    EXPECT_EQ(chrom(15), -12);  // bass sounds an octave lower
    EXPECT_EQ(s->parts()[12]->staff(0)->defaultClefType().concertClef, ClefType::F);
    EXPECT_EQ(s->parts()[13]->nstaves(), 2u);
    EXPECT_EQ(s->parts()[13]->staff(1)->defaultClefType().concertClef, ClefType::F);
}

TEST(Jubelle_Templates, BigBandSectionBracketsAndPianoBrace)
{
    auto s = bigBand();
    ASSERT_TRUE(s);
    EXPECT_EQ(sectionBracket(s->staff(0)), 5u);   // saxes
    EXPECT_EQ(sectionBracket(s->staff(5)), 4u);   // trumpets
    EXPECT_EQ(sectionBracket(s->staff(9)), 4u);   // trombones
    EXPECT_EQ(sectionBracket(s->staff(13)), 5u);  // piano (2), guitar, bass, drums
    EXPECT_TRUE(hasBrace(s->staff(13)));
    for (staff_idx_t i = 0; i < s->nstaves(); ++i) {
        const bool lastOfSection = (i == 4 || i == 8 || i == 12 || i == 17);
        if (lastOfSection) {
            EXPECT_FALSE(s->staff(i)->barLineSpan()) << "barline stops at section end, staff " << i;
        }
    }
}

TEST(Jubelle_Templates, BigBandPageAndHouseStyle)
{
    auto s = bigBand();
    ASSERT_TRUE(s);
    const MStyle& st = s->style();
    EXPECT_TRUE(near(st.styleD(Sid::pageWidth), 17.0)) << "11x17 landscape";
    EXPECT_TRUE(near(st.styleD(Sid::pageHeight), 11.0));
    EXPECT_TRUE(near(st.styleD(Sid::pagePrintableWidth), 16.0));
    EXPECT_TRUE(near(st.spatium() / DPMM, 1.3));
    EXPECT_EQ(st.styleS(Sid::minMeasureWidth).val(), 36.0);
    EXPECT_EQ(st.styleSt(Sid::musicalSymbolFont), String(u"Leland")) << "T2: Leland by default";
    EXPECT_EQ(st.styleSt(Sid::musicalTextFont), String(u"Leland Text"));
    EXPECT_NE(st.styleSt(Sid::defaultFontFace), String(u"MuseJazz Text"));
    EXPECT_EQ(st.styleSt(Sid::chordDescriptionFile), String(u"chords_std.xml"));
    EXPECT_TRUE(st.styleB(Sid::showMeasureNumber)) << "arrangers need bar numbers";
    EXPECT_EQ(s->metaTag(jt::PAGE_CLASS_TAG), String(u"11x17-landscape"));
    EXPECT_TRUE(s->metaTag(jt::SCORE_ONLY_STYLE_TAG).contains(u"minMeasureWidth"));
}

TEST(Jubelle_TemplateLayout, BigBandFitsOneSystemPerPage)
{
    REQUIRE_REALISTIC_METRICS();
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->doLayout();
    ASSERT_FALSE(s->pages().empty());
    const double pageH = s->style().styleD(Sid::pageHeight) * DPI;
    const double bottom = s->style().styleD(Sid::pageOddBottomMargin) * DPI;
    for (const Page* p : s->pages()) {
        ASSERT_EQ(p->systems().size(), (p == s->pages().front()) ? 2u : 1u)
            << "the title frame plus one system on page 1, one system on the others";
        const System* sys = p->systems().back();
        EXPECT_LT(sys->pagePos().y() + sys->height(), pageH - bottom + 0.5) << "system overflows the page";
    }
}

TEST(Jubelle_Templates, PageClassUsesUsOrInternationalSizes)
{
    jt::PageSizeInch sz;
    ASSERT_TRUE(jt::usPageSize(u"11x17-landscape", sz));
    EXPECT_TRUE(near(sz.width, 11) && near(sz.height, 17));
    ASSERT_TRUE(jt::internationalPageSize(u"9x12", sz));
    EXPECT_TRUE(near(sz.width * 25.4, 250, 0.1) && near(sz.height * 25.4, 353, 0.1)) << "B4";
    EXPECT_TRUE(jt::isUsPaper(8.5, 11));
    EXPECT_TRUE(jt::isUsPaper(17, 11));
    EXPECT_FALSE(jt::isUsPaper(210 / 25.4, 297 / 25.4));

    auto us = bigBand();
    ASSERT_TRUE(us);
    EXPECT_TRUE(jt::applyTemplatePageClass(us.get(), 8.5, 11));
    EXPECT_TRUE(near(us->style().styleD(Sid::pageWidth), 17.0));
    EXPECT_TRUE(us->metaTag(jt::PAGE_CLASS_TAG).isEmpty()) << "the tag isn't left in new scores";

    auto a = bigBand();
    ASSERT_TRUE(a);
    EXPECT_TRUE(jt::applyTemplatePageClass(a.get(), 210 / 25.4, 297 / 25.4));
    EXPECT_TRUE(near(a->style().styleD(Sid::pageWidth), 420 / 25.4)) << "A3 landscape";
    EXPECT_TRUE(near(a->style().styleD(Sid::pageHeight), 297 / 25.4));
    EXPECT_TRUE(near(a->style().styleD(Sid::pagePrintableWidth), 420 / 25.4 - 1.0)) << "same side margins";
}

TEST(Jubelle_TemplateLayout, BigBandA3VersionFits)
{
    REQUIRE_REALISTIC_METRICS();
    auto a = bigBand();
    ASSERT_TRUE(a);
    jt::applyTemplatePageClass(a.get(), 210 / 25.4, 297 / 25.4);
    ASSERT_TRUE(jubelle_templates::writeBigBandTestMusic(a.get()));
    a->doLayout();
    const double pageH = a->style().styleD(Sid::pageHeight) * DPI;
    for (const Page* p : a->pages()) {
        EXPECT_LE(p->systems().size(), 2u) << "the A3 version still has one system per page";
        const System* sys = p->systems().back();
        EXPECT_LT(sys->pagePos().y() + sys->height(), pageH) << "page " << p->pageNumber() + 1;
    }
}

TEST(Jubelle_Templates, PartsDontInheritScoreOnlyValues)
{
    auto s = bigBand();
    ASSERT_TRUE(s);
    // a part made the way MuseScore makes parts
    Excerpt* ex = new Excerpt(s.get());
    ex->setParts({ s->parts()[0] });
    s->initExcerpt(ex);
    Score* part = ex->excerptScore();
    ASSERT_TRUE(part);
    EXPECT_EQ(part->style().styleS(Sid::minMeasureWidth).val(),
              DefaultStyle::defaultStyle().styleS(Sid::minMeasureWidth).val());
    EXPECT_EQ(part->style().styleD(Sid::chordSymbolAFontSize),
              DefaultStyle::defaultStyle().styleD(Sid::chordSymbolAFontSize));
    // values not marked score-only are still inherited, as in MuseScore
    EXPECT_EQ(part->style().styleS(Sid::staffDistance).val(), 5.0);
}

TEST(Jubelle_TemplateLayout, BigBandWithRealMusic)
{
    REQUIRE_REALISTIC_METRICS();
    // Layout judged on real big-band writing (bigbandtestmusic.h), not empty bars
    auto s = bigBand();
    ASSERT_TRUE(s);
    ASSERT_TRUE(jubelle_templates::writeBigBandTestMusic(s.get()));
    s->doLayout();

    const double pageH = s->style().styleD(Sid::pageHeight) * DPI;
    const double bottom = s->style().styleD(Sid::pageOddBottomMargin) * DPI;
    std::map<std::string, std::vector<int> > barsPerSystem;
    int systems = 0;
    for (const Page* p : s->pages()) {
        size_t musicSystems = 0;
        for (const System* sys : p->systems()) {
            if (!sys->firstMeasure()) {
                continue; // title frame
            }
            ++musicSystems;
            ++systems;
            EXPECT_LT(sys->pagePos().y() + sys->height(), pageH - bottom + 0.5) << "system overflows page " << p->pageNumber() + 1;
            const int first = sys->firstMeasure()->no() + 1;
            int count = 0;
            for (const MeasureBase* m = sys->firstMeasure(); m; m = m->next()) {
                if (m->isMeasure()) {
                    ++count;
                }
                if (m == sys->lastMeasure()) {
                    break;
                }
            }
            if (first > 40 || sys == s->systems().back()) {
                continue; // the final system holds whatever bars are left
            }
            if (systems <= 2) {
                std::cout << "[ Big Band ] system " << systems << ": instrument names take "
                          << sys->leftMargin() / DPMM << " mm\n";
            }
            for (const jubelle_templates::Passage& ps : jubelle_templates::bigBandPassages()) {
                if (first >= ps.firstBar && first <= ps.lastBar) {
                    barsPerSystem[ps.name].push_back(count);
                }
            }
        }
        EXPECT_LE(musicSystems, 1u) << "one system per page, page " << p->pageNumber() + 1;
    }
    std::cout << "[ Big Band ] systems: " << systems << ", pages: " << s->pages().size() << "\n";
    for (const auto& [name, counts] : barsPerSystem) {
        std::cout << "[ Big Band ] " << name << ": bars per system";
        for (int c : counts) {
            std::cout << " " << c;
            EXPECT_LE(c, 8) << name;
            EXPECT_GE(c, 3) << name << ": never cramped below 3 bars";
        }
        std::cout << "\n";
    }

}

TEST(Jubelle_Templates, SoloWithPianoFamily)
{
    // T3: eleven Solo Instrument + Piano templates, built from MuseScore's
    // instrument definitions; US Letter (T4)
    jubelle_templates::loadInstrumentData();
    struct Expect { const char* dir; const char16_t* id; int chromatic; ClefType clef; };
    const std::vector<Expect> expected {
        { "01-Violin_+_Piano", u"violin", 0, ClefType::G },
        { "02-Viola_+_Piano", u"viola", 0, ClefType::C3 },
        { "03-Cello_+_Piano", u"violoncello", 0, ClefType::F },
        { "04-Flute_+_Piano", u"flute", 0, ClefType::G },
        { "05-Oboe_+_Piano", u"oboe", 0, ClefType::G },
        { "06-Clarinet_+_Piano", u"bb-clarinet", -2, ClefType::G },
        { "07-Alto_Saxophone_+_Piano", u"alto-saxophone", -9, ClefType::G },
        { "08-Tenor_Saxophone_+_Piano", u"tenor-saxophone", -14, ClefType::G8_VB },   // concert-pitch view: treble 8vb
        { "09-Trumpet_+_Piano", u"bb-trumpet", -2, ClefType::G },
        { "10-Horn_+_Piano", u"horn", -7, ClefType::G },
        { "11-Trombone_+_Piano", u"trombone", 0, ClefType::F },
    };
    ASSERT_EQ(jubelle_templates::soloWithPianoTemplates().size(), expected.size());
    for (const Expect& e : expected) {
        const std::string path = TEMPLATES + "09-Solo_with_Piano/" + e.dir + "/" + e.dir + ".mscx";
        std::unique_ptr<MasterScore> s(jubelle_templates::readTemplate(path));
        ASSERT_TRUE(s) << path;
        ASSERT_EQ(s->parts().size(), 2u) << e.dir;
        const Part* solo = s->parts()[0];
        const Part* piano = s->parts()[1];
        EXPECT_EQ(solo->instrument()->id(), String(e.id)) << e.dir;
        EXPECT_EQ(solo->instrument()->transpose().chromatic, e.chromatic) << e.dir;
        EXPECT_EQ(solo->staff(0)->defaultClefType().concertClef, e.clef) << e.dir;
        EXPECT_FALSE(solo->staff(0)->staffType(Fraction(0, 1))->isSmall()) << "full-size solo staff, so its part is full size";
        EXPECT_EQ(piano->instrument()->id(), String(u"piano"));
        ASSERT_EQ(piano->nstaves(), 2u);
        EXPECT_TRUE(hasBrace(piano->staff(0))) << e.dir;
        EXPECT_EQ(piano->staff(1)->defaultClefType().concertClef, ClefType::F);
        EXPECT_TRUE(near(s->style().styleD(Sid::pageWidth), 8.5)) << "US Letter";
        EXPECT_TRUE(near(s->style().styleD(Sid::pageHeight), 11.0));
        EXPECT_EQ(s->metaTag(jt::PAGE_CLASS_TAG), String(u"letter"));
        EXPECT_EQ(s->nmeasures(), 32u);
        EXPECT_TRUE(s->first()->isVBox()) << "title frame: New Score lists the template by its title";
    }
}

TEST(Jubelle_Templates, PianoTrioAndStringTrio)
{
    // T3: new chamber templates; 9x12 (T4)
    jubelle_templates::loadInstrumentData();
    std::unique_ptr<MasterScore> pt(jubelle_templates::readTemplate(TEMPLATES + "03-Chamber_Music/07-Piano_Trio/07-Piano_Trio.mscx"));
    ASSERT_TRUE(pt);
    ASSERT_EQ(pt->parts().size(), 3u);
    EXPECT_EQ(pt->parts()[0]->instrument()->id(), String(u"violin"));
    EXPECT_EQ(pt->parts()[1]->instrument()->id(), String(u"violoncello"));
    EXPECT_EQ(pt->parts()[2]->instrument()->id(), String(u"piano"));
    EXPECT_TRUE(hasBrace(pt->parts()[2]->staff(0)));
    EXPECT_TRUE(near(pt->style().styleD(Sid::pageWidth), 9.0));
    EXPECT_TRUE(near(pt->style().styleD(Sid::pageHeight), 12.0));

    std::unique_ptr<MasterScore> st(jubelle_templates::readTemplate(TEMPLATES + "03-Chamber_Music/08-String_Trio/08-String_Trio.mscx"));
    ASSERT_TRUE(st);
    ASSERT_EQ(st->parts().size(), 3u);
    EXPECT_EQ(st->parts()[1]->instrument()->id(), String(u"viola"));
    EXPECT_EQ(st->parts()[1]->staff(0)->defaultClefType().concertClef, ClefType::C3);
    EXPECT_EQ(sectionBracket(st->staff(0)), 3u) << "the three strings bracketed together";
    EXPECT_TRUE(near(st->style().styleD(Sid::pageWidth), 9.0));
}

TEST(Jubelle_Templates, RefinedOriginalsHaveTheirPageAndLook)
{
    // T5: MuseScore's templates refined in place; T4/T6 page sizes; T2 Leland
    jubelle_templates::loadInstrumentData();
    ASSERT_EQ(jubelle_templates::refinements().size(), 40u) << "the 41 originals minus the Big Band (redesigned separately)";
    for (const jubelle_templates::Refinement& r : jubelle_templates::refinements()) {
        std::unique_ptr<MasterScore> s(jubelle_templates::readTemplate(TEMPLATES + r.path));
        ASSERT_TRUE(s) << r.path;
        jt::PageSizeInch size;
        ASSERT_TRUE(jt::usPageSize(r.pageClass, size));
        EXPECT_TRUE(near(s->style().styleD(Sid::pageWidth), size.width)) << r.path;
        EXPECT_TRUE(near(s->style().styleD(Sid::pageHeight), size.height)) << r.path;
        EXPECT_EQ(s->metaTag(jt::PAGE_CLASS_TAG), String(r.pageClass)) << r.path;
        EXPECT_EQ(s->style().styleSt(Sid::musicalSymbolFont), String(u"Leland")) << r.path;
        EXPECT_NE(s->style().styleSt(Sid::defaultFontFace), String(u"MuseJazz Text")) << r.path;
        EXPECT_FALSE(s->parts().empty()) << r.path;
    }
}

TEST(Jubelle_TemplateLayout, RefinedOriginalsFitTheirPages)
{
    REQUIRE_REALISTIC_METRICS();
    jubelle_templates::loadInstrumentData();
    for (const jubelle_templates::Refinement& r : jubelle_templates::refinements()) {
        std::unique_ptr<MasterScore> s(jubelle_templates::readTemplate(TEMPLATES + r.path));
        ASSERT_TRUE(s) << r.path;
        for (bool international : { false, true }) {
            if (international) {
                jt::applyTemplatePageClass(s.get(), 210 / 25.4, 297 / 25.4);   // the A-size version
            }
            s->setLayoutAll();
            s->doLayout();
            const double pageH = s->style().styleD(Sid::pageHeight) * DPI;
            const double bottom = s->style().styleD(Sid::pageOddBottomMargin) * DPI;
            for (const Page* p : s->pages()) {
                for (const System* sys : p->systems()) {
                    EXPECT_LT(sys->pagePos().y() + sys->height(), pageH - bottom + 0.5)
                        << r.path << (international ? " (A size)" : "") << ": a system overflows page " << p->pageNumber() + 1;
                }
            }
        }
    }
}
