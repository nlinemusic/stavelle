/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "templatebuilder.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

#include "engraving/compat/scoreaccess.h"
#include "engraving/compat/mscxcompat.h"
#include "engraving/infrastructure/localfileinfoprovider.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/mscore.h"
#include "engraving/dom/part.h"
#include "engraving/dom/scoreorder.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/box.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/sig.h"
#include "engraving/dom/text.h"
#include "engraving/dom/timesig.h"
#include "engraving/style/defaultstyle.h"
#include "engraving/style/style.h"
#include "engraving/infrastructure/mscwriter.h"
#include "engraving/rw/mscsaver.h"
#include "global/io/buffer.h"

#include "notation/internal/jubelle/templatepage.h"
#include "bigbandtestmusic.h"

using namespace mu::engraving;
using muse::String;
namespace jt = mu::notation::jubelle;

namespace jubelle_templates {
bool loadInstrumentData()
{
    static bool loaded = false;
    if (loaded) {
        return true;
    }
    const std::string share = std::string(MU_ROOT_DIR) + "/share/instruments/";
    loaded = loadInstrumentTemplates(muse::io::path_t(share + "instruments.xml"))
             && loadInstrumentTemplates(muse::io::path_t(share + "orders.xml"));
    return loaded;
}

MasterScore* readTemplate(const std::string& path)
{
    MasterScore* score = compat::ScoreAccess::createMasterScoreWithBaseStyle(nullptr);
    score->setFileInfoProvider(std::make_shared<LocalFileInfoProvider>(muse::io::path_t(path)));
    if (!compat::loadMsczOrMscx(score, muse::io::path_t(path), false)) {
        std::cerr << "can't read " << path << "\n";
        delete score;
        return nullptr;
    }
    score->doLayout();
    return score;
}

bool writeTemplate(MasterScore* score, const std::string& path)
{
    // Templates are installed as folders (the .mscx plus score_style.mss and
    // META-INF), MuseScore's "directory" file form, written by its own saver.
    const std::filesystem::path p(path);
    std::filesystem::remove_all(p.parent_path());
    std::filesystem::create_directories(p.parent_path());
    MscWriter::Params params;
    params.filePath = muse::io::path_t(path);
    params.mainFileName = String::fromStdString(p.filename().string());
    params.mode = MscIoMode::Dir;
    MscWriter writer(params);
    if (!writer.open()) {
        return false;
    }
    const bool ok = MscSaver(score->iocContext()).writeMscz(score, writer, false);
    writer.close();
    return ok && !writer.hasError();
}

bool writeMscz(MasterScore* score, const std::string& path)
{
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    muse::io::Buffer buffer;
    buffer.open(muse::io::IODevice::WriteOnly);
    MscWriter::Params params;
    params.device = &buffer;
    params.filePath = muse::io::path_t(path);
    params.mainFileName = String::fromStdString(std::filesystem::path(path).stem().string() + ".mscx");
    params.mode = MscIoMode::Zip;
    MscWriter writer(params);
    if (!writer.open()) {
        return false;
    }
    const bool ok = MscSaver(score->iocContext()).writeMscz(score, writer, false);
    writer.close();
    if (!ok || writer.hasError()) {
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(buffer.data().constData()), std::streamsize(buffer.data().size()));
    return bool(out);
}

void applyHouseStyle(MasterScore* score, const HouseStyle& hs)
{
    MStyle& st = score->style();
    st.set(Sid::pageTwosided, false);
    st.set(Sid::pageOddLeftMargin, hs.marginSideInch);
    st.set(Sid::pageEvenLeftMargin, hs.marginSideInch);
    st.set(Sid::pageOddTopMargin, hs.marginTopInch);
    st.set(Sid::pageEvenTopMargin, hs.marginTopInch);
    st.set(Sid::pageOddBottomMargin, hs.marginBottomInch);
    st.set(Sid::pageEvenBottomMargin, hs.marginBottomInch);

    // Page size: the US size of the class; the right margin equals the left one
    jt::PageSizeInch size;
    if (jt::usPageSize(hs.pageClass, size)) {
        st.set(Sid::pageWidth, 1.0);
        st.set(Sid::pagePrintableWidth, 1.0 - 2 * hs.marginSideInch);
        jt::setPage(st, size, jt::isLandscapeClass(hs.pageClass));
        st.set(Sid::pagePrintableWidth, st.styleD(Sid::pageWidth) - 2 * hs.marginSideInch);
    }
    st.set(Sid::spatium, hs.spatiumMm * DPMM);

    String scoreOnly;
    for (const StyleValue& v : hs.values) {
        st.set(v.sid, v.value);
        if (v.scoreOnly) {
            if (!scoreOnly.isEmpty()) {
                scoreOnly += u",";
            }
            scoreOnly += String::fromAscii(MStyle::valueName(v.sid));
        }
    }
    score->setMetaTag(jt::PAGE_CLASS_TAG, hs.pageClass);
    if (!scoreOnly.isEmpty()) {
        score->setMetaTag(jt::SCORE_ONLY_STYLE_TAG, scoreOnly);
    } else {
        score->metaTags().erase(jt::SCORE_ONLY_STYLE_TAG);
    }
    st.precomputeValues();
    score->setLayoutAll();
}

HouseStyle bigBandStyle()
{
    // Designed for 11x17 landscape: 17 instruments (18 staves) as one system per
    // page, 4-8 bars per system. The staff size and distances are checked by the
    // layout tests with real big-band music (jubelle_templates_tests.cpp).
    HouseStyle hs;
    hs.name = u"Jubelle Big Band";
    hs.pageClass = u"11x17-landscape";
    hs.marginTopInch = 0.4;
    hs.marginBottomInch = 0.4;
    hs.marginSideInch = 0.5;
    hs.spatiumMm = 1.3;
    hs.values = {
        // vertical: tight within sections, the order's brackets mark sections
        { Sid::staffDistance, Spatium(5.0) },
        { Sid::akkoladeDistance, Spatium(5.0) },
        { Sid::staffUpperBorder, Spatium(5.0) },
        { Sid::staffLowerBorder, Spatium(5.0) },
        { Sid::frameSystemDistance, Spatium(5.0) },
        { Sid::enableVerticalSpread, true },
        { Sid::minStaffSpread, Spatium(3.5) },
        { Sid::maxStaffSpread, Spatium(12.0) },
        // horizontal: at most 8 bars per system on the full score
        { Sid::minMeasureWidth, Spatium(36.0), true },
        // instrument names: full names on the first system, short ones after
        { Sid::enableIndentationOnFirstSystem, true },
        { Sid::longInstrumentFontSize, 13.0, true },
        { Sid::shortInstrumentFontSize, 13.0, true },
        // text that has to be read at arm's length on a large page
        { Sid::chordSymbolAFontSize, 13.5, true },
        { Sid::chordSymbolBFontSize, 13.5, true },
        { Sid::rehearsalMarkFontSize, 20.0, true },
        { Sid::tempoFontSize, 15.0, true },
        { Sid::metronomeFontSize, 15.0, true },
        { Sid::expressionFontSize, 13.0, true },
        { Sid::staffTextFontSize, 13.0, true },
        { Sid::systemTextFontSize, 13.0, true },
        { Sid::dynamicsSize, 1.25, true },
        { Sid::measureNumberFontSize, 10.0 },
        // a full score: no multimeasure rests (parts get them anyway)
        { Sid::createMultiMeasureRests, false },
    };
    return hs;
}

void reorderParts(MasterScore* score, const std::vector<Part*>& order)
{
    std::vector<staff_idx_t> dst;
    for (Part* p : order) {
        for (Staff* s : p->staves()) {
            dst.push_back(s->idx());
        }
    }
    if (dst.size() != score->nstaves()) {
        std::cerr << "reorderParts: not all staves listed\n";
        return;
    }
    score->sortStaves(dst);
    score->setLayoutAll();
}

void setNames(Part* part, const String& longName, const String& shortName)
{
    part->setPartName(longName);
    part->setLongNameAll(longName);
    part->setShortNameAll(shortName);
}

bool replaceInstrument(Part* part, const String& instrumentId)
{
    const InstrumentTemplate* t = searchTemplate(instrumentId);
    if (!t) {
        std::cerr << "no instrument " << instrumentId.toStdString() << "\n";
        return false;
    }
    part->setInstrument(Instrument::fromTemplate(t));
    for (size_t i = 0; i < part->nstaves() && i < t->staffCount; ++i) {
        part->staff(i)->setDefaultClefType(t->clefType(i));
    }
    return true;
}

bool applyScoreOrder(MasterScore* score, const String& orderId)
{
    for (const ScoreOrder& o : instrumentOrders) {
        if (o.id == orderId) {
            score->startCmd(muse::TranslatableString::untranslatable("Jubelle template order"));
            score->setScoreOrder(o);
            score->setBracketsAndBarlines();
            score->endCmd();
            return true;
        }
    }
    std::cerr << "no score order " << orderId.toStdString() << "\n";
    return false;
}

MasterScore* newTemplate(const String& title, const std::vector<NewPart>& parts)
{
    MasterScore* score = compat::ScoreAccess::createMasterScoreWithBaseStyle(nullptr);
    score->setMetaTag(u"workTitle", title);
    score->startCmd(muse::TranslatableString::untranslatable("Jubelle new template"));
    for (const NewPart& np : parts) {
        const InstrumentTemplate* t = searchTemplate(np.instrumentId);
        if (!t) {
            std::cerr << "no instrument " << np.instrumentId.toStdString() << "\n";
            score->endCmd();
            delete score;
            return nullptr;
        }
        score->appendPart(t);
        Part* p = score->parts().back();
        if (!np.longName.isEmpty()) {
            setNames(p, np.longName, np.shortName);
        }
    }

    const Fraction fourFour(4, 4);
    score->sigmap()->add(0, fourFour);
    score->appendMeasures(32);
    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        m->setTimesig(fourFour);
        m->setTicks(fourFour);
    }
    TimeSig* ts = Factory::createTimeSig(score->dummy()->segment());
    ts->setSig(fourFour, TimeSigType::NORMAL);
    score->cmdAddTimeSig(score->firstMeasure(), 0, ts, false);

    // Title frame (its title text is the template's name in New Score)
    MeasureBase* box = static_cast<Score*>(score)->insertMeasure(ElementType::VBOX, score->first());
    if (box && box->isVBox()) {
        Text* text = Factory::createText(box, TextStyleType::TITLE);
        text->setPlainText(title);
        text->setParent(box);
        score->undoAddElement(text);
    }
    score->endCmd();
    score->doLayout();
    return score;
}

HouseStyle soloWithPianoStyle()
{
    // US Letter (T4). The solo staff stays full size (a cue-size staff would also
    // make the soloist's part small); MuseScore's chamber spacing, with a little
    // more room between the solo staff and the piano and between systems.
    HouseStyle hs;
    hs.name = u"Jubelle Solo with Piano";
    hs.pageClass = u"letter";
    hs.marginTopInch = 0.5;
    hs.marginBottomInch = 0.5;
    hs.marginSideInch = 0.6;
    hs.spatiumMm = 1.75;
    hs.values = {
        { Sid::staffDistance, Spatium(7.0) },
        { Sid::akkoladeDistance, Spatium(6.5) },
        { Sid::minSystemDistance, Spatium(10.0) },
        { Sid::maxSystemDistance, Spatium(16.0) },
        { Sid::enableIndentationOnFirstSystem, true },
        { Sid::createMultiMeasureRests, false },
    };
    return hs;
}

const std::vector<SoloWithPiano>& soloWithPianoTemplates()
{
    static const std::vector<SoloWithPiano> t {
        { "01-Violin_+_Piano", u"Violin + Piano", u"violin" },
        { "02-Viola_+_Piano", u"Viola + Piano", u"viola" },
        { "03-Cello_+_Piano", u"Cello + Piano", u"violoncello" },
        { "04-Flute_+_Piano", u"Flute + Piano", u"flute" },
        { "05-Oboe_+_Piano", u"Oboe + Piano", u"oboe" },
        { "06-Clarinet_+_Piano", u"Clarinet + Piano", u"bb-clarinet" },
        { "07-Alto_Saxophone_+_Piano", u"Alto Saxophone + Piano", u"alto-saxophone" },
        { "08-Tenor_Saxophone_+_Piano", u"Tenor Saxophone + Piano", u"tenor-saxophone" },
        { "09-Trumpet_+_Piano", u"Trumpet + Piano", u"bb-trumpet" },
        { "10-Horn_+_Piano", u"Horn + Piano", u"horn" },
        { "11-Trombone_+_Piano", u"Trombone + Piano", u"trombone" },
    };
    return t;
}

MasterScore* buildBigBand(const std::string& originalsDir)
{
    MasterScore* score = readTemplate(originalsDir + "/05-Jazz/02-Big_Band/02-Big_Band.mscx");
    if (!score) {
        return nullptr;
    }

    // MuseScore's original: 5 saxes, 4 trumpets, 4 trombones, guitar, piano,
    // double bass, drum kit.
    std::vector<Part*> saxes, trumpets, trombones;
    Part* piano = nullptr;
    Part* guitar = nullptr;
    Part* bass = nullptr;
    Part* drums = nullptr;
    for (Part* p : score->parts()) {
        const String id = p->instrument()->id();
        if (id.contains(u"saxophone")) {
            saxes.push_back(p);
        } else if (id.contains(u"trumpet")) {
            trumpets.push_back(p);
        } else if (id.contains(u"trombone")) {
            trombones.push_back(p);
        } else if (id == u"piano") {
            piano = p;
        } else if (id.contains(u"guitar")) {
            guitar = p;
        } else if (id.contains(u"bass")) {
            bass = p;
        } else if (p->hasDrumStaff()) {
            drums = p;
        }
    }
    if (saxes.size() != 5 || trumpets.size() != 4 || trombones.size() != 4 || !piano || !guitar || !bass || !drums) {
        std::cerr << "Big Band original has unexpected instruments\n";
        delete score;
        return nullptr;
    }

    // Trombone 4 becomes a bass trombone
    if (!replaceInstrument(trombones[3], u"bass-trombone")) {
        delete score;
        return nullptr;
    }

    std::vector<Part*> order = saxes;
    order.insert(order.end(), trumpets.begin(), trumpets.end());
    order.insert(order.end(), trombones.begin(), trombones.end());
    order.insert(order.end(), { piano, guitar, bass, drums });
    reorderParts(score, order);

    const char16_t* saxNames[5][2] = {
        { u"Alto Sax 1", u"A. Sx. 1" }, { u"Alto Sax 2", u"A. Sx. 2" },
        { u"Tenor Sax 1", u"T. Sx. 1" }, { u"Tenor Sax 2", u"T. Sx. 2" },
        { u"Baritone Sax", u"B. Sx." } };
    for (size_t i = 0; i < 5; ++i) {
        setNames(saxes[i], saxNames[i][0], saxNames[i][1]);
    }
    for (size_t i = 0; i < 4; ++i) {
        const String n = String::number(int(i + 1));
        setNames(trumpets[i], u"Trumpet " + n, u"Tpt. " + n);
    }
    for (size_t i = 0; i < 3; ++i) {
        const String n = String::number(int(i + 1));
        setNames(trombones[i], u"Trombone " + n, u"Tbn. " + n);
    }
    setNames(trombones[3], u"Bass Trombone", u"B. Tbn.");
    setNames(piano, u"Piano", u"Pno.");
    setNames(guitar, u"Guitar", u"Gtr.");
    setNames(bass, u"Bass", u"Bass");
    setNames(drums, u"Drums", u"Dr.");

    // T2: Leland by default. MuseScore's jazz templates use the handwritten
    // MuseJazz look (MuseJazz fonts, jazz chord symbols, thicker lines, no
    // measure numbers); the Big Band starts from MuseScore's standard style
    // instead. MuseJazz remains available as an optional look.
    score->style() = DefaultStyle::baseStyle();
    score->checkChordList();

    if (!applyScoreOrder(score, u"bigband")) {
        delete score;
        return nullptr;
    }
    applyHouseStyle(score, bigBandStyle());
    score->doLayout();
    return score;
}

int buildAll(const std::string& originalsDir, const std::string& outDir)
{
    int failures = 0;
    if (!loadInstrumentData()) {
        std::cerr << "can't load instruments\n";
        return 1;
    }

    if (MasterScore* bb = buildBigBand(originalsDir)) {
        if (!writeTemplate(bb, outDir + "/05-Jazz/02-Big_Band/02-Big_Band.mscx")) {
            ++failures;
        }
        // The realistic test music, as a new score made from the template, for
        // the PDF prints in the Windows build (tools/jubelle_checks/template_prints.py)
        jt::applyTemplatePageClass(bb, 8.5, 11.0);
        bb->setMetaTag(u"workTitle", u"Big Band template: layout test");
        if (!writeBigBandTestMusic(bb)
            || !writeMscz(bb, std::string(MU_ROOT_DIR) + "/tools/jubelle_templates/testscores/Big_Band_test_music.mscz")) {
            ++failures;
        }
        delete bb;
    } else {
        ++failures;
    }
    // Solo Instrument + Piano (new, built from MuseScore's instrument definitions)
    for (const SoloWithPiano& t : soloWithPianoTemplates()) {
        MasterScore* s = newTemplate(t.title, { { t.instrumentId, u"", u"" }, { u"piano", u"", u"" } });
        if (!s) {
            ++failures;
            continue;
        }
        applyHouseStyle(s, soloWithPianoStyle());
        s->doLayout();
        const std::string dir = std::string("/09-Solo_with_Piano/") + t.dir + "/" + t.dir + ".mscx";
        if (!writeTemplate(s, outDir + dir)) {
            ++failures;
        }
        delete s;
    }
    return failures;
}
}
