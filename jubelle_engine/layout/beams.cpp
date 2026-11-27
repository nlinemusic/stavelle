/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::layout). Not part of MuseScore.
 *
 * Ported from MuseScore's BeamLayout::createBeams and beamGraceNotes (rendering/score/beamlayout.cpp) and Groups
 * (dom/groups.cpp: the default beam groups, baseBeamMode, actualBeamMode, beamMode) (GPL-3.0).
 */
#include "beams.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <unordered_map>

#include "io/details.h"
#include "io/xml.h"
#include "model/detailskey.h"
#include "model/fields.h"
#include "model/fragment.h"

namespace je::layout {
namespace F = model::fields;
using model::Kind;
using model::Object;

namespace {
// MuseScore's BeamMode
enum BM { INVALID = -1, AUTO = 0, NONE, BEGIN, BEGIN16, BEGIN32, MID, END };
constexpr int V_QUARTER = 4, V_EIGHTH = 5, V_16TH = 6, V_32ND = 7, V_64TH = 8, V_1024TH = 12, V_MEASURE = 14, V_INVALID = 15;

struct Node {
    int pos, action;
};
using Groups = std::vector<Node>;

//! MuseScore's noteGroups: the default beam groups of the common time signatures
const Groups* defaultGroups(int num, int den)
{
    static const std::map<std::pair<int, int>, Groups> table {
        { { 2, 2 }, { { 4, 0x200 }, { 8, 0x110 }, { 12, 0x200 }, { 16, 0x111 }, { 20, 0x200 }, { 24, 0x110 }, { 28, 0x200 } } },
        { { 3, 2 }, { { 4, 0x200 }, { 8, 0x110 }, { 12, 0x200 }, { 16, 0x111 }, { 20, 0x200 }, { 24, 0x110 }, { 28, 0x200 },
              { 32, 0x111 }, { 36, 0x200 }, { 40, 0x110 }, { 44, 0x200 } } },
        { { 4, 2 }, { { 4, 0x200 }, { 8, 0x110 }, { 12, 0x200 }, { 16, 0x111 }, { 20, 0x200 }, { 24, 0x110 }, { 28, 0x200 },
              { 32, 0x111 }, { 36, 0x200 }, { 40, 0x110 }, { 44, 0x200 }, { 48, 0x111 }, { 52, 0x200 }, { 56, 0x110 }, { 60, 0x200 } } },
        { { 2, 4 }, { { 4, 0x200 }, { 8, 0x111 }, { 12, 0x200 } } },
        { { 3, 4 }, { { 4, 0x200 }, { 8, 0x111 }, { 12, 0x200 }, { 16, 0x111 }, { 20, 0x200 } } },
        { { 4, 4 }, { { 4, 0x200 }, { 8, 0x110 }, { 12, 0x200 }, { 16, 0x111 }, { 20, 0x200 }, { 24, 0x110 }, { 28, 0x200 } } },
        { { 5, 4 }, { { 4, 0x200 }, { 8, 0x110 }, { 12, 0x200 }, { 16, 0x110 }, { 20, 0x200 }, { 24, 0x111 }, { 28, 0x200 },
              { 32, 0x110 }, { 36, 0x200 } } },
        { { 6, 4 }, { { 4, 0x200 }, { 8, 0x110 }, { 12, 0x200 }, { 16, 0x110 }, { 20, 0x200 }, { 24, 0x111 }, { 28, 0x200 },
              { 32, 0x110 }, { 36, 0x200 }, { 40, 0x110 }, { 44, 0x200 } } },
        { { 3, 8 }, { { 4, 0x200 }, { 8, 0x200 } } },
        { { 5, 8 }, { { 4, 0x200 }, { 8, 0x200 }, { 12, 0x111 }, { 16, 0x200 } } },
        { { 6, 8 }, { { 4, 0x200 }, { 8, 0x200 }, { 12, 0x111 }, { 16, 0x200 }, { 20, 0x200 } } },
        { { 7, 8 }, { { 4, 0x200 }, { 8, 0x200 }, { 12, 0x111 }, { 16, 0x200 }, { 20, 0x111 }, { 24, 0x200 } } },
        { { 9, 8 }, { { 4, 0x200 }, { 8, 0x200 }, { 12, 0x111 }, { 16, 0x200 }, { 20, 0x200 }, { 24, 0x111 }, { 28, 0x200 }, { 32, 0x200 } } },
        { { 12, 8 }, { { 4, 0x200 }, { 8, 0x200 }, { 12, 0x111 }, { 16, 0x200 }, { 20, 0x200 }, { 24, 0x111 }, { 28, 0x200 },
               { 32, 0x200 }, { 36, 0x111 }, { 40, 0x200 }, { 44, 0x200 } } },
    };
    const auto it = table.find({ num, den });
    return it == table.end() ? nullptr : &it->second;
}

//! Groups::beamMode: a group boundary's beam mode at `tick` (in the measure) for a note value
int groupBeamMode(const Groups& g, int tick, int type)
{
    int shift;
    if (type == V_EIGHTH) {
        shift = 0;
    } else if (type == V_16TH) {
        shift = 4;
    } else if (type >= V_32ND && type <= V_1024TH) {
        shift = 8;
    } else {
        return AUTO;
    }
    const int dm = 480 / 8;
    for (const Node& e : g) {
        if (e.pos * dm < tick) {
            continue;
        }
        if (e.pos * dm > tick) {
            break;
        }
        switch ((e.action >> shift) & 0xf) {
        case 0: return AUTO;
        case 1: return BEGIN;
        case 2: return BEGIN16;
        case 3: return BEGIN32;
        default: return AUTO;
        }
    }
    return AUTO;
}

int ticksOf(const Fraction& f)
{
    // Fraction::ticks, rounded
    return int((f.numerator() * 1920 + f.denominator() / 2) / f.denominator());
}

//! TDuration(length): the longest note value that fits the length (with up to 4 dots, its type)
int typeOfLength(const Fraction& l)
{
    if (l.numerator() == 0) {
        return V_MEASURE;
    }
    int i = 0;
    for (Fraction b(4, 1); i <= V_1024TH; b = b * Fraction(1, 2), ++i) {
        if (!(l < b)) {
            return i;
        }
    }
    return V_INVALID;
}

int hooksOf(int type)
{
    return type >= V_EIGHTH && type <= V_1024TH ? type - 4 : 0;
}

//! TDuration's order: a shorter value (a larger type) is "less", then fewer dots
bool durationLess(int typeA, int dotsA, int typeB, int dotsB)
{
    return typeB < typeA || (typeA == typeB && dotsA < dotsB);
}

struct CR {
    Id id;
    bool rest = false;
    bool grace = false;
    int type = V_QUARTER, dots = 0;
    int beamMode = AUTO;
    int crossMeasure = 0;
    Fraction tick, ticks, actual, rtick, stretch { 1, 1 };   // ticks: written (MuseScore's ticks()), actual: as played
    Id tuplet;
    std::vector<CR> before, after;
    int beam = -1;   // the beam it is in
    bool unbeamed = false;   // its own measure left it out of any beam (beam mode NONE, or too long)
    Fraction endTick() const { return tick + actual; }
};

int beamModeOf(const Object* o, int version)
{
    const bool fresh = !o->has(F::DETAILS_KEY) || model::detailsKey(*o) == o->intOr(F::DETAILS_KEY, 0);
    if (!fresh) {
        return AUTO;
    }
    std::string v = model::fragment::value(o->text(F::DETAILS), "BeamMode");
    if (v.empty()) {
        v = model::fragment::value(o->text(F::DETAILS_AFTER), "BeamMode");
    }
    // before 4.10 MuseScore wrote BEGIN16 and BEGIN32 as begin32 and begin64 (read400's and read206's TRead)
    if (version < 410) {
        if (v == "begin64") {
            v = "begin32";
        } else if (v == "begin32") {
            v = "begin16";
        }
    }
    static const std::map<std::string, int> names { { "auto", AUTO }, { "begin", BEGIN }, { "mid", MID }, { "end", END },
        { "no", NONE }, { "begin16", BEGIN16 }, { "begin32", BEGIN32 }, { "invalid", INVALID } };
    const auto it = names.find(v);
    if (it != names.end()) {
        return it->second;
    }
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    return !v.empty() && end && *end == 0 ? int(n) : AUTO;
}

struct Beamer {
    const ScoreContext& ctx;
    std::map<Id, const ChordLayout*> chordOf;
    std::vector<Beam> beams;

    struct TimeSig {
        int num = 4, den = 4;
        Groups groups;
    };
    std::vector<std::map<Fraction, TimeSig> > timeSigs;
    std::set<Fraction> frames;   // the times of frames (a frame before a measure starts at its time)

    Beamer(const ScoreContext& c, const std::vector<ChordLayout>& chords)
        : ctx(c)
    {
        for (const ChordLayout& cl : chords) {
            chordOf[cl.chord] = &cl;
        }
        timeSigs.assign(ctx.staves().size(), {});
        const Object* score = ctx.obj(ctx.snapshot().score());
        for (Id f : score->children()) {
            const Object* flow = ctx.obj(f);
            if (!flow || flow->kind() != Kind::Flow) {
                continue;
            }
            for (Id it : flow->children()) {
                const Object* item = ctx.obj(it);
                if (item->kind() == Kind::FlowItem && item->text(F::SUBKIND) == "frame") {
                    frames.insert(item->fractionOr(F::TIME, Fraction()));
                }
                if (item->kind() != Kind::FlowItem || item->text(F::SUBKIND) != "timeSig") {
                    continue;
                }
                const int st = item->has(F::ANCHOR) ? ctx.staffIndex(item->idOr(F::ANCHOR)) : -1;
                if (st < 0 && item->has(F::ANCHOR)) {
                    continue;
                }
                TimeSig ts;
                ts.num = int(item->intOr(F::TIMESIG_NUM, 4));
                ts.den = int(item->intOr(F::TIMESIG_DEN, 4));
                for (const io::xml::Node& n : io::parseFragment(item->text(F::DETAILS))) {
                    if (n.name == "Groups") {
                        for (const io::xml::Node& node : n.children) {
                            if (node.name == "Node") {
                                ts.groups.push_back({ std::atoi(node.attr("pos").c_str()), std::atoi(node.attr("action").c_str()) });
                            }
                        }
                    }
                }
                const Fraction t = item->fractionOr(F::TIME, Fraction());
                for (size_t i = 0; i < timeSigs.size(); ++i) {
                    if (st < 0 || size_t(st) == i) {
                        timeSigs[i].emplace(t, ts);
                    }
                }
            }
            break;
        }
    }

    const TimeSig* timeSigAt(int staff, const Fraction& t) const
    {
        const auto& m = timeSigs[size_t(staff)];
        auto it = m.upper_bound(t);
        return it == m.begin() ? nullptr : &std::prev(it)->second;
    }

    //! Staff::group
    Groups groupsAt(int staff, const Fraction& t, const MeasureInfo& m) const
    {
        const TimeSig* ts = timeSigAt(staff, t);
        int num = 4, den = 4;
        if (ts) {
            if (!ts->groups.empty()) {
                return ts->groups;
            }
            num = ts->num, den = ts->den;
        } else {
            const Object* mo = ctx.obj(m.id);
            num = int(mo->intOr(F::TIMESIG_NUM, 4)), den = int(mo->intOr(F::TIMESIG_DEN, 4));
        }
        if (const Groups* g = defaultGroups(num, den)) {
            return *g;
        }
        // Groups::endings builds a group per beat for the other time signatures but returns the empty copy it stored
        // first, so MuseScore breaks no beams by position in them (4/8: four eighths under one beam; 7/4: only the
        // beat subdivision rule breaks them). Kept as MuseScore shows them, pending a decision (docs/14: beam groups).
        return {};
    }

    CR makeCR(const Object* ev, const MeasureInfo& m)
    {
        CR c;
        c.id = ev->id();
        c.rest = ev->kind() == Kind::Rest;
        c.grace = ev->has(F::GRACE);
        c.tick = ev->fractionOr(F::TIME, Fraction());
        c.actual = ev->fractionOr(F::DURATION, Fraction());
        c.ticks = c.actual;
        if (ev->has(F::EXPLICIT_DURATION)) {
            // a two-note tremolo's chord: its given <duration> (DURATION without its tuplets' ratios)
            for (const Object* t = ctx.obj(ev->idOr(F::TUPLET)); t; t = ctx.obj(t->idOr(F::TUPLET))) {
                c.ticks = c.ticks * Fraction(t->intOr(F::RATIO_NUM, 1), t->intOr(F::RATIO_DEN, 1));
            }
        } else if (ev->has(F::BASE_DURATION) && !ev->has(F::MEASURE_REST)) {
            Fraction add = ev->fractionOr(F::BASE_DURATION, Fraction());
            c.ticks = add;
            for (int64_t i = 0; i < ev->intOr(F::DOTS, 0); ++i) {
                add = add * Fraction(1, 2);
                c.ticks = c.ticks + add;
            }
        }
        c.rtick = c.tick - m.start;
        c.stretch = ev->fractionOr(F::STRETCH, Fraction(1, 1));
        c.tuplet = ev->idOr(F::TUPLET);
        c.beamMode = beamModeOf(ev, ctx.version());
        c.dots = int(ev->intOr(F::DOTS, 0));
        if (ev->has(F::MEASURE_REST)) {
            c.type = V_MEASURE;
        } else {
            const Fraction base = ev->fractionOr(F::BASE_DURATION, c.ticks);
            int i = 0;
            for (Fraction b(4, 1); b > base && i < V_1024TH; b = b * Fraction(1, 2)) {
                ++i;
            }
            c.type = i;
        }
        const auto cl = chordOf.find(c.id);
        if (cl != chordOf.end()) {
            c.type = cl->second->durationType;
            c.dots = cl->second->dots;
            c.crossMeasure = cl->second->crossMeasure;
        }
        return c;
    }

    // ---- Groups::baseBeamMode ----
    int baseBeamMode(const CR& cr, const CR* prev, int type, int dots, const Groups& g, const Fraction& anacrusis) const
    {
        if (cr.grace || cr.beamMode != AUTO) {
            return cr.beamMode;
        }
        const Fraction maxTickLen = prev && cr.ticks < prev->ticks ? prev->ticks : cr.ticks;
        Fraction smallest(1, 8);
        const Fraction limit(1, 32);
        while (maxTickLen < smallest || (ticksOf(smallest) && ticksOf(cr.rtick) % ticksOf(smallest) != 0)) {
            smallest = smallest * Fraction(1, 2);
            if (smallest < limit) {
                smallest = cr.ticks;
                break;
            }
        }
        const int bigBeat = typeOfLength(smallest);
        const int tick = ticksOf(cr.rtick * cr.stretch + anacrusis);
        const int byType = groupBeamMode(g, tick, type);
        const int byPos = groupBeamMode(g, tick, bigBeat);
        int val = byType == AUTO ? byPos : byType;
        if (val == AUTO && tick != 0) {
            // a tuplet starting or ending between two notes of the same value: as the next shorter value
            if (prev && cr.tuplet != prev->tuplet && type == prev->type && dots == prev->dots) {
                if (type <= V_EIGHTH) {
                    val = groupBeamMode(g, tick, V_16TH);
                } else if (type == V_16TH) {
                    val = groupBeamMode(g, tick, V_32ND);
                } else {
                    val = groupBeamMode(g, tick, V_64TH);
                }
            }
            // a hole between them (voices 2 to 4)
            if (crVoice && prev && !prev->tuplet.isValid() && prev->endTick() < cr.tick) {
                val = BEGIN;
            }
        }
        return val;
    }
    mutable int crVoice = 0;

    // ---- Groups::actualBeamMode ----
    int actualBeamMode(const CR& cr, const CR* prev, const std::unordered_map<int, std::pair<int, int> >& beatSub,
                       const Groups& g, const Fraction& anacrusis, bool checkBeats) const
    {
        if (cr.rest && cr.beamMode == AUTO) {
            return NONE;
        }
        if (!cr.rest && hooksOf(cr.type) == 0) {
            return NONE;
        }
        if (hooksOf(cr.type) > 0 && cr.crossMeasure == 2) {
            return NONE;
        }
        int bm = baseBeamMode(cr, prev, cr.type, cr.dots, g, anacrusis);
        if (bm == AUTO) {
            if (cr.rtick.numerator() == 0) {
                return BEGIN;
            }
            if (checkBeats) {
                const int tick = ticksOf(cr.rtick * cr.stretch);
                if (tick % 480 == 0) {
                    const int beat = tick / 480;
                    auto cur = beatSub.find(beat);
                    auto prv = beatSub.find(beat - 1);
                    std::pair<int, int> a = cur != beatSub.end() ? cur->second : std::make_pair(V_INVALID, 0);
                    std::pair<int, int> b = prv != beatSub.end() ? prv->second : std::make_pair(V_INVALID, 0);
                    const std::pair<int, int> mn = durationLess(b.first, b.second, a.first, a.second) ? b : a;
                    bm = baseBeamMode(cr, prev, mn.first, mn.second, g, anacrusis);
                }
            }
        }
        return bm == AUTO ? MID : bm;
    }

    static bool hasBreak(const Object* m)
    {
        const std::string& b = m->text(F::BREAK);
        return b == "line" || b == "page" || b == "section";
    }
    const MeasureInfo& measureOf(const Fraction& t) const
    {
        const auto& ms = ctx.measures();
        auto it = std::upper_bound(ms.begin(), ms.end(), t, [](const Fraction& x, const MeasureInfo& m) { return x < m.start; });
        return it == ms.begin() ? ms.front() : *std::prev(it);
    }

    static bool beamModeMid(int a) { return a == MID || a == BEGIN16 || a == BEGIN32; }
    static bool beamNoContinue(int m) { return m == END || m == NONE || m == INVALID; }

    void finish(int& beam)
    {
        if (beam >= 0) {
            Beam& b = beams[size_t(beam)];
            bool allRests = true;
            for (Id e : b.elements) {
                allRests = allRests && ctx.obj(e)->kind() == Kind::Rest;
            }
            if (b.elements.size() <= 1 || allRests) {
                b.elements.clear();
            }
        }
        beam = -1;
    }

    // ---- BeamLayout::beamGraceNotes ----
    void beamGraces(std::vector<CR>& graces, int staff, int voice)
    {
        CR* a1 = nullptr;
        int beam = -1;
        for (CR& cr : graces) {
            const int bm = cr.beamMode;   // baseBeamMode: a grace note's own
            if (cr.type <= V_QUARTER || bm == NONE) {
                finish(beam);
                a1 = nullptr;
                continue;
            }
            CR* c = &cr;
            if (beam >= 0) {
                bool end = bm == BEGIN;
                if (!end) {
                    beams[size_t(beam)].elements.push_back(c->id);
                    c = nullptr;
                    end = bm == END;
                }
                if (end) {
                    finish(beam);
                }
            }
            if (!c) {
                continue;
            }
            if (!a1) {
                a1 = c;
            } else if (!beamModeMid(bm) && bm == BEGIN) {
                a1 = c;
            } else {
                beams.push_back({ staff, voice, true, { a1->id, c->id } });
                beam = int(beams.size()) - 1;
                a1 = nullptr;
            }
        }
        finish(beam);
    }

    // ---- BeamLayout::createBeams ----
    void run()
    {
        const auto& measures = ctx.measures();
        for (const StaffInfo& st : ctx.staves()) {
            if (!st.shown) {
                continue;   // (tablature without stems too, once its staff types are read)
            }
            for (int vn = 0; vn < 4; ++vn) {
                const Id v = st.voices[size_t(vn)];
                if (!v.isValid()) {
                    continue;
                }
                crVoice = vn;
                // the voice's chords and rests by measure, grace chords with their chord
                std::vector<std::vector<CR> > byMeasure(measures.size());
                std::vector<CR> pendingBefore;
                for (Id e : ctx.obj(v)->children()) {
                    const Object* ev = ctx.obj(e);
                    if (!ev || (ev->kind() != Kind::Chord && ev->kind() != Kind::Rest)) {
                        continue;
                    }
                    const Fraction t = ev->fractionOr(F::TIME, Fraction());
                    auto mit = std::upper_bound(measures.begin(), measures.end(), t,
                                                [](const Fraction& x, const MeasureInfo& m) { return x < m.start; });
                    const size_t mi = mit == measures.begin() ? 0 : size_t(mit - measures.begin() - 1);
                    CR c = makeCR(ev, measures[mi]);
                    if (c.grace) {
                        if (ev->text(F::GRACE_TYPE).find("after") != std::string::npos) {
                            if (!byMeasure[mi].empty()) {
                                byMeasure[mi].back().after.push_back(c);
                            }
                        } else {
                            pendingBefore.push_back(c);
                        }
                        continue;
                    }
                    c.before = std::move(pendingBefore);
                    pendingBefore.clear();
                    byMeasure[mi].push_back(std::move(c));
                }
                const CR* lastOfPrevious = nullptr;   // the last chord or rest before this measure
                int lastBeam = -1;                    // and its beam
                for (size_t mi = 0; mi < measures.size(); ++mi) {
                    measure(st, vn, mi, byMeasure[mi], lastOfPrevious, lastBeam);
                    if (!byMeasure[mi].empty()) {
                        lastOfPrevious = &byMeasure[mi].back();
                        lastBeam = byMeasure[mi].back().beam;
                    }
                }
            }
        }
        beams.erase(std::remove_if(beams.begin(), beams.end(), [](const Beam& b) { return b.elements.size() < 2; }), beams.end());
    }

    void measure(const StaffInfo& st, int vn, size_t mi, std::vector<CR>& crs, const CR* prevCR, int prevBeam)
    {
        const MeasureInfo& m = ctx.measures()[mi];
        const Object* mo = ctx.obj(m.id);
        // Measure::anacrusisOffset: a short first measure (or after a break) counts from its end
        Fraction anacrusis;
        const Fraction nominal(mo->intOr(F::TIMESIG_NUM, 4), mo->intOr(F::TIMESIG_DEN, 4));
        const Object* prevMeasure = mi > 0 ? ctx.obj(ctx.measures()[mi - 1].id) : nullptr;
        if ((mo->has(F::IRREGULAR) || !prevMeasure || frames.count(m.start) || hasBreak(prevMeasure)) && m.length < nominal) {
            anacrusis = nominal - m.length;
        }
        const Groups g = groupsAt(st.index, m.start, m);
        const TimeSig* ts = timeSigAt(st.index, m.start);
        const bool checkBeats = ts && ts->den == 4;
        std::unordered_map<int, std::pair<int, int> > beatSub;
        if (checkBeats) {
            for (const CR& c : crs) {
                const int beat = ticksOf(c.rtick * c.stretch) / 480;
                auto it = beatSub.find(beat);
                if (it == beatSub.end() || durationLess(c.type, c.dots, it->second.first, it->second.second)) {
                    beatSub[beat] = { c.type, c.dots };
                }
            }
        }
        CR* a1 = nullptr;
        int beam = -1;
        const CR* prev = nullptr;
        bool first = true;
        for (CR& item : crs) {
            CR* cr = &item;
            if (first) {
                first = false;
                // a beam continued from the previous measure
                const int mode = cr->beamMode;
                if ((mode == MID || mode == END || mode == BEGIN16 || mode == BEGIN32) && prevCR && prevMeasure
                    && !beamNoContinue(prevCR->beamMode) && !hasBreak(ctx.obj(measureOf(prevCR->tick).id))
                    && !(!prevCR->rest && prevCR->type <= V_QUARTER)) {
                    if (prevBeam >= 0 && !beams[size_t(prevBeam)].elements.empty()) {
                        beam = prevBeam;
                    } else if (!prevCR->unbeamed) {
                        // MuseScore takes the previous measure's last chord or rest as the beam's start, but lays
                        // that measure out again afterwards, which takes out a chord or rest it leaves unbeamed
                        // (beam mode NONE: an unbeamed rest, MuseScore's vtest beams-2 and beams-19)
                        a1 = const_cast<CR*>(prevCR);
                    }
                }
            }
            if (!cr->rest) {
                beamGraces(cr->before, st.index, vn);
                beamGraces(cr->after, st.index, vn);
            }
            const int bm = actualBeamMode(*cr, prev, beatSub, g, anacrusis, checkBeats);
            prev = cr;
            const bool canBeBeamed = cr->type > V_QUARTER || cr->rest;
            if (!canBeBeamed || bm == NONE) {
                cr->unbeamed = true;
                finish(beam);
                a1 = nullptr;
                continue;
            }
            if (beam >= 0) {
                bool end = bm == BEGIN;
                if (!end) {
                    beams[size_t(beam)].elements.push_back(cr->id);
                    cr->beam = beam;
                    cr = nullptr;
                    end = bm == END;
                }
                if (end) {
                    finish(beam);
                }
            }
            if (!cr) {
                continue;
            }
            if (!a1) {
                a1 = cr;
            } else if (!beamModeMid(bm) && (bm == BEGIN || a1->endTick() < cr->tick)) {
                a1 = cr;
            } else {
                beams.push_back({ st.index, vn, false, { a1->id, cr->id } });
                beam = int(beams.size()) - 1;
                a1->beam = beam;
                cr->beam = beam;
                a1 = nullptr;
            }
        }
        // a beam still open at the barline goes on into the next measure only if its first chord there asks for it
        if (beam >= 0) {
            const Beam& b = beams[size_t(beam)];
            if (b.elements.size() <= 1) {
                beams[size_t(beam)].elements.clear();
            }
        }
    }
};
}

std::vector<Beam> layoutBeams(const ScoreContext& ctx, const std::vector<ChordLayout>& chords)
{
    Beamer b(ctx, chords);
    b.run();
    return b.beams;
}
}
