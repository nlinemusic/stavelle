/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "drumstems.h"

#include <map>

#include "engraving/dom/chord.h"
#include "engraving/dom/drumset.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"

using namespace mu::engraving;
using muse::String;

namespace mu::notation::jubelle {
namespace {
//! General MIDI drum numbers
DrumKind kindOf(int pitch)
{
    switch (pitch) {
    case 35: case 36: return DrumKind::Kick;
    case 37: case 38: case 40: return DrumKind::Snare;
    case 42: return DrumKind::ClosedHiHat;
    case 46: return DrumKind::OpenHiHat;
    case 44: return DrumKind::HiHatPedal;
    case 48: case 50: return DrumKind::HighTom;
    case 45: case 47: return DrumKind::MidTom;
    case 41: case 43: return DrumKind::FloorTom;
    case 51: case 53: case 59: return DrumKind::Ride;
    case 49: case 52: case 55: case 57: return DrumKind::Crash;
    case 54: return DrumKind::Tambourine;
    default: return DrumKind::Other;
    }
}

String nameOf(DrumKind kind)
{
    switch (kind) {
    case DrumKind::Kick: return u"Kick";
    case DrumKind::Snare: return u"Snare";
    case DrumKind::ClosedHiHat: return u"Closed hi-hat";
    case DrumKind::OpenHiHat: return u"Open hi-hat";
    case DrumKind::HiHatPedal: return u"Hi-hat pedal";
    case DrumKind::HighTom: return u"High tom";
    case DrumKind::MidTom: return u"Mid tom";
    case DrumKind::FloorTom: return u"Floor tom";
    case DrumKind::Ride: return u"Ride cymbal";
    case DrumKind::Crash: return u"Crash cymbals";
    case DrumKind::Tambourine: return u"Tambourine";
    default: return u"Percussion";
    }
}

bool isDrumPart(const Part* part)
{
    return part && part->instrument() && part->instrument()->useDrumset() && part->instrument()->drumset();
}

template<typename F>
void forEachNote(Score* score, const Part* part, F f)
{
    const track_idx_t from = part->startTrack();
    const track_idx_t to = part->endTrack();
    for (Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (track_idx_t t = from; t < to; ++t) {
            EngravingItem* e = seg->element(t);
            if (!e || !e->isChord()) {
                continue;
            }
            Chord* chord = toChord(e);
            for (Chord* grace : chord->graceNotes()) {
                for (Note* n : grace->notes()) {
                    f(n);
                }
            }
            for (Note* n : chord->notes()) {
                f(n);
            }
        }
    }
}
}

std::vector<Part*> drumParts(Score* score)
{
    std::vector<Part*> out;
    if (score) {
        for (Part* p : score->parts()) {
            if (isDrumPart(p)) {
                out.push_back(p);
            }
        }
    }
    return out;
}

std::vector<DrumComponent> drumComponents(Score* score, const Part* part)
{
    std::vector<DrumComponent> out;
    if (!score || !isDrumPart(part)) {
        return out;
    }
    std::map<int, int> notesByPitch;
    forEachNote(score, part, [&](Note* n) { ++notesByPitch[n->pitch()]; });

    const Drumset* kit = part->instrument()->drumset();
    std::map<int, size_t> index;   // component id -> position in out
    for (const auto& [pitch, count] : notesByPitch) {
        const DrumKind kind = kindOf(pitch);
        const int id = kind == DrumKind::Other ? 1000 + pitch : int(kind);
        auto it = index.find(id);
        if (it == index.end()) {
            DrumComponent c;
            c.id = id;
            c.kind = kind;
            c.name = nameOf(kind);
            if (kind == DrumKind::Other) {
                const String kitName = kit->isValid(pitch) ? kit->name(pitch) : String();
                c.name = kitName.isEmpty() ? String(u"Drum %1").arg(pitch) : kitName;
            }
            it = index.emplace(id, out.size()).first;
            out.push_back(c);
        }
        out[it->second].pitches.insert(pitch);
        out[it->second].notes += count;
    }
    // kit order: kick, snare, hi-hats, toms, cymbals, tambourine, then the rest by drum number
    std::stable_sort(out.begin(), out.end(), [](const DrumComponent& a, const DrumComponent& b) { return a.id < b.id; });
    return out;
}

std::vector<StemGroup> suggestedGroups(const std::vector<DrumComponent>& components)
{
    struct Plan {
        const char16_t* name;
        std::vector<DrumKind> kinds;
    };
    const std::vector<Plan> plans {
        { u"Kick", { DrumKind::Kick } },
        { u"Snare", { DrumKind::Snare } },
        { u"Hi-hat", { DrumKind::ClosedHiHat, DrumKind::OpenHiHat, DrumKind::HiHatPedal } },
        { u"Toms", { DrumKind::HighTom, DrumKind::MidTom, DrumKind::FloorTom } },
        { u"Cymbals", { DrumKind::Ride, DrumKind::Crash } },
        { u"Percussion", { DrumKind::Tambourine, DrumKind::Other } },
    };
    std::vector<StemGroup> out;
    for (const Plan& plan : plans) {
        StemGroup g;
        g.name = String(plan.name);
        for (const DrumComponent& c : components) {
            if (std::find(plan.kinds.begin(), plan.kinds.end(), c.kind) != plan.kinds.end()) {
                g.componentIds.push_back(c.id);
            }
        }
        if (!g.componentIds.empty()) {
            out.push_back(g);
        }
    }
    return out;
}

std::set<int> pitchesOf(const StemGroup& group, const std::vector<DrumComponent>& components)
{
    std::set<int> out;
    for (const DrumComponent& c : components) {
        if (std::find(group.componentIds.begin(), group.componentIds.end(), c.id) != group.componentIds.end()) {
            out.insert(c.pitches.begin(), c.pitches.end());
        }
    }
    return out;
}

int isolate(Score* score, const Part* part, const std::set<int>& pitches)
{
    if (!score || !isDrumPart(part)) {
        return 0;
    }
    int playing = 0;
    forEachNote(score, part, [&](Note* n) {
        if (pitches.count(n->pitch())) {
            playing += n->play();
        } else if (n->play()) {
            n->undoChangeProperty(Pid::PLAY, false);
        }
    });
    return playing;
}

String stemFileName(const String& name)
{
    String out;
    for (size_t i = 0; i < name.size(); ++i) {
        const char16_t ch = name.at(i).unicode();
        const bool bad = ch < 32 || ch == u'/' || ch == u'\\' || ch == u':' || ch == u'*' || ch == u'?' || ch == u'"' || ch == u'<'
                         || ch == u'>' || ch == u'|';
        out += bad ? u'_' : ch;
    }
    out = out.trimmed();
    while (!out.isEmpty() && out.at(out.size() - 1).unicode() == u'.') {
        out = out.left(out.size() - 1);
    }
    return out.isEmpty() ? String(u"Stem") : out;
}

// one group per line: name<TAB>id,id,id
String groupsToText(const std::vector<StemGroup>& groups)
{
    String out;
    for (const StemGroup& g : groups) {
        String name = g.name;
        name.replace(u"\t", u" ").replace(u"\n", u" ");
        out += name + u"\t";
        for (size_t i = 0; i < g.componentIds.size(); ++i) {
            out += (i ? u"," : u"") + String::number(g.componentIds[i]);
        }
        out += u"\n";
    }
    return out;
}

std::vector<StemGroup> groupsFromText(const String& text)
{
    std::vector<StemGroup> out;
    for (const String& line : text.split(u'\n')) {
        const size_t tab = line.indexOf(u'\t');
        if (line.trimmed().isEmpty() || tab == muse::nidx) {
            continue;
        }
        StemGroup g;
        g.name = line.left(tab);
        for (const String& id : line.mid(tab + 1).split(u',')) {
            if (!id.trimmed().isEmpty()) {
                g.componentIds.push_back(id.trimmed().toInt());
            }
        }
        out.push_back(g);
    }
    return out;
}
}
