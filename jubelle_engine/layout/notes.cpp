/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::layout). Not part of MuseScore.
 *
 * Ported from MuseScore's ModifyDom::cmdUpdateNotes (with Jubelle's courtesy accidentals), Chord::cmdUpdateNotes,
 * Note::updateAccidental, AccidentalState (dom/key.cpp), Note::updateRelLine and Note::noteHead (GPL-3.0).
 */
#include "notes.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <unordered_map>

#include "model/detailskey.h"
#include "model/fields.h"
#include "model/fragment.h"

namespace je::layout {
namespace F = model::fields;
using model::Kind;
using model::Object;

namespace {
constexpr int MAX_ACC_STATE = 75;
constexpr int TIE_CONTEXT = 0x10;

//! AccidentalState (with Jubelle's courtesy marks)
class AccidentalState
{
public:
    void init(const KeyEvent& k)
    {
        std::memset(m_state, 3, sizeof(m_state));   // natural (alteration − MIN, MIN = −3)
        if (k.key > 0 && k.key <= 7) {
            for (int i = 0; i < k.key; ++i) {
                const int idx = tpc2step(TPC_F_S + i);
                for (int o = 0; o < 77; o += 7) {
                    if (idx + o >= MAX_ACC_STATE) {
                        break;
                    }
                    m_state[idx + o] = 4;
                }
            }
        } else if (k.key < 0 && k.key >= -7) {
            for (int i = 0; i > k.key; --i) {
                const int idx = tpc2step(TPC_B_B + i);
                for (int o = 0; o < 77; o += 7) {
                    if (idx + o >= MAX_ACC_STATE) {
                        break;
                    }
                    m_state[idx + o] = 2;
                }
            }
        }
        if (k.custom) {
            for (const KeyEvent::CustDef& d : k.defs) {
                const int degree = (d.degree + (k.key + 7) * 4) % 7;   // KeySigEvent::degInKey
                const SymId sym = symInKey(k, d.sym, d.degree);
                int alter = 0;
                for (int i = 0; i < style::ACCIDENTAL_COUNT; ++i) {
                    if (style::ACCIDENTALS[i].sym == sym) {
                        alter = style::ACCIDENTALS[i].alteration;
                        break;
                    }
                }
                for (int o = 0; o < 77; o += 7) {
                    if (degree + o >= MAX_ACC_STATE) {
                        break;
                    }
                    m_state[degree + o] = uint8_t(alter + 3);
                }
            }
        }
        m_force.fill(false);
        m_courtesy.fill(false);
    }
    static bool valid(int line) { return line >= 0 && line < MAX_ACC_STATE; }
    int value(int line) const { return (m_state[line] & 0x0f) - 3; }
    bool tieContext(int line) const { return m_state[line] & TIE_CONTEXT; }
    bool force(int line) const { return m_force[size_t(line)]; }
    bool courtesy(int line) const { return valid(line) && m_courtesy[size_t(line)]; }
    void set(int line, int alter, bool tieContext = false) { m_state[line] = uint8_t((alter + 3) | (tieContext ? TIE_CONTEXT : 0)); }
    void setForce(int line, bool f)
    {
        m_force[size_t(line)] = f;
        if (!f) {
            m_courtesy[size_t(line)] = false;
        }
    }
    void setCourtesy(int line, bool v)
    {
        if (valid(line)) {
            m_courtesy[size_t(line)] = v;
        }
    }

private:
    static SymId symInKey(const KeyEvent& k, SymId sym, int degree)
    {
        degree = (degree + (k.key + 7) * 4) % 7;
        const int keyval = k.key;
        int index = 0;
        while (index < style::ACCIDENTAL_ORDER_COUNT && style::ACCIDENTAL_ORDER[index] != sym) {
            ++index;
        }
        if (keyval == 0 || std::abs(keyval) > 7) {
            return sym;
        }
        if (index >= style::ACCIDENTAL_ORDER_COUNT) {
            return SymId::noSym;
        }
        for (int i = 1; i <= std::abs(keyval); ++i) {
            if ((degree * 2 + 2) % 7 == (keyval < 0 ? 8 - i : i) % 7) {
                index += keyval < 0 ? -1 : 1;
            }
        }
        return style::ACCIDENTAL_ORDER[index];
    }

    uint8_t m_state[MAX_ACC_STATE] = {};
    std::array<bool, MAX_ACC_STATE> m_force {};
    std::array<bool, MAX_ACC_STATE> m_courtesy {};
};

int accidentalIndex(SymId sym)
{
    for (int i = 1; i < style::ACCIDENTAL_COUNT; ++i) {
        if (style::ACCIDENTALS[i].sym == sym) {
            return i;
        }
    }
    return -1;
}

SymId accidentalOfAlter(int alter)
{
    // Accidental::value2subtype (natural is NONE there; callers add it)
    switch (alter) {
    case 1: return SymId::accidentalSharp;
    case 2: return SymId::accidentalDoubleSharp;
    case 3: return SymId::accidentalTripleSharp;
    case -1: return SymId::accidentalFlat;
    case -2: return SymId::accidentalDoubleFlat;
    case -3: return SymId::accidentalTripleFlat;
    default: return SymId::noSym;
    }
}

//! A value in an object's details, before or after its music (DETAILS, then DETAILS_AFTER)
std::string detail(const Object* o, const std::string& tag)
{
    std::string v = model::fragment::value(o->text(F::DETAILS), tag);
    return v.empty() ? model::fragment::value(o->text(F::DETAILS_AFTER), tag) : v;
}

SymId symByName(const std::string& n)
{
    static const std::unordered_map<std::string, SymId> names = [] {
        std::unordered_map<std::string, SymId> m;
        for (int i = 0; i < style::SYM_COUNT; ++i) {
            m.emplace(style::SYMBOL_CODES[i].name, SymId(i));
        }
        return m;
    }();
    const auto it = names.find(n);
    return it == names.end() ? SymId::noSym : it->second;
}

//! A note being worked out
struct Work {
    const Object* note = nullptr;
    int pitch = 0, tpc1 = 0, tpc = 0, epitch = 0;
    SymId accidental = SymId::noSym;   // its accidental now (as read, then as worked out)
    bool user = false;                 // AccidentalRole::USER
    double centOffset = 0.0;
    const Work* tieBack = nullptr;
    bool fixed = false;
    int fixedLine = 0;
    int line = 0;
    bool courtesy = false;
};

struct ChordWork {
    ChordLayout out;
    std::vector<Work*> notes;
};

struct Builder {
    const ScoreContext& ctx;
    std::vector<Work> works;
    std::vector<ChordWork> chords;
    // per measure, per staff: the chords drawn there, by part (index into chords); grace chords with their chord
    struct Group {
        std::vector<size_t> before;
        size_t main = 0;
        std::vector<size_t> after;
        Fraction tick;
        int staff = 0, voice = 0;
    };
    std::vector<std::vector<Group> > groupsByMeasure;   // in score order

    explicit Builder(const ScoreContext& c)
        : ctx(c) {}

    void collect()
    {
        const auto& measures = ctx.measures();
        groupsByMeasure.assign(measures.size(), {});
        std::map<Id, size_t> workOf;
        std::vector<std::pair<Id, Id> > ties;   // (from, to)
        size_t noteCount = 0;
        for (const StaffInfo& st : ctx.staves()) {
            for (Id v : st.voices) {
                if (!v.isValid()) {
                    continue;
                }
                for (Id e : ctx.obj(v)->children()) {
                    const Object* ev = ctx.obj(e);
                    if (ev && ev->kind() == Kind::Chord) {
                        for (Id n : ev->children()) {
                            noteCount += ctx.obj(n)->kind() == Kind::Note;
                        }
                    }
                }
            }
        }
        works.reserve(noteCount);
        for (const StaffInfo& st : ctx.staves()) {
            for (int vn = 0; vn < 4; ++vn) {
                const Id v = st.voices[size_t(vn)];
                if (!v.isValid()) {
                    continue;
                }
                std::vector<size_t> pendingBefore;
                bool hasLast = false;
                size_t lastMeasure = 0, lastGroup = 0;
                for (Id e : ctx.obj(v)->children()) {
                    const Object* ev = ctx.obj(e);
                    if (!ev || (ev->kind() != Kind::Chord && ev->kind() != Kind::Rest)) {
                        continue;
                    }
                    if (ev->kind() == Kind::Rest) {
                        pendingBefore.clear();
                        hasLast = false;
                        continue;
                    }
                    const bool grace = ev->has(F::GRACE);
                    const bool after = grace && ev->text(F::GRACE_TYPE).find("after") != std::string::npos;
                    ChordWork cw;
                    cw.out.chord = e;
                    cw.out.staff = st.index;
                    cw.out.voice = vn;
                    cw.out.tick = ev->fractionOr(F::TIME, Fraction());
                    const int move = int(ev->intOr(F::STAFF_MOVE, 0));
                    cw.out.shownStaff = st.index;
                    if (move != 0) {
                        // within the instrument's staves
                        auto it = std::find(st.partStaves.begin(), st.partStaves.end(), st.index);
                        const long pos = long(it - st.partStaves.begin()) + move;
                        if (pos >= 0 && pos < long(st.partStaves.size())) {
                            cw.out.shownStaff = st.partStaves[size_t(pos)];
                        }
                    }
                    cw.out.shown = st.shown;
                    cw.out.stemless = ctx.stemlessMeasure(st.index, cw.out.tick);
                    {
                        // Chord::cmdUpdateNotes: tablature is fretted, a drum set's notes come from it
                        const StaffInfo& shown = ctx.staves()[size_t(cw.out.shownStaff)];
                        cw.out.standard = (shown.group == "pitched" || shown.group.empty()) && !shown.drumset;
                    }
                    for (Id n : ev->children()) {
                        const Object* note = ctx.obj(n);
                        if (!note || note->kind() != Kind::Note) {
                            continue;
                        }
                        Work w;
                        w.note = note;
                        w.pitch = int(note->intOr(F::PITCH, 60));
                        w.tpc1 = int(note->intOr(F::TPC, 14));
                        const int tpc2 = int(note->intOr(F::TPC2, w.tpc1));
                        w.tpc = ctx.concertPitch() ? w.tpc1 : tpc2;
                        const ScoreContext::Transposition tr = ctx.transpositionAt(st.index, cw.out.tick);
                        w.epitch = w.pitch - (ctx.concertPitch() ? 0 : tr.chromatic);
                        const bool fresh = !note->has(F::DETAILS_KEY) || model::detailsKey(*note) == note->intOr(F::DETAILS_KEY, 0);
                        if (fresh) {
                            const std::string acc = detail(note, "Accidental");
                            if (!acc.empty()) {
                                w.accidental = symByName(model::fragment::value("<x>" + acc + "</x>", "subtype"));
                                w.user = model::fragment::value("<x>" + acc + "</x>", "role") == "1";
                            }
                            w.fixed = detail(note, "fixed") == "1";
                            w.fixedLine = std::atoi(detail(note, "fixedLine").c_str());
                            // Note::add: a note's cent offset is its accidental's
                            const int ai = accidentalIndex(w.accidental);
                            w.centOffset = ai > 0 ? style::ACCIDENTALS[ai].cents : 0.0;
                        }
                        workOf[n] = works.size();
                        if (note->has(F::TIE_TO)) {
                            ties.emplace_back(n, note->idOr(F::TIE_TO));
                        }
                        works.push_back(w);
                        cw.notes.push_back(&works.back());
                    }
                    if (cw.notes.empty()) {
                        continue;
                    }
                    const size_t index = chords.size();
                    // the measure of its time
                    const auto& ms = ctx.measures();
                    auto mit = std::upper_bound(ms.begin(), ms.end(), cw.out.tick,
                                                [](const Fraction& t, const MeasureInfo& m) { return t < m.start; });
                    const size_t mi = mit == ms.begin() ? 0 : size_t(mit - ms.begin() - 1);
                    if (after) {
                        cw.out.kind = 'A';
                        chords.push_back(std::move(cw));
                        if (hasLast) {
                            Group& last = groupsByMeasure[lastMeasure][lastGroup];
                            chords.back().out.tick = last.tick;
                            last.after.push_back(index);
                        }
                        continue;
                    }
                    if (grace) {
                        cw.out.kind = 'G';
                        chords.push_back(std::move(cw));
                        pendingBefore.push_back(index);
                        continue;
                    }
                    chords.push_back(std::move(cw));
                    Group g;
                    g.before = pendingBefore;
                    g.main = index;
                    g.tick = chords[index].out.tick;
                    g.staff = st.index;
                    g.voice = vn;
                    for (size_t b : pendingBefore) {
                        chords[b].out.tick = g.tick;
                    }
                    pendingBefore.clear();
                    groupsByMeasure[mi].push_back(g);
                    hasLast = true;
                    lastMeasure = mi;
                    lastGroup = groupsByMeasure[mi].size() - 1;
                }
            }
        }
        for (const auto& [from, to] : ties) {
            auto f = workOf.find(from);
            auto t = workOf.find(to);
            if (f != workOf.end() && t != workOf.end()) {
                works[t->second].tieBack = &works[f->second];
            }
        }
        // MuseScore's order within a measure: time, then track (staff, voice)
        for (auto& groups : groupsByMeasure) {
            std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) {
                if (a.tick != b.tick) {
                    return a.tick < b.tick;
                }
                return a.staff != b.staff ? a.staff < b.staff : a.voice < b.voice;
            });
        }
    }

    // ---- Note::updateAccidental ----
    bool hasAlteredUnison(const ChordWork& c, const Work* w) const
    {
        const int alter = tpc2alter(w->tpc);
        const int line = absStep(w->tpc, w->epitch);
        for (const Work* n : c.notes) {
            if (n != w && absStep(n->tpc, n->epitch) == line && tpc2alter(n->tpc) != alter) {
                return true;
            }
        }
        return false;
    }

    void updateAccidental(const ChordWork& c, Work* w, AccidentalState& as, int staff)
    {
        const int absLine = absStep(w->tpc, w->epitch);
        const int ai = accidentalIndex(w->accidental);
        if (w->centOffset == 0.0) {
            if (ai > 0 && style::ACCIDENTALS[ai].cents != 0.0) {
                w->accidental = SymId::noSym;   // a microtonal accidental without its offset is removed
            }
        }
        const int now = accidentalIndex(w->accidental);
        const bool microtonal = now > 7;   // Accidental::isMicrotonal: after FLAT3
        if (!microtonal) {
            SymId acci = SymId::noSym;
            const int accVal = tpc2alter(w->tpc);
            if (!AccidentalState::valid(absLine)) {
                return;   // (MuseScore: "error accidentalVal()", the note left as it is)
            }
            const int lineVal = as.value(absLine);
            const bool courtesyOnly = as.courtesy(absLine) && as.force(absLine) && accVal == lineVal && !as.tieContext(absLine);
            if (accVal != lineVal || as.tieContext(absLine) || as.force(absLine)) {
                as.set(absLine, accVal, w->tieBack && w->accidental == SymId::noSym);
                acci = accidentalOfAlter(accVal);
                if (w->tieBack && w->tieBack->tpc1 == w->tpc1) {
                    acci = SymId::noSym;
                } else if (acci == SymId::noSym) {
                    acci = SymId::accidentalNatural;
                }
            } else if (hasAlteredUnison(c, w)) {
                acci = accidentalOfAlter(accVal);
                if (acci == SymId::noSym) {
                    acci = SymId::accidentalNatural;
                }
            }
            if (acci != SymId::noSym) {
                w->accidental = acci;   // a new automatic one, or the kept one changed (its role kept)
            } else if (w->accidental != SymId::noSym) {
                if (!w->user) {
                    w->accidental = SymId::noSym;   // an automatic one no longer needed
                } else {
                    SymId keep = accidentalOfAlter(accVal);
                    w->accidental = keep == SymId::noSym ? SymId::accidentalNatural : keep;
                }
            }
            w->courtesy = courtesyOnly && w->accidental != SymId::noSym && !w->user;
        } else {
            // microtonal accidentals count as their alteration
            as.set(absLine, style::ACCIDENTALS[now].alteration, w->tieBack && w->accidental == SymId::noSym);
        }
        if (AccidentalState::valid(absLine)) {
            as.setForce(absLine, false);
        }
        // updateRelLine: the line on the staff the chord is shown on
        const style::ClefDef& clef = ctx.clefInfo(ctx.clefAt(staff, c.out.tick));
        w->line = clef.pitchOffset - absLine;
    }

    void updateChord(ChordWork& c, AccidentalState& as, int staff)
    {
        for (Work* w : c.notes) {
            // a tied note of the same spelling: its automatic accidental goes
            if (w->tieBack && w->tpc == w->tieBack->tpc && w->accidental != SymId::noSym && !w->user) {
                w->accidental = SymId::noSym;
            }
            updateAccidental(c, w, as, staff);
        }
    }

    // ---- Jubelle: automatic courtesy accidentals (ModifyDom's markCourtesyAccidentals) ----
    std::vector<int> courtesyStaves(int staff) const
    {
        std::vector<int> r { staff };
        if (ctx.style().boolean(Sid::jubelleCourtesyAccidentalsOtherStaves)) {
            for (int s : ctx.staves()[size_t(staff)].partStaves) {
                if (s != staff && ctx.staves()[size_t(s)].group != "tablature") {
                    r.push_back(s);
                }
            }
        }
        return r;
    }

    //! Calls f(chord) for the chords (grace chords included) of a measure drawn on staff `on`, in time order;
    //! key(t) at each key signature on that staff
    template<typename KeyFn, typename ChordFn>
    void forEachOn(size_t measure, int on, KeyFn key, ChordFn f)
    {
        const MeasureInfo& m = ctx.measures()[measure];
        const std::vector<Fraction> keys = ctx.keyChanges(on, m.start, m.end());
        size_t k = 0;
        for (const Group& g : groupsByMeasure[measure]) {
            if (ctx.staves()[size_t(g.staff)].player != ctx.staves()[size_t(on)].player) {
                continue;
            }
            while (k < keys.size() && keys[k] <= g.tick) {
                key(keys[k++]);
            }
            for (size_t b : g.before) {
                if (chords[b].out.shownStaff == on) {
                    f(chords[b], g.tick);
                }
            }
            if (chords[g.main].out.shownStaff == on) {
                f(chords[g.main], g.tick);
            }
            for (size_t a : g.after) {
                if (chords[a].out.shownStaff == on) {
                    f(chords[a], g.tick);
                }
            }
        }
        while (k < keys.size()) {
            key(keys[k++]);
        }
    }

    void forceCourtesy(AccidentalState& as, int line, int seen, bool allOctaves)
    {
        auto mark = [&](int l) {
            if (seen != as.value(l)) {
                as.setForce(l, true);
                as.setCourtesy(l, true);
            } else if (as.courtesy(l)) {
                as.setForce(l, false);
            }
        };
        if (!allOctaves) {
            mark(line);
            return;
        }
        for (int other = line % 7; other < MAX_ACC_STATE; other += 7) {
            mark(other);
        }
    }

    void markCourtesy(size_t measure, int staff, AccidentalState& as)
    {
        const style::Style& st = ctx.style();
        if (!st.boolean(Sid::jubelleCourtesyAccidentals) || measure == 0) {
            return;
        }
        if (ctx.staves()[size_t(staff)].group != "pitched" && !ctx.staves()[size_t(staff)].group.empty()) {
            return;
        }
        const int bars = std::clamp(st.integer(Sid::jubelleCourtesyAccidentalsBars), 1, 4);
        std::vector<size_t> window;
        for (size_t m = measure; m > 0 && int(window.size()) < bars; --m) {
            window.insert(window.begin(), m - 1);
        }
        struct Pending {
            bool on = false;
            int val = 0;
            Fraction tick = Fraction(-1, 1);
        };
        std::array<Pending, MAX_ACC_STATE> pending {};
        for (int s : courtesyStaves(staff)) {
            std::array<Pending, MAX_ACC_STATE> mine {};
            for (size_t m : window) {
                AccidentalState state;
                state.init(ctx.keyAt(s, ctx.measures()[m].start));
                std::array<bool, MAX_ACC_STATE> altered {}, played {};
                std::array<Fraction, MAX_ACC_STATE> lastTick;
                lastTick.fill(Fraction(-1, 1));
                forEachOn(m, s, [&](const Fraction& t) {
                    // a key signature cancels what came before it
                    state.init(ctx.keyAt(s, t));
                    altered.fill(false);
                    mine.fill(Pending());
                }, [&](ChordWork& c, const Fraction& tick) {
                    for (const Work* w : c.notes) {
                        const int line = absStep(w->tpc, w->epitch);
                        if (!AccidentalState::valid(line)) {
                            continue;
                        }
                        const int val = tpc2alter(w->tpc);
                        if (val != state.value(line)) {
                            altered[size_t(line)] = true;
                        }
                        state.set(line, val);
                        played[size_t(line)] = true;
                        lastTick[size_t(line)] = tick;
                    }
                });
                for (int line = 0; line < MAX_ACC_STATE; ++line) {
                    if (played[size_t(line)]) {
                        mine[size_t(line)] = altered[size_t(line)] ? Pending { true, state.value(line), lastTick[size_t(line)] } : Pending();
                    }
                }
            }
            for (int line = 0; line < MAX_ACC_STATE; ++line) {
                if (mine[size_t(line)].on && (!pending[size_t(line)].on || mine[size_t(line)].tick > pending[size_t(line)].tick)) {
                    pending[size_t(line)] = mine[size_t(line)];
                }
            }
        }
        const bool allOctaves = st.boolean(Sid::jubelleCourtesyAccidentalsAllOctaves);
        for (int line = 0; line < MAX_ACC_STATE; ++line) {
            if (pending[size_t(line)].on) {
                forceCourtesy(as, line, pending[size_t(line)].val, allOctaves);
            }
        }
    }

    struct StaffEvent {
        Fraction tick;
        int line = 0, val = 0;
    };
    std::vector<StaffEvent> otherStaffEvents(size_t measure, int staff)
    {
        std::vector<StaffEvent> events;
        const style::Style& st = ctx.style();
        if (!st.boolean(Sid::jubelleCourtesyAccidentals) || !st.boolean(Sid::jubelleCourtesyAccidentalsOtherStaves)
            || (ctx.staves()[size_t(staff)].group != "pitched" && !ctx.staves()[size_t(staff)].group.empty())) {
            return events;
        }
        for (int s : courtesyStaves(staff)) {
            if (s == staff) {
                continue;
            }
            AccidentalState state;
            state.init(ctx.keyAt(s, ctx.measures()[measure].start));
            forEachOn(measure, s, [&](const Fraction& t) { state.init(ctx.keyAt(s, t)); }, [&](ChordWork& c, const Fraction& tick) {
                for (const Work* w : c.notes) {
                    const int line = absStep(w->tpc, w->epitch);
                    if (!AccidentalState::valid(line)) {
                        continue;
                    }
                    const int val = tpc2alter(w->tpc);
                    if (val != state.value(line)) {
                        events.push_back({ tick, line, val });
                    }
                    state.set(line, val);
                }
            });
        }
        std::stable_sort(events.begin(), events.end(), [](const StaffEvent& a, const StaffEvent& b) { return a.tick < b.tick; });
        return events;
    }

    // ---- ModifyDom::cmdUpdateNotes ----
    void updateNotes()
    {
        const bool allOctaves = ctx.style().boolean(Sid::jubelleCourtesyAccidentalsAllOctaves);
        for (size_t mi = 0; mi < ctx.measures().size(); ++mi) {
            const MeasureInfo& m = ctx.measures()[mi];
            for (const StaffInfo& staff : ctx.staves()) {
                if (staff.group == "percussion" || staff.group == "tablature" || staff.drumset || !staff.shown) {
                    continue;   // drum sets and fretting: not yet
                }
                AccidentalState as;
                as.init(ctx.keyAt(staff.index, m.start));
                markCourtesy(mi, staff.index, as);
                const std::vector<StaffEvent> others = otherStaffEvents(mi, staff.index);
                size_t nextOther = 0;
                Fraction lastTick(-1, 1);
                forEachOn(mi, staff.index, [&](const Fraction& t) { as.init(ctx.keyAt(staff.index, t)); },
                          [&](ChordWork& c, const Fraction& tick) {
                    if (tick != lastTick && !others.empty()) {
                        std::map<int, int> latest;
                        while (nextOther < others.size() && others[nextOther].tick <= tick) {
                            latest[others[nextOther].line] = others[nextOther].val;
                            ++nextOther;
                        }
                        for (const auto& [line, val] : latest) {
                            forceCourtesy(as, line, val, allOctaves);
                        }
                    }
                    lastTick = tick;
                    updateChord(c, as, staff.index);
                });
            }
        }
    }

    //! The written value with its dots (Chord::actualDurationType)
    static Fraction written(const Object* chord)
    {
        const Fraction base = chord->fractionOr(F::BASE_DURATION, chord->fractionOr(F::DURATION, Fraction(1, 4)));
        Fraction v = base, add = base;
        for (int64_t i = 0; i < chord->intOr(F::DOTS, 0); ++i) {
            add = add * Fraction(1, 2);
            v = v + add;
        }
        return v;
    }

    //! A length as one note value (toDurationList with up to 4 dots gives one value); its undotted value and dots
    static bool singleValue(const Fraction& l, Fraction& base, int& dotsOut)
    {
        for (Fraction b(4, 1); b >= Fraction(1, 1024); b = b * Fraction(1, 2)) {
            Fraction v = b, add = b;
            for (int dots = 0; dots <= 4; ++dots) {
                if (v == l) {
                    base = b;
                    dotsOut = dots;
                    return true;
                }
                add = add * Fraction(1, 2);
                v = v + add;
            }
        }
        return false;
    }

public:
    //! MuseScore's DurationType of a note value (0 longa, 1 breve, 2 whole … 12 1024th)
    static int durationTypeOf(const Fraction& base)
    {
        int i = 0;
        for (Fraction b(4, 1); b > base && i < 14; b = b * Fraction(1, 2)) {
            ++i;
        }
        return i;
    }

    size_t measureOf(const Fraction& t) const
    {
        const auto& ms = ctx.measures();
        auto it = std::upper_bound(ms.begin(), ms.end(), t, [](const Fraction& x, const MeasureInfo& m) { return x < m.start; });
        return it == ms.begin() ? 0 : size_t(it - ms.begin() - 1);
    }

    // ---- Note::noteHead (the normal scheme) ----
    void heads()
    {
        static const char* typeNames[] = { "whole", "half", "quarter", "breve" };
        // ChordLayout::crossMeasureSetup (crossMeasureValues): a one-note chord tied into the next measure's one-note
        // chord is shown as their whole length, where that is one note value
        std::map<Id, size_t> chordOfNote;
        const bool crossMeasure = ctx.style().boolean(Sid::crossMeasureValues);
        if (crossMeasure) {
            for (size_t i = 0; i < chords.size(); ++i) {
                for (const Work* w : chords[i].notes) {
                    chordOfNote[w->note->id()] = i;
                }
            }
        }
        for (ChordWork& c : chords) {
            const Object* chord = ctx.obj(c.out.chord);
            c.out.dots = int(chord->intOr(F::DOTS, 0));
            c.out.durationType = durationTypeOf(chord->fractionOr(F::BASE_DURATION, chord->fractionOr(F::DURATION, Fraction(1, 4))));
        }
        if (crossMeasure) {
            for (ChordWork& c : chords) {
                const Object* chord = ctx.obj(c.out.chord);
                if (c.out.kind != 'C' || c.notes.size() != 1 || !c.notes[0]->note->has(F::TIE_TO) || c.out.crossMeasure == 2) {
                    continue;
                }
                const auto it = chordOfNote.find(c.notes[0]->note->idOr(F::TIE_TO));
                if (it == chordOfNote.end()) {
                    continue;
                }
                ChordWork& tied = chords[it->second];
                Fraction combined;
                int dots = 0;
                if (tied.notes.size() == 1 && measureOf(tied.out.tick) != measureOf(c.out.tick)) {
                    if (singleValue(written(chord) + written(ctx.obj(tied.out.chord)), combined, dots)) {
                        c.out.crossMeasure = 1;
                        c.out.durationType = durationTypeOf(combined);
                        c.out.dots = dots;
                        tied.out.crossMeasure = 2;
                    }
                }
            }
        }
        for (ChordWork& c : chords) {
            const Object* chord = ctx.obj(c.out.chord);
            const int dt = c.out.durationType;
            // TDuration::headType: breve and longa a breve, whole (and a measure) a whole, half a half, else a quarter
            int type = dt <= 1 ? 3 : dt == 2 ? 0 : dt == 3 ? 1 : 2;
            if (chord->has(F::MEASURE_REST)) {
                type = 0;
            }
            for (Work* w : c.notes) {
                NoteLayout nl;
                int t = type;
                const bool fresh = !w->note->has(F::DETAILS_KEY) || model::detailsKey(*w->note) == w->note->intOr(F::DETAILS_KEY, 0);
                auto d = [&](const char* tag) { return fresh ? detail(w->note, tag) : std::string(); };
                const std::string ht = d("headType");
                for (int i = 0; i < 4; ++i) {
                    if (ht == typeNames[i]) {
                        t = i;
                    }
                }
                std::string group = d("head");
                if (group.empty()) {
                    group = "normal";
                }
                const std::string scheme = d("headScheme");
                nl.headKnown = c.out.standard && (scheme.empty() || scheme == "normal" || scheme == "auto") && group != "custom";
                const style::NoteheadGroup* g = &style::NOTEHEAD_GROUPS[0];
                for (const style::NoteheadGroup* x = style::NOTEHEAD_GROUPS; x->name; ++x) {
                    if (group == x->name) {
                        g = x;
                    }
                }
                nl.headDown = g->heads[size_t(t)][0];
                nl.headUp = g->heads[size_t(t)][1];
                if (nl.headDown == SymId::noSym) {
                    nl.headDown = style::NOTEHEAD_GROUPS[0].heads[size_t(t)][0];
                }
                if (nl.headUp == SymId::noSym) {
                    nl.headUp = style::NOTEHEAD_GROUPS[0].heads[size_t(t)][1];
                }
                nl.note = w->note->id();
                nl.pitch = w->pitch;
                nl.tpc = w->tpc;
                nl.line = w->fixed ? w->fixedLine : w->line;
                nl.accidental = w->accidental;
                nl.userAccidental = w->user;
                nl.courtesy = w->courtesy;
                c.out.notes.push_back(nl);
            }
            // Chord::sortNotes: from the bottom line up, then by pitch
            std::stable_sort(c.out.notes.begin(), c.out.notes.end(), [](const NoteLayout& a, const NoteLayout& b) {
                return a.line != b.line ? a.line > b.line : a.pitch < b.pitch;
            });
        }
    }
};
}

std::vector<ChordLayout> layoutNotes(const ScoreContext& ctx)
{
    Builder b(ctx);
    b.collect();
    // every staff line once, before anything else uses it (tablature and drum staves keep the line of their clef)
    for (ChordWork& c : b.chords) {
        const style::ClefDef& clef = ctx.clefInfo(ctx.clefAt(c.out.shownStaff, c.out.tick));
        for (Work* w : c.notes) {
            w->line = clef.pitchOffset - absStep(w->tpc, w->epitch);
        }
    }
    b.updateNotes();
    b.heads();
    std::vector<ChordLayout> out;
    out.reserve(b.chords.size());
    for (size_t mi = 0; mi < b.groupsByMeasure.size(); ++mi) {
        for (const Builder::Group& g : b.groupsByMeasure[mi]) {
            for (size_t i : g.before) {
                out.push_back(b.chords[i].out);
            }
            out.push_back(b.chords[g.main].out);
            for (size_t i : g.after) {
                out.push_back(b.chords[i].out);
            }
        }
    }
    return out;
}
}
