/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "linkedpassages.h"

#include <algorithm>
#include <map>
#include <sstream>

#include "engraving/dom/articulation.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/slur.h"
#include "engraving/dom/spanner.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/textline.h"
#include "engraving/dom/tie.h"
#include "engraving/types/symnames.h"
#include "engraving/types/typesconv.h"

using namespace mu::engraving;
using namespace mu::notation::passagelink;
using PEvent = mu::notation::passagelink::Event;
using muse::String;

namespace mu::notation::jubelle {
namespace {
std::vector<String> s_messages;
// Set while Jubelle itself rewrites a target (Re-sync), so the change isn't
// taken for a hand edit
bool s_ownEdit = false;

//! An articulation's name without its placement ("articStaccatoAbove" and
//! "articStaccatoBelow" are the same articulation; MuseScore flips them by stem)
std::string articulationName(SymId id)
{
    std::string name(SymNames::nameForSymId(id).ascii());
    for (const char* suffix : { "Above", "Below" }) {
        const std::string s(suffix);
        if (name.size() > s.size() && name.compare(name.size() - s.size(), s.size(), s) == 0) {
            name.erase(name.size() - s.size());
            break;
        }
    }
    return name;
}

std::string eidOf(const EngravingItem* item)
{
    EID eid = item->eid();
    if (!eid.isValid()) {
        eid = item->assignNewEID();
    }
    return eid.toStdString();
}

// ---- line text ---------------------------------------------------------------

String syncText(const SyncOptions& o)
{
    std::vector<std::string> parts;
    if (o.rhythm) {
        parts.push_back("rhythm");
    }
    if (o.durations) {
        parts.push_back("durations");
    }
    if (o.articulations) {
        parts.push_back("articulations");
    }
    if (o.dynamics) {
        parts.push_back("dynamics");
    }
    if (o.markings) {
        parts.push_back("markings");
    }
    std::string s;
    for (size_t i = 0; i < parts.size(); ++i) {
        s += (i ? "," : "") + parts[i];
    }
    return String::fromStdString(s.empty() ? "none" : s);
}

String memberText(int group, bool source, const SyncOptions& o, const std::set<std::string>& handEdited)
{
    std::ostringstream out;
    out << "Jubelle link: group=" << group << " role=" << (source ? "source" : "target");
    if (source) {
        out << " sync=" << syncText(o).toStdString();
    } else if (!handEdited.empty()) {
        out << " own=";
        bool first = true;
        for (const std::string& e : handEdited) {
            out << (first ? "" : ";") << e;
            first = false;
        }
    }
    return String::fromStdString(out.str());
}

struct ParsedText {
    int group = 0;
    bool source = false;
    SyncOptions sync;
    std::set<std::string> handEdited;
};

std::optional<ParsedText> parseText(const String& text)
{
    const std::string t = text.toStdString();
    const std::string prefix = String(LINK_PREFIX).toStdString();
    const size_t at = t.find(prefix);
    if (at == std::string::npos) {
        return std::nullopt;
    }
    ParsedText p;
    std::istringstream in(t.substr(at + prefix.size()));
    std::string token;
    while (in >> token) {
        const size_t eq = token.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = token.substr(0, eq);
        const std::string v = token.substr(eq + 1);
        if (key == "group") {
            try {
                p.group = std::stoi(v);
            } catch (...) {
            }
        } else if (key == "role") {
            p.source = v == "source";
        } else if (key == "sync") {
            p.sync.rhythm = v.find("rhythm") != std::string::npos;
            p.sync.durations = v.find("durations") != std::string::npos;
            p.sync.articulations = v.find("articulations") != std::string::npos;
            p.sync.dynamics = v.find("dynamics") != std::string::npos;
            p.sync.markings = v.find("markings") != std::string::npos;
        } else if (key == "own") {
            std::istringstream ids(v);
            std::string id;
            while (std::getline(ids, id, ';')) {
                if (!id.empty()) {
                    p.handEdited.insert(id);
                }
            }
        }
    }
    if (p.group <= 0) {
        return std::nullopt;
    }
    return p;
}

TextLine* lineOf(const EngravingItem* item)
{
    if (!item) {
        return nullptr;
    }
    if (item->isTextLineSegment()) {
        item = toTextLineSegment(item)->textLine();
    }
    if (!item || !item->isTextLine()) {
        return nullptr;
    }
    TextLine* line = const_cast<TextLine*>(toTextLine(item));
    return line->beginText().contains(String(LINK_PREFIX)) ? line : nullptr;
}

void setLineText(TextLine* line, const String& text)
{
    if (line->beginText() != text) {
        static_cast<EngravingItem*>(line)->undoChangeProperty(Pid::BEGIN_TEXT, PropertyValue(text));
    }
}

TextLine* addLine(MasterScore* master, size_t staffIdx, int start, int end, const String& text)
{
    TextLine* tl = Factory::createTextLine(master->dummy());
    const track_idx_t track = staffIdx * VOICES;
    tl->setTrack(track);
    tl->setTrack2(track);
    tl->setTick(Fraction::fromTicks(start));
    tl->setTick2(Fraction::fromTicks(end));
    tl->setBeginText(text);
    tl->setVisible(false);   // never printed; grey on screen with View → Show → Invisible
    master->undoAddElement(tl);
    return tl;
}

// ---- reading music --------------------------------------------------------------

std::vector<ChordRest*> chordRests(const Score* score, size_t staffIdx, int start, int end)
{
    std::vector<ChordRest*> out;
    const track_idx_t track = staffIdx * VOICES;
    for (Segment* s = score->tick2segment(Fraction::fromTicks(start), true, SegmentType::ChordRest); s && s->tick().ticks() < end;
         s = s->next1(SegmentType::ChordRest)) {
        if (EngravingItem* e = s->element(track)) {
            out.push_back(toChordRest(e));
        }
    }
    return out;
}

std::vector<const Slur*> slursIn(const Score* score, size_t staffIdx, int start, int end)
{
    std::vector<const Slur*> out;
    for (const auto& iv : score->spannerMap().findOverlapping(start, end)) {
        const Spanner* sp = iv.value;
        if (sp->isSlur() && sp->track() == staffIdx * VOICES && sp->tick().ticks() >= start && sp->tick().ticks() < end) {
            out.push_back(toSlur(sp));
        }
    }
    return out;
}

bool hasTuplets(const std::vector<ChordRest*>& crs)
{
    return std::any_of(crs.begin(), crs.end(), [](const ChordRest* cr) { return cr->tuplet() != nullptr; });
}

bool hasGraceNotes(const std::vector<ChordRest*>& crs)
{
    return std::any_of(crs.begin(), crs.end(), [](const ChordRest* cr) {
        return cr->isChord() && !toChord(cr)->graceNotes().empty();
    });
}

ChordRest* crAt(const Score* score, size_t staffIdx, int tick)
{
    Segment* s = score->tick2segment(Fraction::fromTicks(tick), true, SegmentType::ChordRest);
    if (!s || s->tick().ticks() != tick) {
        return nullptr;
    }
    EngravingItem* e = s->element(staffIdx * VOICES);
    return e ? toChordRest(e) : nullptr;
}
}

Passage readPassage(const Score* score, size_t staffIdx, int start, int end, const std::set<std::string>& handEdited)
{
    Passage p;
    const std::vector<const Slur*> slurs = slursIn(score, staffIdx, start, end);
    for (ChordRest* cr : chordRests(score, staffIdx, start, end)) {
        PEvent e;
        e.tick = cr->tick().ticks() - start;
        e.duration = std::min(cr->actualTicks().ticks(), end - cr->tick().ticks());
        e.detached = handEdited.count(eidOf(cr)) > 0;
        if (cr->isChord()) {
            const Chord* c = toChord(cr);
            for (const Note* n : c->notes()) {
                e.pitches.push_back(n->pitch());
                if (n->tieFor()) {
                    e.markings.insert("tie");
                }
            }
            std::sort(e.pitches.begin(), e.pitches.end());
            for (const Articulation* a : c->articulations()) {
                e.articulations.insert(articulationName(a->symId()));
            }
            for (const Slur* s : slurs) {
                if (s->startElement() == c) {
                    e.markings.insert("slur-start");
                }
                if (s->endElement() == c) {
                    e.markings.insert("slur-end");
                }
            }
        }
        for (const EngravingItem* a : cr->segment()->annotations()) {
            if (a->isDynamic() && a->track() == cr->track()) {
                e.dynamic = std::string(TConv::toXml(toDynamic(a)->dynamicType()).ascii());
            }
        }
        p.push_back(e);
    }
    return p;
}

// ---- writing music --------------------------------------------------------------

namespace {
bool sameRhythmAndPitches(const Passage& a, const Passage& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].tick != b[i].tick || a[i].duration != b[i].duration || a[i].pitches != b[i].pitches) {
            return false;
        }
    }
    return true;
}

//! Rests wherever voice 1 has no chord or rest in [start, end)
void fillHoles(MasterScore* score, size_t staffIdx, int start, int end)
{
    const track_idx_t track = staffIdx * VOICES;
    for (Measure* m = score->tick2measure(Fraction::fromTicks(start)); m && m->tick().ticks() < end; m = m->nextMeasure()) {
        const int from = std::max(start, m->tick().ticks());
        const int to = std::min(end, m->endTick().ticks());
        int pos = from;
        for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            EngravingItem* e = s->element(track);
            if (!e) {
                continue;
            }
            const int t = s->tick().ticks();
            const int te = t + toChordRest(e)->actualTicks().ticks();
            if (te <= from || t >= to) {
                continue;
            }
            if (t > pos) {
                score->setRest(Fraction::fromTicks(pos), track, Fraction::fromTicks(t - pos), true, nullptr, false);
            }
            pos = std::max(pos, te);
        }
        if (pos < to) {
            const bool wholeBar = pos == m->tick().ticks() && to == m->endTick().ticks();
            score->setRest(Fraction::fromTicks(pos), track, Fraction::fromTicks(to - pos), true, nullptr, wholeBar);
        }
    }
}

void writeRhythmAndPitches(MasterScore* score, size_t staffIdx, int start, int end, const Passage& desired,
                           const std::set<std::string>& handEdited)
{
    const track_idx_t track = staffIdx * VOICES;

    // Slurs on the rebuilt notes go; they come back from the passage's markings
    for (const Slur* s : slursIn(score, staffIdx, start, end)) {
        score->undoRemoveElement(const_cast<Slur*>(s));
    }
    // Everything except your hand-edited notes is rewritten
    for (ChordRest* cr : chordRests(score, staffIdx, start, end)) {
        if (!handEdited.count(eidOf(cr))) {
            score->undoRemoveElement(cr);
        }
    }

    for (const PEvent& e : desired) {
        if (e.detached || e.pitches.empty()) {
            continue;   // kept as they are / rests are filled in below
        }
        const int tick = start + e.tick;
        const Fraction len = Fraction::fromTicks(e.duration);
        score->setRest(Fraction::fromTicks(tick), track, len, true, nullptr, false);
        Segment* seg = score->tick2segment(Fraction::fromTicks(tick), true, SegmentType::ChordRest);
        if (!seg) {
            continue;
        }
        score->setNoteRest(seg, track, NoteVal(e.pitches.front()), len);
        // The other notes of the chord, on every chord of a tied chain
        // (a note across a bar line becomes tied notes)
        std::vector<Chord*> chain;
        EngravingItem* el = seg->element(track);
        for (Chord* c = el && el->isChord() ? toChord(el) : nullptr; c;) {
            chain.push_back(c);
            Note* f = c->notes().front();
            c = f->tieFor() && f->tieFor()->endNote() ? f->tieFor()->endNote()->chord() : nullptr;
        }
        for (Chord* c : chain) {
            for (size_t i = 1; i < e.pitches.size(); ++i) {
                score->addNote(c, NoteVal(e.pitches[i]));
            }
        }
        for (size_t k = 0; k + 1 < chain.size(); ++k) {
            for (Note* n : chain[k]->notes()) {
                if (n->tieFor()) {
                    continue;
                }
                for (Note* m : chain[k + 1]->notes()) {
                    if (m->pitch() == n->pitch() && !m->tieBack()) {
                        Tie* tie = Factory::createTie(n);
                        tie->setStartNote(n);
                        tie->setEndNote(m);
                        tie->setTrack(n->track());
                        tie->setTick(n->tick());
                        tie->setTick2(m->tick());
                        score->undoAddElement(tie);
                        break;
                    }
                }
            }
        }
    }
    fillHoles(score, staffIdx, start, end);
}

void writeDetails(MasterScore* score, size_t staffIdx, int start, const Passage& desired, const SyncOptions& sync)
{
    std::vector<std::pair<Chord*, Chord*> > wantedSlurs;
    Chord* openSlur = nullptr;

    for (const PEvent& e : desired) {
        if (e.detached || e.pitches.empty()) {
            continue;
        }
        ChordRest* cr = crAt(score, staffIdx, start + e.tick);
        if (!cr || !cr->isChord()) {
            continue;
        }
        Chord* chord = toChord(cr);

        if (sync.articulations) {
            std::set<std::string> have;
            for (Articulation* a : std::vector<Articulation*>(chord->articulations().begin(), chord->articulations().end())) {
                const std::string name = articulationName(a->symId());
                if (!e.articulations.count(name)) {
                    score->undoRemoveElement(a);
                } else {
                    have.insert(name);
                }
            }
            for (const std::string& name : e.articulations) {
                if (have.count(name)) {
                    continue;
                }
                SymId id = SymNames::symIdByName(muse::AsciiStringView((name + "Above").c_str()));
                if (id == SymId::noSym) {
                    id = SymNames::symIdByName(muse::AsciiStringView(name.c_str()));
                }
                if (id == SymId::noSym) {
                    continue;
                }
                Articulation* a = Factory::createArticulation(chord);
                a->setSymId(id);
                a->setParent(chord);
                a->setTrack(chord->track());
                score->undoAddElement(a);
            }
        }

        if (sync.dynamics) {
            Dynamic* existing = nullptr;
            for (EngravingItem* a : chord->segment()->annotations()) {
                if (a->isDynamic() && a->track() == chord->track()) {
                    existing = toDynamic(a);
                }
            }
            const std::string have = existing ? std::string(TConv::toXml(existing->dynamicType()).ascii()) : std::string();
            if (have != e.dynamic) {
                if (existing) {
                    score->undoRemoveElement(existing);
                }
                if (!e.dynamic.empty()) {
                    Dynamic* d = Factory::createDynamic(chord->segment());
                    d->setDynamicType(TConv::fromXml(muse::AsciiStringView(e.dynamic.c_str()), DynamicType::OTHER));
                    d->setXmlText(Dynamic::dynamicText(d->dynamicType()));
                    d->setParent(chord->segment());
                    d->setTrack(chord->track());
                    score->undoAddElement(d);
                }
            }
        }

        if (sync.markings) {
            if (e.markings.count("slur-start")) {
                openSlur = chord;
            }
            if (e.markings.count("slur-end") && openSlur && openSlur != chord) {
                wantedSlurs.push_back({ openSlur, chord });
                openSlur = nullptr;
            }
            // ties: to the next chord with the same pitch
            const bool wantTie = e.markings.count("tie") > 0;
            ChordRest* nextCr = crAt(score, staffIdx, chord->tick().ticks() + chord->actualTicks().ticks());
            Chord* next = nextCr && nextCr->isChord() ? toChord(nextCr) : nullptr;
            for (Note* n : chord->notes()) {
                if (n->tieFor() && !wantTie) {
                    score->undoRemoveElement(n->tieFor());
                } else if (!n->tieFor() && wantTie && next) {
                    for (Note* m : next->notes()) {
                        if (m->pitch() == n->pitch() && !m->tieBack()) {
                            Tie* tie = Factory::createTie(n);
                            tie->setStartNote(n);
                            tie->setEndNote(m);
                            tie->setTrack(n->track());
                            tie->setTick(n->tick());
                            tie->setTick2(m->tick());
                            score->undoAddElement(tie);
                            break;
                        }
                    }
                }
            }
        }
    }

    if (sync.markings && !desired.empty()) {
        const int end = start + desired.back().tick + desired.back().duration;
        std::vector<const Slur*> existing = slursIn(score, staffIdx, start, end);
        for (const auto& [a, b] : wantedSlurs) {
            const bool there = std::any_of(existing.begin(), existing.end(), [&](const Slur* s) {
                return s->startElement() == a && s->endElement() == b;
            });
            if (there) {
                continue;
            }
            Slur* slur = Factory::createSlur(score->dummy());
            slur->setTrack(a->track());
            slur->setTrack2(b->track());
            slur->setTick(a->tick());
            slur->setTick2(b->tick());
            slur->setStartElement(a);
            slur->setEndElement(b);
            score->undoAddElement(slur);
        }
        for (const Slur* s : existing) {
            const bool wanted = std::any_of(wantedSlurs.begin(), wantedSlurs.end(), [&](const auto& w) {
                return s->startElement() == w.first && s->endElement() == w.second;
            });
            const bool ownNotes = std::any_of(desired.begin(), desired.end(), [&](const PEvent& e) {
                return e.detached && start + e.tick == s->tick().ticks();
            });
            if (!wanted && !ownNotes) {
                score->undoRemoveElement(const_cast<Slur*>(s));
            }
        }
    }
}

//! Writes a target passage. Returns false (with a message) if it can't be
//! done safely, in which case nothing is changed.
bool writeTarget(MasterScore* score, const LinkMember& target, const Passage& desired, const SyncOptions& sync)
{
    const std::vector<ChordRest*> crs = chordRests(score, target.staffIdx, target.startTick, target.endTick);
    const Passage now = readPassage(score, target.staffIdx, target.startTick, target.endTick, target.handEdited);
    if (!sameRhythmAndPitches(now, desired)) {
        if (hasTuplets(crs) || std::any_of(desired.begin(), desired.end(), [](const PEvent& e) {
            return e.duration <= 0;
        })) {
            s_messages.push_back(String(u"A linked passage on staff %1 wasn't updated: it contains tuplets, "
                                        "which linked rhythm changes don't handle yet.").arg(int(target.staffIdx) + 1));
            return false;
        }
        if (hasGraceNotes(crs)) {
            s_messages.push_back(String(u"A linked passage on staff %1 wasn't updated: it contains grace notes, "
                                        "which would be lost.").arg(int(target.staffIdx) + 1));
            return false;
        }
        writeRhythmAndPitches(score, target.staffIdx, target.startTick, target.endTick, desired, target.handEdited);
    }
    writeDetails(score, target.staffIdx, target.startTick, desired, sync);
    return true;
}

//! The ids of the chords and rests at the given ticks of a passage
std::set<std::string> idsAt(const Score* score, const LinkMember& m, const std::set<int>& relTicks)
{
    std::set<std::string> out;
    for (ChordRest* cr : chordRests(score, m.staffIdx, m.startTick, m.endTick)) {
        const int rel = cr->tick().ticks() - m.startTick;
        const int relEnd = rel + cr->actualTicks().ticks();
        for (int t : relTicks) {
            if (t >= rel && t < relEnd) {
                out.insert(eidOf(cr));
            }
        }
    }
    return out;
}

bool sameEvent(const PEvent& a, const PEvent& b)
{
    return a.tick == b.tick && a.duration == b.duration && a.pitches == b.pitches && a.articulations == b.articulations
           && a.dynamic == b.dynamic && a.markings == b.markings;
}

//! Ticks (relative) where `after` differs from `before`
std::set<int> changedTicks(const Passage& before, const Passage& after)
{
    std::set<int> out;
    std::map<int, const PEvent*> b;
    for (const PEvent& e : before) {
        b[e.tick] = &e;
    }
    std::map<int, const PEvent*> a;
    for (const PEvent& e : after) {
        a[e.tick] = &e;
    }
    for (const auto& [t, e] : a) {
        auto it = b.find(t);
        if (it == b.end() || !sameEvent(*it->second, *e)) {
            out.insert(t);
        }
    }
    for (const auto& [t, e] : b) {
        if (!a.count(t)) {
            out.insert(t);
        }
    }
    return out;
}

// ---- the hook: edits follow links ------------------------------------------------

struct GroupSnapshot {
    Passage source;
    std::map<TextLine*, Passage> targets;
};

class LinkedPassagesHook : public Score::JubelleCmdHook
{
public:
    void beforeCmd(Score* score) override
    {
        m_snapshots.clear();
        MasterScore* master = score ? score->masterScore() : nullptr;
        if (!master) {
            return;
        }
        for (const LinkGroup& g : linkGroups(master)) {
            GroupSnapshot snap;
            snap.source = readPassage(master, g.source.staffIdx, g.source.startTick, g.source.endTick);
            for (const LinkMember& t : g.targets) {
                snap.targets[t.line] = readPassage(master, t.staffIdx, t.startTick, t.endTick, t.handEdited);
            }
            m_snapshots[g.id] = snap;
        }
        m_master = master;
    }

    void afterCmd(Score* score) override
    {
        MasterScore* master = score ? score->masterScore() : nullptr;
        if (!master || master != m_master || m_snapshots.empty() || m_busy) {
            return;
        }
        if (s_ownEdit) {
            s_ownEdit = false;
            m_snapshots.clear();
            return;
        }
        m_busy = true;
        s_messages.clear();
        for (const LinkGroup& g : linkGroups(master)) {
            auto snapIt = m_snapshots.find(g.id);
            if (snapIt == m_snapshots.end()) {
                continue;
            }
            const GroupSnapshot& snap = snapIt->second;
            const Passage sourceNow = readPassage(master, g.source.staffIdx, g.source.startTick, g.source.endTick);
            const bool sourceChanged = !changedTicks(snap.source, sourceNow).empty()
                                       || snap.source.size() != sourceNow.size();

            for (const LinkMember& t : g.targets) {
                auto tb = snap.targets.find(t.line);
                if (tb == snap.targets.end()) {
                    continue;
                }
                // Notes you changed by hand in a target become your own
                const Passage targetNow = readPassage(master, t.staffIdx, t.startTick, t.endTick, t.handEdited);
                const std::set<int> handTicks = changedTicks(tb->second, targetNow);
                LinkMember member = t;
                if (!handTicks.empty()) {
                    const std::set<std::string> ids = idsAt(master, t, handTicks);
                    member.handEdited.insert(ids.begin(), ids.end());
                    setLineText(t.line, memberText(g.id, false, g.sync, member.handEdited));
                }
                if (!sourceChanged) {
                    continue;
                }
                const Passage current = readPassage(master, t.staffIdx, t.startTick, t.endTick, member.handEdited);
                const Passage desired = propagate(snap.source, sourceNow, current, g.sync);
                writeTarget(master, member, desired, g.sync);
                // Hand-edited ids still valid after a rewrite (kept chords keep theirs)
            }
        }
        m_busy = false;
        m_snapshots.clear();
    }

private:
    std::map<int, GroupSnapshot> m_snapshots;
    MasterScore* m_master = nullptr;
    bool m_busy = false;
};
}

// ---- public API --------------------------------------------------------------------

const std::vector<String>& lastLinkMessages()
{
    return s_messages;
}

std::optional<LinkMember> linkMember(const EngravingItem* item)
{
    TextLine* line = lineOf(item);
    if (!line) {
        return std::nullopt;
    }
    const std::optional<ParsedText> p = parseText(line->beginText());
    if (!p) {
        return std::nullopt;
    }
    LinkMember m;
    m.line = line;
    m.staffIdx = line->staffIdx();
    m.startTick = line->tick().ticks();
    m.endTick = line->tick2().ticks();
    m.source = p->source;
    m.handEdited = p->handEdited;
    return m;
}

std::vector<LinkGroup> linkGroups(const Score* score)
{
    std::map<int, LinkGroup> groups;
    if (!score || !score->masterScore()) {
        return {};
    }
    const MasterScore* master = score->masterScore();
    for (const auto& [tick, sp] : master->spanner()) {
        TextLine* line = lineOf(sp);
        if (!line) {
            continue;
        }
        const std::optional<ParsedText> p = parseText(line->beginText());
        const std::optional<LinkMember> m = linkMember(line);
        if (!p || !m) {
            continue;
        }
        LinkGroup& g = groups[p->group];
        g.id = p->group;
        // A copy of a linked passage pasted elsewhere on the same staff isn't a
        // second member: the earliest one counts (after cut and paste, the
        // pasted one is the only one left, so the link moves with the music)
        const bool sameStaffTaken = (g.source.line && g.source.staffIdx == m->staffIdx)
                                    || std::any_of(g.targets.begin(), g.targets.end(), [&](const LinkMember& t) {
            return t.staffIdx == m->staffIdx;
        });
        if (sameStaffTaken) {
            continue;
        }
        if (p->source && !g.source.line) {
            g.source = *m;
            g.sync = p->sync;
        } else if (!p->source) {
            g.targets.push_back(*m);
        }
    }
    std::vector<LinkGroup> out;
    for (auto& [id, g] : groups) {
        if (g.source.line && !g.targets.empty()) {
            std::sort(g.targets.begin(), g.targets.end(), [](const LinkMember& a, const LinkMember& b) { return a.staffIdx < b.staffIdx; });
            out.push_back(g);
        }
    }
    return out;
}

int linkPassages(Score* score, size_t sourceStaff, int startTick, int endTick, const std::vector<size_t>& targetStaves,
                 const SyncOptions& sync)
{
    if (!score || endTick <= startTick || targetStaves.empty()) {
        return 0;
    }
    MasterScore* master = score->masterScore();
    int id = 1;
    for (const auto& [tick, sp] : master->spanner()) {
        if (TextLine* line = lineOf(sp)) {
            if (const auto p = parseText(line->beginText())) {
                id = std::max(id, p->group + 1);
            }
        }
    }
    std::vector<size_t> targets;
    for (size_t t : targetStaves) {
        if (t != sourceStaff && t < master->nstaves() && std::find(targets.begin(), targets.end(), t) == targets.end()) {
            targets.push_back(t);
        }
    }
    if (targets.empty()) {
        return 0;
    }
    addLine(master, sourceStaff, startTick, endTick, memberText(id, true, sync, {}));
    for (size_t t : targets) {
        addLine(master, t, startTick, endTick, memberText(id, false, sync, {}));
    }
    return id;
}

void unlinkMember(Score* score, TextLine* memberLine)
{
    const std::optional<LinkMember> m = linkMember(memberLine);
    if (!score || !m) {
        return;
    }
    MasterScore* master = score->masterScore();
    const std::optional<ParsedText> p = parseText(m->line->beginText());
    for (const LinkGroup& g : linkGroups(master)) {
        if (g.id != p->group) {
            continue;
        }
        if (m->source) {
            // The first target becomes the source, keeping the group's options
            const LinkMember& heir = g.targets.front();
            setLineText(heir.line, memberText(g.id, true, g.sync, {}));
            if (g.targets.size() == 1) {
                master->undoRemoveElement(heir.line);   // nothing left to link to
            }
        } else if (g.targets.size() == 1) {
            master->undoRemoveElement(g.source.line);    // the source has nothing left either
        }
    }
    master->undoRemoveElement(m->line);
}

void setSyncOptions(Score* score, int groupId, const SyncOptions& sync)
{
    if (!score) {
        return;
    }
    for (const LinkGroup& g : linkGroups(score)) {
        if (g.id == groupId) {
            setLineText(g.source.line, memberText(g.id, true, sync, {}));
        }
    }
}

void resyncTarget(Score* score, TextLine* targetLine)
{
    const std::optional<LinkMember> m = linkMember(targetLine);
    if (!score || !m || m->source) {
        return;
    }
    MasterScore* master = score->masterScore();
    s_messages.clear();
    s_ownEdit = true;
    for (const LinkGroup& g : linkGroups(master)) {
        for (const LinkMember& t : g.targets) {
            if (t.line != m->line) {
                continue;
            }
            LinkMember clean = t;
            clean.handEdited.clear();
            const Passage source = readPassage(master, g.source.staffIdx, g.source.startTick, g.source.endTick);
            const Passage current = readPassage(master, t.staffIdx, t.startTick, t.endTick);
            const Passage desired = propagate(source, source, current, g.sync);
            if (writeTarget(master, clean, desired, g.sync)) {
                setLineText(t.line, memberText(g.id, false, g.sync, {}));
            }
        }
    }
}

void installLinkedPassagesHook()
{
    static LinkedPassagesHook hook;
    Score::jubelleCmdHook = &hook;
}
}
