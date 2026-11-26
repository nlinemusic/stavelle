/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle engine (je::perform), based on MuseScore Studio's RepeatList (src/engraving/dom/repeatlist.cpp,
 * Copyright (C) MuseScore Limited and others, GPL-3.0-only), including Jubelle's "Play this repeat" switch.
 * The unwinding follows MuseScore's step for step (comments kept where they explain a rule), on measure indices.
 */
#include "repeatlist.h"

#include <algorithm>
#include <list>
#include <set>
#include <string>

#include "model/fields.h"
#include "model/fragment.h"
#include "model/model.h"

namespace F = je::model::fields;
using je::model::Kind;
using je::model::Object;
using je::model::Snapshot;

namespace je::perform {
namespace {
constexpr size_t NONE = size_t(-1);

struct Volta {
    size_t start = NONE, end = NONE;   // measures
    std::vector<int> endings;          // sorted
    bool openEnd = true;               // no hook at the end (MuseScore's END_HOOK_TYPE NONE)

    bool hasEnding(int n) const { return std::find(endings.begin(), endings.end(), n) != endings.end(); }
    int firstEnding() const { return endings.empty() ? 0 : endings.front(); }
    int lastEnding() const { return endings.empty() ? 0 : endings.back(); }
};

enum class Type { SectionBreak, VoltaStart, VoltaEnd, RepeatStart, RepeatEnd, Jump, Marker };

struct Element {
    Element(Type t, size_t m)
        : type(t), measure(m) {}
    Type type;
    size_t measure;
    int volta = -1;           // VoltaStart, VoltaEnd
    Id item;                  // Jump, Marker
    std::string label;        // Marker
    bool rightMarker = false;
    std::string jumpTo, playUntil, continueAt;
    bool playRepeats = false;
    int repeatCount = 0;
    bool jubelleSkip = false; // on a RepeatStart: its repeat is switched off ("Play this repeat")
};

using Section = std::vector<Element>;

//! The marker types MuseScore aligns right (Marker::RIGHT_MARKERS); a marker without a type is a Fine
bool isRightMarkerType(const std::string& type)
{
    return type.empty() || type == "fine" || type == "coda" || type == "codasym" || type == "dacoda" || type == "dadblcoda";
}

std::vector<int> parseEndings(const std::string& text)
{
    std::vector<int> out;
    std::string cur;
    auto flush = [&]() {
        size_t i = 0;
        while (i < cur.size() && (cur[i] == ' ' || cur[i] == '\t')) {
            ++i;
        }
        if (i < cur.size()) {
            try {
                out.push_back(std::stoi(cur.substr(i)));
            } catch (...) {
            }
        }
        cur.clear();
    };
    for (char c : text) {
        if (c == ',') {
            flush();
        } else {
            cur += c;
        }
    }
    flush();
    std::sort(out.begin(), out.end());
    return out;
}

bool falseValue(const std::string& v)
{
    return v == "0" || v == "false";
}

struct Pos {
    size_t section, element;
};

class Unwinder
{
public:
    Unwinder(const Snapshot& s, Id flow)
        : m_s(s)
    {
        const Object* score = s.find(s.score());
        if (!score) {
            return;
        }
        if (!flow.isValid()) {
            for (Id c : score->children()) {
                if (const Object* o = s.find(c); o && o->kind() == Kind::Flow) {
                    flow = c;
                    break;
                }
            }
        }
        for (Id c : score->children()) {
            const Object* p = s.find(c);
            if (p && p->kind() == Kind::Player) {
                for (Id st : p->children()) {
                    if (const Object* o = s.find(st); o && o->kind() == Kind::Staff) {
                        m_topStaff = st;
                        break;
                    }
                }
                if (m_topStaff.isValid()) {
                    break;
                }
            }
        }
        const Object* f = s.find(flow);
        if (!f) {
            return;
        }
        for (Id c : f->children()) {
            const Object* o = s.find(c);
            if (!o) {
                continue;
            }
            if (o->kind() == Kind::Measure) {
                m_bases.push_back({ measures.size(), o->text(F::BREAK) == "section" });
                measures.push_back(c);
                times.push_back(o->fractionOr(F::TIME, Fraction()));
                lengths.push_back(o->fractionOr(F::DURATION, Fraction()));
                m_repeatStart.push_back(o->has(F::REPEAT_START));
                m_repeatEnd.push_back(o->has(F::REPEAT_END));
                m_repeatCount.push_back(int(o->has(F::REPEAT_END) ? o->intOr(F::REPEAT_END, 2) : 2));
                m_playRepeat.push_back(true);
                m_items.emplace_back();
            } else if (o->kind() == Kind::FlowItem && o->text(F::SUBKIND) == "frame") {
                const std::string& d = o->text(F::DETAILS);
                const size_t lb = d.find("<LayoutBreak");
                const bool section = lb != std::string::npos && d.find("<subtype>section</subtype>", lb) != std::string::npos;
                m_bases.push_back({ NONE, section });
            }
        }
        // the measures' jumps and markers (the top staff's: MuseScore's isTopSystemObject), and the measures'
        // own Jubelle setting, in file order
        std::vector<std::pair<int64_t, Id> > items;
        for (Id c : f->children()) {
            const Object* o = s.find(c);
            if (!o || o->kind() != Kind::FlowItem) {
                continue;
            }
            const std::string& sub = o->text(F::SUBKIND);
            if (sub == "measureDetail") {
                if (o->text(F::DETAILS).find("jubellePlayRepeat") != std::string::npos) {
                    const size_t mi = measureAtTime(o->fractionOr(F::TIME, Fraction()));
                    if (mi != NONE && falseValue(model::fragment::value(o->text(F::DETAILS), "jubellePlayRepeat"))) {
                        m_playRepeat[mi] = false;
                    }
                }
                continue;
            }
            if (sub != "jump" && sub != "marker") {
                continue;
            }
            const Id home = o->has(F::HOME_STAFF) ? o->idOr(F::HOME_STAFF) : (o->has(F::ANCHOR) ? o->idOr(F::ANCHOR) : Id());
            if (home != m_topStaff || o->intOr(F::VOICE_INDEX, 0) != 0) {
                continue;
            }
            items.push_back({ o->intOr(F::SEQ, 0), c });
        }
        std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [seq, id] : items) {
            const size_t mi = measureAtTime(s.find(id)->fractionOr(F::TIME, Fraction()));
            if (mi != NONE) {
                m_items[mi].push_back(id);
            }
        }
        // voltas, by start (the spanner map's order: time, then file order)
        std::vector<const Object*> vs;
        for (Id c : score->children()) {
            const Object* o = s.find(c);
            if (o && o->kind() == Kind::Spanner && o->text(F::SUBKIND) == "volta") {
                vs.push_back(o);
            }
        }
        std::stable_sort(vs.begin(), vs.end(), [](const Object* a, const Object* b) {
            const Fraction ta = a->fractionOr(F::TIME, Fraction()), tb = b->fractionOr(F::TIME, Fraction());
            return ta != tb ? ta < tb : a->intOr(F::SEQ, 0) < b->intOr(F::SEQ, 0);
        });
        for (const Object* o : vs) {
            const std::string& d = o->text(F::DETAILS);
            if (falseValue(model::fragment::value(d, "play"))) {
                continue;   // !playSpanner()
            }
            Volta v;
            const Fraction t = o->fractionOr(F::TIME, Fraction());
            v.start = measureAtTime(t);
            v.end = measureAtTime(t + o->fractionOr(F::DURATION, Fraction()) - Fraction(1, 1920));
            if (v.start == NONE || v.end == NONE) {
                continue;
            }
            v.endings = parseEndings(o->text(F::TEXT));
            const std::string hook = model::fragment::value(d, "endHookType");
            v.openEnd = hook.empty() || hook == "0" || hook == "none";
            m_rawVoltas.push_back(v);
        }
    }

    size_t measureAtTime(const Fraction& t) const
    {
        auto it = std::upper_bound(times.begin(), times.end(), t);
        if (it == times.begin()) {
            return NONE;
        }
        const size_t i = size_t(it - times.begin()) - 1;
        return t < times[i] + lengths[i] ? i : NONE;
    }

    std::vector<RepeatSegment> flatten() const
    {
        std::vector<RepeatSegment> out;
        if (measures.empty()) {
            return out;
        }
        RepeatSegment rs;
        for (size_t i = 0; i < measures.size(); ++i) {
            addMeasure(rs, i);
        }
        out.push_back(rs);
        return out;
    }

    std::vector<RepeatSegment> unwind()
    {
        std::vector<RepeatSegment> out;
        if (measures.empty()) {
            return out;
        }
        collect();
        const size_t END = m_sections.size();
        Pos playUntil { END, 0 }, continueAt { END, 0 };
        Id activeJump;
        bool forceFinalRepeat = false;
        std::set<std::pair<Id, int> > jumpsTaken;
        int playbackCount = 1;
        int activeVolta = -1;
        bool haveRs = false;
        RepeatSegment rs;
        auto newRs = [&](size_t measure) {
            rs = RepeatSegment();
            rs.playbackCount = playbackCount;
            addMeasure(rs, measure);
            haveRs = true;
        };
        auto pushRs = [&]() {
            if (haveRs && !rs.measures.empty()) {
                out.push_back(rs);
            }
        };

        for (size_t sectionIt = 0; sectionIt < m_sections.size(); ++sectionIt) {
            playbackCount = 1;
            size_t it = 0;   // should always be a RepeatStart indicator
            Pos startRepeat { sectionIt, 0 };
            activeVolta = -1;
            playUntil.section = END;
            continueAt.section = END;
            forceFinalRepeat = false;
            if (el(startRepeat).jubelleSkip) {
                // Jubelle: a repeat switched off from the start of the section
                playbackCount = el(startRepeat).repeatCount;
            }
            newRs(m_sections[sectionIt][it].measure);
            ++it;

            while (it < m_sections[sectionIt].size()) {
                Section& sec = m_sections[sectionIt];
                Element& e = sec[it];
                if (haveRs) {
                    addMeasures(rs, e.measure);
                }
                bool advance = true;
                switch (e.type) {
                case Type::SectionBreak:
                    pushRs();
                    break;
                case Type::VoltaStart: {
                    activeVolta = e.volta;
                    const Volta& v = m_voltas[size_t(activeVolta)];
                    // a volta whose endings all lie beyond the exhausted repeats is played after them
                    const bool playAfterRepeats = playbackCount > 1 && playbackCount >= el(startRepeat).repeatCount
                                                  && v.firstEnding() > playbackCount;
                    if (!v.hasEnding(playbackCount) && !playAfterRepeats) {
                        // should be skipped, remove our measure from rs
                        if (!rs.measures.empty()) {
                            rs.measures.pop_back();
                        }
                        if (!rs.measures.empty()) {
                            out.push_back(rs);
                        }
                        // skip the volta
                        size_t lastSkipped = v.end;
                        do {
                            ++it;
                            const Type t = sec[it].type;
                            const bool structure = t == Type::RepeatStart || t == Type::RepeatEnd || t == Type::Jump;
                            if (structure && (lastSkipped == NONE || sec[it].measure > lastSkipped)) {
                                lastSkipped = sec[it].measure;
                            }
                        } while (sec[it].type != Type::VoltaEnd);
                        const size_t after = it + 1;
                        const bool endedBySectionEnd = after == sec.size() || sec[after].type == Type::SectionBreak;
                        if (!endedBySectionEnd || lastSkipped == NONE) {
                            lastSkipped = sec[it].measure;
                        }
                        activeVolta = -1;
                        // start the next rs on the following measure
                        if (lastSkipped + 1 >= measures.size()) {
                            haveRs = false;   // end of score, but will still encounter the section break
                        } else {
                            newRs(lastSkipped + 1);
                        }
                    }
                    // else: take the volta
                } break;
                case Type::VoltaEnd:
                    activeVolta = -1;
                    break;
                case Type::RepeatStart:
                    if (!haveRs) {   // sent here by an end repeat
                        newRs(e.measure);
                    } else {
                        // Jubelle: a repeat switched off is played once, on its final pass
                        const int desired = (forceFinalRepeat || e.jubelleSkip) ? e.repeatCount : 1;
                        if (rs.playbackCount != desired) {
                            // restart a segment, as we have a new playbackCount reference
                            if (!rs.measures.empty()) {
                                rs.measures.pop_back();
                            }
                            if (!rs.measures.empty()) {
                                out.push_back(rs);
                            }
                            playbackCount = desired;
                            newRs(e.measure);
                        }
                        startRepeat = { sectionIt, it };
                    }
                    break;
                case Type::RepeatEnd:
                    e.repeatCount += 1;
                    if (playbackCount < el(startRepeat).repeatCount && e.repeatCount < m_repeatCount[e.measure]) {
                        // honour the repeat
                        pushRs();
                        haveRs = false;
                        do {   // rewind
                            --it;
                            if (sec[it].type == Type::VoltaStart) {
                                activeVolta = -1;
                            } else if (sec[it].type == Type::VoltaEnd) {
                                activeVolta = sec[it].volta;
                            }
                        } while (!(startRepeat.section == sectionIt && startRepeat.element == it));
                        ++playbackCount;
                        advance = false;   // force evaluation of the start repeat
                    }
                    break;
                case Type::Jump: {
                    if (activeJump == e.item) {
                        // we've reached this jump again after just having taken it: if it forced the final
                        // playthrough of measures, it only does so up to this point (MuseScore's test repeat23)
                        forceFinalRepeat = false;
                    }
                    // a jump is honoured only on the final playthrough of its measure, and taken once
                    if (playbackCount >= el(startRepeat).repeatCount
                        || (activeVolta >= 0 && playbackCount == m_voltas[size_t(activeVolta)].lastEnding())) {
                        const std::pair<Id, int> occurrence { e.item, playbackCount };
                        if (!jumpsTaken.count(occurrence)) {
                            jumpsTaken.insert(occurrence);
                            const Pos jumpTo = findMarker(e.jumpTo, sectionIt, it);
                            playUntil = findMarker(e.playUntil, sectionIt, it);
                            continueAt = findMarker(e.continueAt, sectionIt, it);
                            if (jumpTo.section != END) {
                                pushRs();
                                haveRs = false;
                                activeJump = e.item;
                                const bool playRepeats = e.playRepeats;
                                performJump(jumpTo.section, jumpTo.element, playRepeats, playbackCount, activeVolta, startRepeat);
                                sectionIt = jumpTo.section;
                                it = jumpTo.element;
                                forceFinalRepeat = !playRepeats;
                                Section& target = m_sections[sectionIt];
                                // re-evaluate the repeat counts of end repeats
                                if (playUntil.section != END) {   // only required with an end target
                                    for (size_t r = it + 1; r < target.size(); ++r) {
                                        if (target[r].type == Type::RepeatEnd && target[r].repeatCount != 0) {
                                            // played before (MuseScore's test repeat22 for why only then)
                                            target[r].repeatCount = forceFinalRepeat ? m_repeatCount[target[r].measure] : 0;
                                        }
                                    }
                                }
                                if (activeVolta >= 0 && playbackCount < el(startRepeat).repeatCount) {
                                    // we jumped into a volta, which might not be the only one of this start repeat:
                                    // re-evaluate the voltas of this start repeat (tests repeat52 and repeat53)
                                    size_t f = it;
                                    do {
                                        --f;
                                    } while (!(startRepeat.section == sectionIt && startRepeat.element == f));
                                    ++f;
                                    int voltaRef = -1;
                                    int processed = 1;
                                    while (f < target.size() && target[f].type != Type::RepeatStart
                                           && processed < el(startRepeat).repeatCount) {
                                        if (target[f].type == Type::VoltaStart) {
                                            voltaRef = target[f].volta;
                                            if (m_voltas[size_t(voltaRef)].lastEnding() < playbackCount) {
                                                voltaRef = -1;   // we won't pass here any more
                                            }
                                        } else if (target[f].type == Type::RepeatEnd) {
                                            if (voltaRef >= 0) {
                                                int remaining = 0;
                                                for (int ending : m_voltas[size_t(voltaRef)].endings) {
                                                    if (ending >= playbackCount) {
                                                        ++remaining;
                                                    }
                                                }
                                                target[f].repeatCount = m_repeatCount[target[f].measure] - remaining - 1;
                                            }
                                            processed += m_repeatCount[target[f].measure] - 1;
                                        }
                                        ++f;
                                    }
                                }
                                // we've arrived at the target marker
                                newRs(target[it].measure);
                            }
                        }
                    }
                } break;
                case Type::Marker:
                    if (sectionIt == playUntil.section && it == playUntil.element
                        && (playbackCount == el(startRepeat).repeatCount
                            || (activeVolta >= 0 && playbackCount == m_voltas[size_t(activeVolta)].lastEnding()))) {
                        // found the final playthrough of this marker
                        pushRs();
                        haveRs = false;
                        playUntil.section = END;
                        forceFinalRepeat = false;
                        if (continueAt.section != END) {
                            performJump(continueAt.section, continueAt.element, true, playbackCount, activeVolta, startRepeat);
                            sectionIt = continueAt.section;
                            it = continueAt.element;
                            if (el(startRepeat).measure != m_sections[sectionIt][it].measure) {
                                // use the continueAt target as the repeat reference if it wasn't this measure
                                // already (a start repeat is processed before a marker: test repeat23 m12)
                                startRepeat = { sectionIt, it };
                            }
                            newRs(m_sections[sectionIt][it].measure);
                            continueAt.section = END;
                        } else {   // nowhere to go: on to the next section
                            it = m_sections[sectionIt].size();
                            advance = false;
                        }
                    }
                    break;
                }
                if (advance) {
                    ++it;
                }
            }
        }
        return out;
    }

    std::vector<Id> measures;
    std::vector<Fraction> times, lengths;

private:
    struct Base {
        size_t measure;   // NONE for a frame
        bool sectionBreak;
    };

    Element& el(const Pos& p) { return m_sections[p.section][p.element]; }

    void addMeasure(RepeatSegment& rs, size_t m) const
    {
        if (rs.measures.empty()) {
            rs.time = times[m];
        }
        if (rs.measures.empty() || rs.measures.back() != m) {
            rs.measures.push_back(m);
        }
        rs.length = times[rs.measures.back()] + lengths[rs.measures.back()] - rs.time;
    }

    //! Expands or clips the segment's measures up to m (including m)
    void addMeasures(RepeatSegment& rs, size_t m) const
    {
        if (!rs.measures.empty()) {
            size_t last = rs.measures.back() + 1;
            if (last < measures.size() && last < m) {
                while (last != m) {
                    rs.measures.push_back(last);
                    ++last;
                }
            }
            while (!rs.measures.empty() && rs.measures.back() >= m) {
                rs.measures.pop_back();
            }
        }
        addMeasure(rs, m);
    }

    void collect()
    {
        m_sections.clear();
        m_voltas.clear();
        Section sec;
        size_t startFrom = 0;   // the section's reference start repeat (index into sec)
        auto pushStart = [&](size_t measure) {
            Element e(Type::RepeatStart, measure);
            e.repeatCount = 1;
            sec.push_back(e);
            startFrom = sec.size() - 1;
        };
        size_t bi = 0;
        while (bi < m_bases.size() && m_bases[bi].measure == NONE) {
            ++bi;   // MuseScore starts at the first measure
        }
        if (bi == m_bases.size()) {
            return;
        }
        pushStart(m_bases[bi].measure);
        size_t sectionEnd = NONE;

        // voltas might overlap (copies on several staves, or real overlaps): pre-processed into pieces that don't
        std::list<Volta> pre;
        for (const Volta& raw : m_rawVoltas) {
            Volta volta = raw;
            bool haveVolta = true;
            if (pre.empty()) {
                pre.push_back(volta);
                continue;
            }
            std::list<Volta> toMerge;
            while (!pre.empty() && volta.start <= pre.back().end) {
                toMerge.push_back(pre.back());
                pre.pop_back();
            }
            while (!toMerge.empty()) {
                Volta remainder = toMerge.back();
                if (volta.start != remainder.start) {
                    // the first part is not empty
                    toMerge.back().end = volta.start - 1;
                    remainder.start = volta.start;
                    pre.push_back(toMerge.back());
                }
                toMerge.pop_back();
                // remainder and volta now start together: make remainder end first
                if (volta.end < remainder.end) {
                    std::swap(volta, remainder);
                }
                std::vector<int> common;
                for (int ending : remainder.endings) {
                    if (volta.hasEnding(ending)) {
                        common.push_back(ending);
                    }
                }
                remainder.endings = common;
                pre.push_back(remainder);
                if (volta.end != remainder.end) {
                    volta.start = remainder.end + 1;   // volta extends past remainder
                } else {
                    pre.splice(pre.end(), toMerge);
                    haveVolta = false;
                    break;
                }
            }
            if (haveVolta) {
                pre.push_back(volta);
            }
        }

        int volta = -1;
        auto voltaRef = [&](const Volta& v) {
            m_voltas.push_back(v);
            return int(m_voltas.size() - 1);
        };
        for (; bi < m_bases.size(); ++bi) {
            const Base& mb = m_bases[bi];
            if (mb.measure != NONE) {
                const size_t mi = mb.measure;
                sectionEnd = mi;
                // volta?
                if (!pre.empty() && pre.front().start == mi) {
                    if (volta >= 0) {
                        // the previous volta was supposed to end before us (an open volta): insert its end
                        Element e(Type::VoltaEnd, mi - 1);
                        e.volta = volta;
                        sec.push_back(e);
                    }
                    volta = voltaRef(pre.front());
                    Element e(Type::VoltaStart, mi);
                    e.volta = volta;
                    sec.push_back(e);
                    pre.pop_front();
                }
                // start
                if (m_repeatStart[mi]) {
                    if (volta >= 0 && m_voltas[size_t(volta)].start != mi) {
                        // the volta and the start repeat are not on the same measure: assume the volta ended
                        // before us (an open volta)
                        Element e(Type::VoltaEnd, mi - 1);
                        e.volta = volta;
                        sec.push_back(e);
                        volta = -1;
                    }
                    pushStart(mi);
                }
                // jumps and markers
                for (Id id : m_items[mi]) {
                    const Object* o = m_s.find(id);
                    const std::string& d = o->text(F::DETAILS);
                    if (o->text(F::SUBKIND) == "jump") {
                        Element e(Type::Jump, mi);
                        e.item = id;
                        e.jumpTo = model::fragment::value(d, "jumpTo");
                        e.playUntil = model::fragment::value(d, "playUntil");
                        e.continueAt = model::fragment::value(d, "continueAt");
                        const std::string pr = model::fragment::value(d, "playRepeats");
                        e.playRepeats = !pr.empty() && !falseValue(pr);
                        sec.push_back(e);
                        if (volta >= 0) {
                            const Volta& v = m_voltas[size_t(volta)];
                            if (v.end < mi || (v.end == mi && v.openEnd)) {
                                // the previous volta was supposed to end before us, or an open volta ends with us,
                                // unless this measure also has an end repeat (MuseScore's #327681)
                                if (!m_repeatEnd[mi]) {
                                    Element ve(Type::VoltaEnd, mi);
                                    ve.volta = volta;
                                    sec.push_back(ve);
                                    volta = -1;
                                }
                            }
                        }
                    } else {
                        Element e(Type::Marker, mi);
                        e.item = id;
                        e.label = model::fragment::value(d, "label");
                        e.rightMarker = isRightMarkerType(model::fragment::value(d, "markerType"));
                        // right markers before left ones in a measure, and markers before jumps
                        size_t ins = sec.size() - 1;
                        while (sec[ins].measure == mi) {
                            bool before = false;
                            if (sec[ins].type == Type::Marker && !e.rightMarker) {
                                before = sec[ins].rightMarker;
                            }
                            if (before || sec[ins].type == Type::Jump) {
                                --ins;   // possible: the list always starts with a start repeat
                            } else {
                                break;
                            }
                        }
                        sec.insert(sec.begin() + std::ptrdiff_t(ins + 1), e);
                        if (startFrom > ins) {
                            ++startFrom;
                        }
                    }
                }
                // end
                if (m_repeatEnd[mi]) {
                    sec.push_back(Element(Type::RepeatEnd, mi));
                    sec[startFrom].repeatCount += m_repeatCount[mi] - 1;
                    if (!m_playRepeat[mi]) {
                        sec[startFrom].jubelleSkip = true;
                    }
                    if (volta >= 0) {
                        Element e(Type::VoltaEnd, mi);
                        e.volta = volta;
                        sec.push_back(e);
                        volta = -1;
                    }
                }
                // the end of a closed volta
                if (volta >= 0 && m_voltas[size_t(volta)].end == mi && !m_voltas[size_t(volta)].openEnd) {
                    Element e(Type::VoltaEnd, mi);
                    e.volta = volta;
                    sec.push_back(e);
                    volta = -1;
                }
            }
            // section break, or the end of the score
            size_t nextMeasure = NONE;
            for (size_t k = bi + 1; k < m_bases.size(); ++k) {
                if (m_bases[k].measure != NONE) {
                    nextMeasure = k;
                    break;
                }
            }
            if (mb.sectionBreak || nextMeasure == NONE) {
                if (sectionEnd != NONE) {
                    if (volta >= 0) {
                        Element e(Type::VoltaEnd, sectionEnd);
                        e.volta = volta;
                        sec.push_back(e);
                        volta = -1;
                    }
                    sec.push_back(Element(Type::SectionBreak, sectionEnd));
                    sectionEnd = NONE;
                    m_sections.push_back(sec);
                }
                if (nextMeasure != NONE) {
                    sec = Section();
                    pushStart(m_bases[nextMeasure].measure);
                    bi = nextMeasure - 1;   // skips frames between sections; the loop moves on one
                } else {
                    break;
                }
            }
        }
    }

    //! A marker by label, in this section first ("start" and "end": the section's own), then earlier and later ones
    Pos findMarker(const std::string& label, size_t refSection, size_t refElement) const
    {
        using P = Pos;
        const size_t END = m_sections.size();
        auto isMarker = [&](size_t s, size_t e) {
            return m_sections[s][e].type == Type::Marker && m_sections[s][e].label == label;
        };
        if (label == "start") {
            return P { refSection, 0 };
        }
        if (label == "end") {
            return P { refSection, m_sections[refSection].size() - 1 };
        }
        for (size_t e = refElement; e-- > 0;) {   // backwards in this section
            if (isMarker(refSection, e)) {
                return P { refSection, e };
            }
        }
        for (size_t e = refElement + 1; e < m_sections[refSection].size(); ++e) {   // forwards in this section
            if (isMarker(refSection, e)) {
                return P { refSection, e };
            }
        }
        for (size_t s = refSection; s-- > 0;) {   // backwards through the earlier sections
            for (size_t e = m_sections[s].size(); e-- > 0;) {
                if (isMarker(s, e)) {
                    return P { s, e };
                }
            }
        }
        for (size_t s = refSection + 1; s < END; ++s) {   // forwards through the later sections
            for (size_t e = 0; e < m_sections[s].size(); ++e) {
                if (isMarker(s, e)) {
                    return P { s, e };
                }
            }
        }
        return P { END, 0 };
    }

    void performJump(size_t section, size_t target, bool withRepeats, int& playbackCount, int& activeVolta, Pos& startRepeat)
    {
        activeVolta = -1;
        startRepeat = { section, 0 };
        for (size_t e = 0; e != target; ++e) {
            switch (m_sections[section][e].type) {
            case Type::VoltaStart:
                activeVolta = m_sections[section][e].volta;
                break;
            case Type::VoltaEnd:
                activeVolta = -1;
                break;
            case Type::RepeatStart:
                startRepeat = { section, e };
                break;
            default:
                break;
            }
        }
        const Element& ref = m_sections[startRepeat.section][startRepeat.element];
        if (withRepeats) {
            playbackCount = activeVolta >= 0 ? m_voltas[size_t(activeVolta)].firstEnding() : 1;
            if (playbackCount == 0) {
                playbackCount = 1;
            }
        } else {   // the final repeat after a jump
            playbackCount = activeVolta >= 0 ? m_voltas[size_t(activeVolta)].lastEnding() : ref.repeatCount;
            if (playbackCount == 0) {
                playbackCount = ref.repeatCount;
            }
        }
    }

    const Snapshot& m_s;
    Id m_topStaff;
    std::vector<Base> m_bases;
    std::vector<bool> m_repeatStart, m_repeatEnd, m_playRepeat;
    std::vector<int> m_repeatCount;
    std::vector<std::vector<Id> > m_items;
    std::vector<Volta> m_rawVoltas;
    std::vector<Volta> m_voltas;
    std::vector<Section> m_sections;
};
}

bool RepeatSegment::contains(size_t measure) const
{
    return std::find(measures.begin(), measures.end(), measure) != measures.end();
}

RepeatList RepeatList::build(const Snapshot& snapshot, Id flow, bool expand)
{
    Unwinder u(snapshot, flow);
    RepeatList list;
    list.m_segments = expand ? u.unwind() : u.flatten();
    list.m_measures = u.measures;
    list.m_times = u.times;
    list.m_lengths = u.lengths;
    Fraction played;
    for (RepeatSegment& s : list.m_segments) {
        s.playTime = played;
        played += s.length;
    }
    return list;
}

size_t RepeatList::indexOf(Id measure) const
{
    auto it = std::find(m_measures.begin(), m_measures.end(), measure);
    return it == m_measures.end() ? NONE : size_t(it - m_measures.begin());
}

size_t RepeatList::measureAt(const Fraction& time) const
{
    auto it = std::upper_bound(m_times.begin(), m_times.end(), time);
    if (it == m_times.begin()) {
        return NONE;
    }
    const size_t i = size_t(it - m_times.begin()) - 1;
    return time < m_times[i] + m_lengths[i] ? i : NONE;
}

std::vector<size_t> RepeatList::followingMeasures(size_t measure) const
{
    std::vector<size_t> out;
    for (size_t i = 0; i + 1 < m_segments.size(); ++i) {
        if (m_segments[i].endsWith(measure) && !m_segments[i + 1].measures.empty()) {
            out.push_back(m_segments[i + 1].measures.front());
        }
    }
    return out;
}

std::vector<size_t> RepeatList::precedingMeasures(size_t measure) const
{
    std::vector<size_t> out;
    for (size_t i = 1; i < m_segments.size(); ++i) {
        if (m_segments[i].startsWith(measure) && !m_segments[i - 1].measures.empty()) {
            out.push_back(m_segments[i - 1].measures.back());
        }
    }
    return out;
}

bool RepeatList::adjacent(size_t first, size_t second) const
{
    if (first == NONE || second == NONE) {
        return false;
    }
    if (first == second) {
        return true;
    }
    bool firstFound = false, secondFound = false;
    std::vector<size_t> next;
    for (size_t i = 0; i < m_segments.size(); ++i) {
        const RepeatSegment& rs = m_segments[i];
        const bool hasFirst = rs.contains(first), hasSecond = rs.contains(second);
        firstFound |= hasFirst;
        secondFound |= hasSecond;
        if (hasFirst && hasSecond) {
            return true;
        }
        if (!rs.endsWith(first) || i + 1 == m_segments.size()) {
            continue;
        }
        if (!m_segments[i + 1].measures.empty()) {
            next.push_back(m_segments[i + 1].measures.front());
        }
    }
    if (std::find(next.begin(), next.end(), second) != next.end()) {
        return true;
    }
    if (!firstFound && !secondFound) {
        return first + 1 == second;   // outside the (invalid) repeat structure
    }
    return false;
}

Fraction RepeatList::playLength() const
{
    return m_segments.empty() ? Fraction() : m_segments.back().playTime + m_segments.back().length;
}
}
