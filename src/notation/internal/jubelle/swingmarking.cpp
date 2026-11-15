/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "swingmarking.h"

#include <algorithm>

#include "engraving/dom/factory.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/systemtext.h"
#include "engraving/dom/tempo.h"
#include "engraving/dom/tempotext.h"
#include "engraving/dom/tripletfeel.h"
#include "engraving/infrastructure/eidregister.h"

using namespace mu::engraving;
using namespace mu::engraving::jubelle;

namespace mu::notation::jubelle {
//! Where the marking goes: the first tempo marking's place, else the start
static Segment* markingSegment(const Score* score)
{
    const Measure* first = score->firstMeasure();
    if (!first) {
        return nullptr;
    }
    for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        for (EngravingItem* a : s->annotations()) {
            if (a->isTempoText()) {
                return s;
            }
        }
    }
    return first->first(SegmentType::ChordRest);
}

String swingMarkingXmlText(const Score* score, const SwingSettings& settings, const String& feelName)
{
    if (settings.markingStyle == SwingMarkingStyle::Off || settings.scoreFeel.isEmpty() || settings.scoreFeel == u"straight") {
        return String();
    }
    const SwingFeel* feel = settings.feel(settings.scoreFeel);
    if (!feel) {
        return String();
    }

    const String name = feelName.isEmpty() ? feel->name : feelName;
    if (settings.markingStyle == SwingMarkingStyle::Custom) {
        return settings.markingText.isEmpty() ? name.toXmlEscaped() : settings.markingText.toXmlEscaped();
    }
    if (settings.markingStyle == SwingMarkingStyle::Text) {
        return name.toXmlEscaped();
    }

    // Text and equation: triplet swing, or dotted for very heavy swing, at
    // the tempo where the marking stands
    const Segment* seg = markingSegment(score);
    const double bpm = seg ? score->tempomap()->tempo(seg->tick().ticks()).val * 60.0 : 120.0;
    const bool dotted = feel->ratioAt(bpm) >= 2.75;
    TripletFeelType type;
    if (feel->unit == 16) {
        type = dotted ? TripletFeelType::DOTTED_16TH : TripletFeelType::TRIPLET_16TH;
    } else {
        type = dotted ? TripletFeelType::DOTTED_8TH : TripletFeelType::TRIPLET_8TH;
    }
    return name.toXmlEscaped() + u" " + TripletFeel::equationXmlText(type);
}

SystemText* findSwingMarking(const Score* score, const SwingSettings& settings)
{
    if (!score || settings.markingEid.isEmpty()) {
        return nullptr;
    }
    const EID eid = EID::fromStdString(settings.markingEid.toStdString());
    if (!eid.isValid()) {
        return nullptr;
    }
    EngravingObject* obj = score->masterScore()->eidRegister()->itemFromEID(eid);
    if (!obj || !obj->isSystemText()) {
        return nullptr;
    }
    // Still in the score? (a deleted marking stays known to undo)
    SystemText* text = toSystemText(obj);
    const Segment* seg = text->explicitParent() && text->explicitParent()->isSegment() ? toSegment(text->explicitParent()) : nullptr;
    if (!seg || seg->score() != score->masterScore()) {
        return nullptr;
    }
    const auto& annotations = seg->annotations();
    if (std::find(annotations.begin(), annotations.end(), text) == annotations.end()) {
        return nullptr;
    }
    return text;
}

SwingMarkingResult updateSwingMarking(Score* score, SwingSettings& settings, const String& feelName)
{
    SwingMarkingResult result;
    if (!score || !score->masterScore()) {
        return result;
    }
    MasterScore* master = score->masterScore();

    SystemText* existing = findSwingMarking(master, settings);
    const String text = swingMarkingXmlText(master, settings, feelName);
    Segment* seg = markingSegment(master);

    if (text.isEmpty() || !seg) {
        if (existing) {
            master->undoRemoveElement(existing);
            result.removed = true;
        }
        settings.markingEid.clear();
        return result;
    }

    if (existing && existing->explicitParent() == seg) {
        if (existing->xmlText() != text) {
            existing->undoChangeProperty(Pid::TEXT, text);
            result.updated = true;
        }
        // R0 (15 Nov 2026): markings made before used the small system-text style
        if (existing->textStyleType() != TextStyleType::TEMPO) {
            existing->undoChangeProperty(Pid::TEXT_STYLE, TextStyleType::TEMPO);
            result.updated = true;
        }
        return result;
    }

    if (existing) {
        // The first tempo marking moved: the marking follows it
        master->undoRemoveElement(existing);
    }

    // Tempo-marking style (R0, 15 Nov 2026): the note symbols of the equation
    // are drawn at metronome-mark size, matching the tempo marking beside it
    SystemText* marking = Factory::createSystemText(seg, TextStyleType::TEMPO);
    marking->setParent(seg);
    marking->setTrack(0);
    marking->setXmlText(text);
    marking->setPlacement(PlacementV::ABOVE);
    master->undoAddElement(marking);

    EID eid = marking->eid();
    if (!eid.isValid()) {
        eid = marking->assignNewEID();
    }
    settings.markingEid = String::fromStdString(eid.toStdString());
    if (existing) {
        result.updated = true;
    } else {
        result.added = true;
    }
    return result;
}
}
