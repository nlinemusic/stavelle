/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "prepareparts.h"

#include <algorithm>

#include "pageturns.h"
#include "proofread.h"

#include "engraving/dom/box.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/layoutbreak.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/stafftext.h"
#include "engraving/dom/text.h"

#include "translation.h"

using namespace mu::engraving;

namespace mu::notation::jubelle {
static const char* CTX = "notation/jubelle/prepareparts";
static const char16_t* TACET_TEXT = u"Tacet";

String instrumentLine(const Score* part)
{
    if (!part || part->parts().empty()) {
        return String();
    }
    const Part* p = part->parts().front();
    String line = p->partName();
    // the other instruments this player picks up (Instrument changes in the part)
    std::vector<String> doubles;
    for (const auto& [tick, instrument] : p->instruments()) {
        if (tick == 0 || !instrument) {
            continue;
        }
        const String name = instrument->trackName();
        if (!name.isEmpty() && name != p->instrument(Fraction(0, 1))->trackName()
            && std::find(doubles.begin(), doubles.end(), name) == doubles.end()) {
            doubles.push_back(name);
        }
    }
    if (!doubles.empty()) {
        String list;
        for (size_t i = 0; i < doubles.size(); ++i) {
            list += (i ? u", " : u"") + doubles[i];
        }
        line += u" " + muse::mtrc(CTX, "(doubling %1)").arg(list);
    }
    return line;
}

namespace {
bool hasNotes(const Score* part, const Measure* from, const Measure* to)
{
    for (const Measure* m = from; m; m = m->nextMeasure()) {
        for (const Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
            for (const EngravingItem* e : seg->elist()) {
                if (e && e->isChord()) {
                    return true;
                }
            }
        }
        if (m == to) {
            break;
        }
    }
    (void)part;
    return false;
}

//! [first, last] measures of each section (split by section breaks)
std::vector<std::pair<Measure*, Measure*> > sections(Score* score)
{
    std::vector<std::pair<Measure*, Measure*> > out;
    Measure* start = score->firstMeasure();
    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        const bool lastInSection = m->sectionBreak() || !m->nextMeasure();
        if (lastInSection) {
            out.push_back({ start, m });
            start = m->nextMeasure();
        }
    }
    return out;
}

bool alreadyTacet(Measure* m)
{
    for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
        for (EngravingItem* a : seg->annotations()) {
            if (a && a->isStaffText() && toStaffText(a)->plainText() == TACET_TEXT) {
                return true;
            }
        }
    }
    return false;
}

void addTacet(Score* part, Measure* m)
{
    Segment* seg = m->first(SegmentType::ChordRest);
    if (!seg || alreadyTacet(m)) {
        return;
    }
    StaffText* t = Factory::createStaffText(seg, TextStyleType::EXPRESSION);
    t->setPlainText(TACET_TEXT);
    t->setTrack(0);
    t->setParent(seg);
    t->setExcludeFromOtherParts(true);   // in this part only
    part->undoAddElement(t, /*addToLinkedStaves*/ false);
}

Text* instrumentText(Score* part)
{
    return part->getText(TextStyleType::INSTRUMENT_EXCERPT);
}
}

std::vector<PreparedPart> prepareParts(MasterScore* master, const PreparePartsOptions& options)
{
    std::vector<PreparedPart> out;
    if (!master) {
        return out;
    }
    for (Excerpt* ex : master->excerpts()) {
        Score* part = ex ? ex->excerptScore() : nullptr;
        if (!part) {
            continue;
        }
        PreparedPart r;
        r.part = part;
        r.name = ex->name();

        if (options.multiMeasureRests && !part->style().styleB(Sid::createMultiMeasureRests)) {
            part->undoChangeStyleVal(Sid::createMultiMeasureRests, true);
            r.turnedOnMMRests = true;
        }

        const bool anyNotes = hasNotes(part, part->firstMeasure(), nullptr);
        if (options.instrumentLine) {
            String line = instrumentLine(part);
            if (options.tacet && !anyNotes) {
                line += u" — " + muse::mtrc(CTX, "Tacet");
            }
            if (Text* t = instrumentText(part); t && t->plainText() != line) {
                t->undoChangeProperty(Pid::TEXT, TextBase::plainToXmlText(line));
                r.instrumentLine = line;
            }
        }
        if (options.tacet) {
            if (!anyNotes) {
                r.wholeTacet = true;
                if (Measure* first = part->firstMeasure()) {
                    addTacet(part, first);
                }
            } else {
                const auto secs = sections(part);
                if (secs.size() > 1) {
                    int bar = 1;
                    for (const auto& [from, to] : secs) {
                        if (!hasNotes(part, from, to)) {
                            addTacet(part, from);
                            r.tacetSectionBars.push_back(bar);
                        }
                        for (Measure* m = from; m; m = m->nextMeasure()) {
                            ++bar;
                            if (m == to) {
                                break;
                            }
                        }
                    }
                }
            }
        }

        part->doLayout();
        if (options.pageTurns && part->pages().size() > 1) {
            TurnSettings settings;
            const TurnResult turns = optimiseTurns(part, settings);
            r.turnsMoved = turns.moved;
            r.turnsStillPoor = turns.stillPoor;
        }

        ProofreadOptions po;
        po.categories = { CheckCategory::Preparation, CheckCategory::Copying };
        r.findings = proofread(part, po).size();
        out.push_back(r);
    }
    return out;
}
}
