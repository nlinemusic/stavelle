/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "articulationpassage.h"

#include <algorithm>
#include <climits>
#include <set>
#include <string>

#include "engraving/dom/articulation.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/chordline.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/select.h"
#include "engraving/dom/spanner.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/textline.h"
#include "engraving/types/symnames.h"

using namespace mu::engraving;
using namespace mu::engraving::jubelle;
using muse::String;

namespace mu::notation::jubelle {
namespace {
Part* masterPart(const Score* master, const Part* part)
{
    return part ? master->partById(part->id()) : nullptr;
}

void addPart(std::vector<Part*>& parts, Part* p)
{
    if (p && std::find(parts.begin(), parts.end(), p) == parts.end()) {
        parts.push_back(p);
    }
}

//! The passage line on this part covering exactly this range, if any
TextLine* exactLine(const Score* score, const Part* part, int start, int end)
{
    for (const auto& interval : score->spannerMap().findOverlapping(start, end)) {
        const TextLine* line = passageLine(interval.value);
        if (line && line->staff() && line->staff()->part() == part && line->tick().ticks() == start && line->tick2().ticks() == end) {
            return const_cast<TextLine*>(line);
        }
    }
    return nullptr;
}

String barsText(const Score* score, int start, int end)
{
    const Measure* a = score->tick2measure(Fraction::fromTicks(start));
    const Measure* b = score->tick2measure(Fraction::fromTicks(std::max(start, end - 1)));
    if (!a || !b) {
        return String();
    }
    return a == b ? String(u"bar %1").arg(a->no() + 1) : String(u"bars %1–%2").arg(a->no() + 1).arg(b->no() + 1);
}

//! Settings that matter for the articulations written in the passage
std::set<ArtParam> paramsInPassage(const Score* score, const PassageRange& range)
{
    std::set<ArtParam> out;
    for (const Part* part : range.parts) {
        for (Segment* s = score->tick2segment(Fraction::fromTicks(range.startTick), true, SegmentType::ChordRest); s
             && s->tick().ticks() < range.endTick; s = s->next1(SegmentType::ChordRest)) {
            for (track_idx_t t = part->startTrack(); t < part->endTrack(); ++t) {
                EngravingItem* e = s->element(t);
                if (!e || !e->isChord()) {
                    continue;
                }
                const Chord* c = toChord(e);
                for (const Articulation* a : c->articulations()) {
                    const std::string name(SymNames::nameForSymId(a->symId()).ascii());
                    if (name.find("Staccatissimo") != std::string::npos) {
                        out.insert(ArtParam::StaccatissimoLength);
                    } else if (name.find("Staccato") != std::string::npos) {
                        out.insert(ArtParam::StaccatoLength);
                    }
                    if (name.find("Tenuto") != std::string::npos) {
                        out.insert(ArtParam::TenutoLength);
                    }
                    if (name.find("Marcato") != std::string::npos) {
                        out.insert(ArtParam::MarcatoLength);
                        out.insert(ArtParam::MarcatoLoudness);
                    }
                    if (name.find("Accent") != std::string::npos) {
                        out.insert(ArtParam::AccentLoudness);
                    }
                    if (a->symId() == SymId::brassJazzTurn) {
                        out.insert(ArtParam::TurnSpeed);
                    }
                }
                if (const ChordLine* cl = c->chordLine()) {
                    if (cl->chordLineType() == ChordLineType::FALL || cl->chordLineType() == ChordLineType::DOIT) {
                        out.insert({ ArtParam::FallStart, ArtParam::FallRange, ArtParam::FallCurve });
                    } else {
                        out.insert({ ArtParam::ScoopLength, ArtParam::ScoopRange });
                    }
                }
                for (const Note* n : c->notes()) {
                    for (const Spanner* sp : n->spannerFor()) {
                        if (sp->isGlissando()) {
                            out.insert({ ArtParam::GlissandoStart, ArtParam::GlissandoCurve });
                        }
                    }
                }
            }
        }
    }
    return out;
}
}

PassageRange passageFromSelection(const Score* score)
{
    PassageRange r;
    if (!score || !score->masterScore()) {
        return r;
    }
    const MasterScore* master = score->masterScore();
    const Selection& sel = score->selection();

    // A passage line itself selected: its own passage
    if (sel.isSingle() || (sel.isList() && sel.elements().size() == 1)) {
        if (const TextLine* line = passageLine(sel.element() ? sel.element() : sel.elements().front())) {
            r.startTick = line->tick().ticks();
            r.endTick = line->tick2().ticks();
            addPart(r.parts, masterPart(master, line->part()));
            return r;
        }
    }

    if (sel.isRange()) {
        r.startTick = sel.tickStart().ticks();
        r.endTick = sel.tickEnd().ticks();
        for (staff_idx_t i = sel.staffStart(); i < sel.staffEnd(); ++i) {
            addPart(r.parts, masterPart(master, score->staff(i)->part()));
        }
        return r;
    }
    if (sel.isList()) {
        int start = INT_MAX;
        int end = 0;
        for (EngravingItem* e : sel.elements()) {
            const EngravingItem* cr = e->isNote() ? toNote(e)->chord() : e;
            if (!cr->isChordRest()) {
                continue;
            }
            start = std::min(start, cr->tick().ticks());
            end = std::max(end, (cr->tick() + toChordRest(cr)->actualTicks()).ticks());
            addPart(r.parts, masterPart(master, cr->part()));
        }
        if (start < end) {
            r.startTick = start;
            r.endTick = end;
        }
    }
    return r;
}

std::vector<PassageRow> passageRows(const Score* score, const PassageRange& range)
{
    std::vector<PassageRow> rows;
    if (!score || !range.isValid()) {
        return rows;
    }
    const MasterScore* master = score->masterScore();
    const Part* part = range.parts.front();
    const ArticulationPlaybackSettings settings = ArticulationPlaybackSettings::read(master);
    const std::set<ArtParam> present = paramsInPassage(master, range);

    const TextLine* here = exactLine(master, part, range.startTick, range.endTick);
    const std::optional<ArticulationPlaybackValues> hereValues = here ? parsePassageText(here->beginText()) : std::nullopt;

    std::vector<PassageAt> others;
    for (const PassageAt& p : passagesAt(master, part, range.startTick)) {
        if (p.line != here) {
            others.push_back(p);
        }
    }
    const auto instrument = settings.partValues.find(part->id().toUint64());

    for (const ArtParamInfo& info : artParams()) {
        PassageRow row;
        row.param = info.param;
        row.inSelection = present.count(info.param) > 0;
        row.value = info.defaultValue;
        if (hereValues && hereValues->get(info.param)) {
            row.value = *hereValues->get(info.param);
            row.source = ValueSource::Here;
        } else {
            // The latest-starting other passage that sets it
            const PassageAt* from = nullptr;
            for (const PassageAt& p : others) {
                if (p.values.get(info.param)) {
                    from = &p;
                }
            }
            if (from) {
                row.value = *from->values.get(info.param);
                row.source = ValueSource::OtherPassage;
                row.sourceName = barsText(master, from->line->tick().ticks(), from->line->tick2().ticks());
            } else if (instrument != settings.partValues.end() && instrument->second.get(info.param)) {
                row.value = *instrument->second.get(info.param);
                row.source = ValueSource::Instrument;
                row.sourceName = part->partName();
            } else if (settings.scoreValues.get(info.param)) {
                row.value = *settings.scoreValues.get(info.param);
                row.source = ValueSource::WholeScore;
            }
        }
        rows.push_back(row);
    }
    return rows;
}

void setPassageValue(Score* score, const PassageRange& range, ArtParam param, std::optional<int> value)
{
    if (!score || !range.isValid()) {
        return;
    }
    MasterScore* master = score->masterScore();
    for (Part* part : range.parts) {
        TextLine* line = exactLine(master, part, range.startTick, range.endTick);
        ArticulationPlaybackValues values;
        if (line) {
            values = parsePassageText(line->beginText()).value_or(ArticulationPlaybackValues());
        }
        values.set(param, value);

        if (values.isEmpty()) {
            if (line) {
                master->undoRemoveElement(line);   // nothing left: back to the inherited values
            }
            continue;
        }
        const String text = passageText(values);
        if (line) {
            if (line->beginText() != text) {
                static_cast<EngravingItem*>(line)->undoChangeProperty(Pid::BEGIN_TEXT, PropertyValue(text));
            }
            continue;
        }
        if (!value) {
            continue;
        }
        // A new hidden line on the instrument's first staff, over the passage
        TextLine* tl = Factory::createTextLine(master->dummy());
        const track_idx_t track = part->startTrack();
        tl->setTrack(track);
        tl->setTrack2(track);
        tl->setTick(Fraction::fromTicks(range.startTick));
        tl->setTick2(Fraction::fromTicks(range.endTick));
        tl->setBeginText(text);
        tl->setVisible(false);   // never printed; grey on screen with View → Show → Invisible
        master->undoAddElement(tl);
    }
}

void clearPassage(Score* score, const PassageRange& range)
{
    if (!score || !range.isValid()) {
        return;
    }
    MasterScore* master = score->masterScore();
    for (Part* part : range.parts) {
        if (TextLine* line = exactLine(master, part, range.startTick, range.endTick)) {
            master->undoRemoveElement(line);
        }
    }
}
}
