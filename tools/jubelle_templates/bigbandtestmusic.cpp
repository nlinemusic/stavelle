/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "bigbandtestmusic.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "engraving/dom/articulation.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/chordline.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/hairpin.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/key.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/rehearsalmark.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/slur.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/tempotext.h"

using namespace mu::engraving;
using muse::String;

namespace jubelle_templates {
namespace {
constexpr int SIXTEENTH = 120;      // ticks (division 480)
constexpr int BAR = 16 * SIXTEENTH;

enum Art : unsigned {
    NONE = 0, ACC = 1, MARC = 2, STAC = 4, TEN = 8, FALL = 16, DOIT = 32
};

struct Ev {
    int start = 0;      // sixteenths from the bar start
    int len = 0;        // sixteenths
    unsigned art = NONE;
};

struct ChordSym {
    const char16_t* name;
    int root;               // pitch class
    std::vector<int> tones; // intervals
};

const std::vector<int> SIX { 0, 4, 7, 9 };
const std::vector<int> DOM { 0, 4, 7, 10 };
const std::vector<int> MIN7 { 0, 3, 7, 10 };
const std::vector<int> DIM7 { 0, 3, 6, 9 };

// An 8-bar progression in concert F; { bar in loop, sixteenth, chord }
struct Change { int bar; int at; ChordSym sym; };
const std::vector<Change>& changes()
{
    static const std::vector<Change> c {
        { 0, 0, { u"F6", 5, SIX } },
        { 1, 0, { u"D7", 2, DOM } },
        { 2, 0, { u"Gm7", 7, MIN7 } }, { 2, 8, { u"C7", 0, DOM } },
        { 3, 0, { u"F6", 5, SIX } },
        { 4, 0, { u"B♭7", 10, DOM } },
        { 5, 0, { u"B°7", 11, DIM7 } },
        { 6, 0, { u"Am7", 9, MIN7 } }, { 6, 8, { u"D7", 2, DOM } },
        { 7, 0, { u"Gm7", 7, MIN7 } }, { 7, 8, { u"C7", 0, DOM } },
    };
    return c;
}

const ChordSym& chordAt(int bar0, int at)
{
    const int b = bar0 % 8;
    const ChordSym* cur = &changes().front().sym;
    for (const Change& c : changes()) {
        if (c.bar < b || (c.bar == b && c.at <= at)) {
            cur = &c.sym;
        }
    }
    return *cur;
}

bool isChordTone(int pitch, const ChordSym& c)
{
    const int pc = ((pitch - c.root) % 12 + 12) % 12;
    return std::find(c.tones.begin(), c.tones.end(), pc) != c.tones.end();
}

int chordToneAtOrBelow(int pitch, const ChordSym& c)
{
    while (!isChordTone(pitch, c)) {
        --pitch;
    }
    return pitch;
}

//! Close voicing downwards from the top note: n different chord tones
std::vector<int> closeVoicing(int top, const ChordSym& c, int n)
{
    std::vector<int> v { chordToneAtOrBelow(top, c) };
    while (int(v.size()) < n) {
        v.push_back(chordToneAtOrBelow(v.back() - 1, c));
    }
    return v;
}

int rootIn(const ChordSym& c, int low)
{
    int p = low;
    while (((p - c.root) % 12 + 12) % 12 != 0) {
        ++p;
    }
    return p;
}

class Writer
{
public:
    explicit Writer(MasterScore* s)
        : m_score(s) {}

    Segment* seg(int tick)
    {
        return m_score->tick2segment(Fraction::fromTicks(tick), true, SegmentType::ChordRest);
    }

    Chord* chordAt(staff_idx_t staff, int tick, int voice = 0)
    {
        Segment* s = seg(tick);
        EngravingItem* e = s ? s->element(staff * VOICES + voice) : nullptr;
        return e && e->isChord() ? toChord(e) : nullptr;
    }

    //! One bar of one staff: events with pitch sets (empty set = rest)
    void bar(staff_idx_t staff, int bar0, const std::vector<std::pair<Ev, std::vector<int> > >& events, int voice = 0)
    {
        const track_idx_t track = staff * VOICES + voice;
        const int base = bar0 * BAR;
        int cursor = 0;
        auto put = [&](int at, int len, const std::vector<int>& pitches, unsigned art) {
            Segment* s = seg(base + at * SIXTEENTH);
            if (!s) {
                return;
            }
            m_score->setNoteRest(s, track, pitches.empty() ? NoteVal() : NoteVal(pitches[0]), Fraction(len, 16));
            if (pitches.empty()) {
                return;
            }
            Chord* c = chordAt(staff, base + at * SIXTEENTH, voice);
            if (!c) {
                return;
            }
            for (size_t i = 1; i < pitches.size(); ++i) {
                m_score->addNote(c, NoteVal(pitches[i]));
            }
            addArticulations(c, art);
        };
        for (const auto& [ev, pitches] : events) {
            if (ev.start > cursor) {
                put(cursor, ev.start - cursor, {}, NONE);
            }
            put(ev.start, ev.len, pitches, ev.art);
            cursor = ev.start + ev.len;
        }
        if (cursor < 16 && !events.empty()) {
            put(cursor, 16 - cursor, {}, NONE);
        }
    }

    void addArticulations(Chord* c, unsigned art)
    {
        auto addSym = [&](SymId id) {
            Articulation* a = Factory::createArticulation(c);
            a->setSymId(id);
            a->setParent(c);
            a->setTrack(c->track());
            m_score->undoAddElement(a);
        };
        if (art & ACC) {
            addSym(SymId::articAccentAbove);
        }
        if (art & MARC) {
            addSym(SymId::articMarcatoAbove);
        }
        if (art & STAC) {
            addSym(SymId::articStaccatoAbove);
        }
        if (art & TEN) {
            addSym(SymId::articTenutoAbove);
        }
        if (art & (FALL | DOIT)) {
            ChordLine* cl = Factory::createChordLine(c);
            cl->setChordLineType((art & FALL) ? ChordLineType::FALL : ChordLineType::DOIT);
            cl->setParent(c);
            cl->setNote(c->upNote());
            cl->setTrack(c->track());
            m_score->undoAddElement(cl);
        }
    }

    void slur(staff_idx_t staff, int tick1, int tick2)
    {
        Chord* a = chordAt(staff, tick1);
        Chord* b = chordAt(staff, tick2);
        if (!a || !b || a == b) {
            return;
        }
        Slur* s = Factory::createSlur(m_score->dummy());
        s->setStartElement(a);
        s->setEndElement(b);
        s->setTick(a->tick());
        s->setTick2(b->tick());
        s->setTrack(a->track());
        s->setTrack2(b->track());
        m_score->undoAddElement(s);
    }

    void hairpin(staff_idx_t staff, int tick1, int tick2, bool cresc)
    {
        Hairpin* h = Factory::createHairpin(m_score->dummy()->segment());
        h->setHairpinType(cresc ? HairpinType::CRESC_HAIRPIN : HairpinType::DIM_HAIRPIN);
        h->setTick(Fraction::fromTicks(tick1));
        h->setTick2(Fraction::fromTicks(tick2));
        h->setTrack(staff * VOICES);
        h->setTrack2(staff * VOICES);
        m_score->undoAddElement(h);
    }

    void dynamic(staff_idx_t staff, int tick, DynamicType t)
    {
        Segment* s = seg(tick);
        Dynamic* d = Factory::createDynamic(s);
        d->setDynamicType(t);
        d->setParent(s);
        d->setTrack(staff * VOICES);
        m_score->undoAddElement(d);
    }

    void harmony(staff_idx_t staff, int tick, const String& name)
    {
        Segment* s = seg(tick);
        Harmony* h = Factory::createHarmony(s);
        h->setTrack(staff * VOICES);
        h->setParent(s);
        h->setHarmony(name);
        m_score->undoAddElement(h);
    }

    void rehearsal(int tick, const String& text)
    {
        Segment* s = seg(tick);
        RehearsalMark* r = Factory::createRehearsalMark(s);
        r->setParent(s);
        r->setTrack(0);
        r->setXmlText(text);
        m_score->undoAddElement(r);
    }

    void tempo(int tick, const String& text, double bpm)
    {
        Segment* s = seg(tick);
        TempoText* t = Factory::createTempoText(s);
        t->setParent(s);
        t->setTrack(0);
        t->setXmlText(text);
        t->setTempo(BeatsPerSecond(bpm / 60.0));
        t->setFollowText(true);
        m_score->undoAddElement(t);
    }

    MasterScore* m_score;
};

// Staff indices in the Big Band template
constexpr staff_idx_t AS1 = 0, BS = 4, TP1 = 5, TB1 = 9, BTB = 12, PNO_R = 13, PNO_L = 14, GTR = 15, BASS = 16, DRUMS = 17;

using Events = std::vector<std::pair<Ev, std::vector<int> > >;

//! The same rhythm for a whole section, each event voiced from its top note
void section(Writer& w, staff_idx_t first, int count, int bar0, const std::vector<Ev>& rhythm,
             const std::vector<int>& tops, int extraLowVoice = 0)
{
    std::vector<Events> perStaff(count);
    for (size_t i = 0; i < rhythm.size(); ++i) {
        const ChordSym& c = chordAt(bar0, rhythm[i].start);
        const int top = tops[i % tops.size()];
        std::vector<int> v = closeVoicing(top, c, std::min(count, 4));
        if (count == 5) {
            v.push_back(top - 12);                  // baritone doubles the lead an octave lower
        }
        if (extraLowVoice && count == 4) {
            v[3] = rootIn(c, extraLowVoice);        // bass trombone on the root
        }
        for (int s = 0; s < count; ++s) {
            perStaff[s].push_back({ rhythm[i], { v[s] } });
        }
    }
    for (int s = 0; s < count; ++s) {
        w.bar(first + s, bar0, perStaff[s]);
    }
}

void rhythmSection(Writer& w, int bar0, bool ballad)
{
    const ChordSym& c1 = chordAt(bar0, 0);
    const ChordSym& c2 = chordAt(bar0, 8);
    // Piano: right-hand comping, left-hand shells
    const std::vector<int> v1 = closeVoicing(72, c1, 4);
    const std::vector<int> v2 = closeVoicing(72, c2, 4);
    if (ballad) {
        w.bar(PNO_R, bar0, { { { 0, 8, NONE }, v1 }, { { 8, 8, NONE }, v2 } });
        w.bar(PNO_L, bar0, { { { 0, 8, NONE }, { rootIn(c1, 41), rootIn(c1, 41) + 10 } },
                       { { 8, 8, NONE }, { rootIn(c2, 41), rootIn(c2, 41) + 10 } } });
    } else {
        w.bar(PNO_R, bar0, { { { 0, 3, ACC }, v1 }, { { 6, 2, NONE }, v1 }, { { 10, 2, STAC }, v2 }, { { 12, 4, NONE }, v2 } });
        w.bar(PNO_L, bar0, { { { 0, 8, NONE }, { rootIn(c1, 41), rootIn(c1, 41) + 10 } },
                       { { 8, 8, NONE }, { rootIn(c2, 41), rootIn(c2, 41) + 10 } } });
    }
    // Guitar: rhythm slashes (written B4, sounding B3), chord symbols above
    Events slashes;
    for (int q = 0; q < 4; ++q) {
        slashes.push_back({ { q * 4, 4, NONE }, { 59 } });
    }
    w.bar(GTR, bar0, slashes);
    for (int q = 0; q < 4; ++q) {
        if (Chord* ch = w.chordAt(GTR, bar0 * BAR + q * 4 * SIXTEENTH)) {
            for (Note* n : ch->notes()) {
                n->setHeadGroup(NoteHeadGroup::HEAD_SLASH);
                n->setFixed(true);
                n->setFixedLine(4);
                n->setPlay(false);
            }
            ch->setNoStem(true);
        }
    }
    // Bass: walking quarters (two-feel in the ballad)
    const int r1 = rootIn(c1, 31), r2 = rootIn(c2, 31);
    const int next = rootIn(chordAt(bar0 + 1, 0), 31);
    if (ballad) {
        w.bar(BASS, bar0, { { { 0, 8, NONE }, { r1 } }, { { 8, 8, NONE }, { r2 + 7 > 47 ? r2 - 5 : r2 + 7 } } });
    } else {
        w.bar(BASS, bar0, { { { 0, 4, NONE }, { r1 } }, { { 4, 4, NONE }, { r1 + c1.tones[1] } },
                      { { 8, 4, NONE }, { r2 + c2.tones[2] } }, { { 12, 4, NONE }, { next + 1 } } });
    }
    // Drums: ride pattern (voice 1), hi-hat on 2 and 4 (voice 2)
    if (ballad) {
        w.bar(DRUMS, bar0, { { { 0, 4, NONE }, { 38 } }, { { 4, 4, NONE }, { 38 } }, { { 8, 4, NONE }, { 38 } },
                       { { 12, 4, NONE }, { 38 } } });
    } else {
        w.bar(DRUMS, bar0, { { { 0, 4, NONE }, { 51 } }, { { 4, 2, NONE }, { 51 } }, { { 6, 2, NONE }, { 51 } },
                       { { 8, 4, NONE }, { 51 } }, { { 12, 2, NONE }, { 51 } }, { { 14, 2, NONE }, { 51 } } });
    }
    w.bar(DRUMS, bar0, { { { 4, 4, NONE }, { 44 } }, { { 12, 4, NONE }, { 44 } } }, 1);
}

void chordSymbols(Writer& w, int bar0)
{
    for (const Change& c : changes()) {
        if (c.bar == bar0 % 8) {
            const int tick = bar0 * BAR + c.at * SIXTEENTH;
            w.harmony(PNO_R, tick, c.sym.name);
            w.harmony(GTR, tick, c.sym.name);
        }
    }
}
}

const std::vector<Passage>& bigBandPassages()
{
    static const std::vector<Passage> p {
        { "A ensemble hits", 1, 8, false },
        { "B sax soli", 9, 16, true },
        { "C shout chorus", 17, 24, true },
        { "D ballad", 25, 32, false },
        { "E backgrounds", 33, 40, false },
    };
    return p;
}

bool writeBigBandTestMusic(MasterScore* score)
{
    if (!score || score->nstaves() != 18) {
        return false;
    }
    const int have = int(score->nmeasures());
    if (have < 40) {
        score->appendMeasures(40 - have);
    }
    score->startCmd(muse::TranslatableString::untranslatable("Jubelle big-band test music"));
    Writer w(score);

    KeySigEvent f;
    f.setConcertKey(Key::F);
    for (Staff* st : score->staves()) {
        if (!st->isDrumStaff(Fraction(0, 1))) {
            score->undoChangeKeySig(st, Fraction(0, 1), f);
        }
    }

    // A: ensemble hits
    const std::vector<std::vector<Ev> > hits {
        { { 0, 4, ACC }, { 6, 2, MARC } },
        { { 2, 2, ACC }, { 4, 4, ACC }, { 12, 4, MARC } },
        { { 0, 6, ACC }, { 6, 2, MARC }, { 10, 2, ACC }, { 12, 4, MARC } },
        { { 0, 2, STAC }, { 2, 2, ACC }, { 6, 10, ACC } },
    };
    for (int b = 0; b < 8; ++b) {
        const std::vector<Ev> r = (b == 7) ? std::vector<Ev> { { 0, 16, ACC | FALL } } : hits[b % 4];
        section(w, AS1, 5, b, r, { 72, 74, 77 });
        section(w, TP1, 4, b, r, { 79, 81, 82 });
        section(w, TB1, 4, b, r, { 65, 67, 69 }, 34);
    }

    // B: sax soli; brass rest
    for (int b = 8; b < 16; ++b) {
        std::vector<Ev> line;
        std::vector<int> tops;
        const bool run = (b % 2 == 1);
        const int eighths = run ? 4 : 8;
        for (int i = 0; i < eighths; ++i) {
            line.push_back({ i * 2, 2, (i % 4 == 3) ? ACC : NONE });
            tops.push_back(70 + int(std::lround(5 * std::sin((b * 8 + i) * 0.7))));
        }
        if (run) {   // a descending sixteenth run on beats 3-4
            for (int i = 0; i < 8; ++i) {
                line.push_back({ 8 + i, 1, NONE });
                tops.push_back(77 - i);
            }
        }
        section(w, AS1, 5, b, line, tops);
        if (b % 2 == 0) {
            for (staff_idx_t s = AS1; s <= BS; ++s) {
                w.slur(s, b * BAR, (b + 1) * BAR + 15 * SIXTEENTH);
            }
        }
    }

    // C: shout chorus
    const std::vector<std::vector<Ev> > shout {
        { { 0, 2, ACC }, { 2, 1, NONE }, { 3, 1, NONE }, { 4, 2, MARC }, { 6, 4, ACC }, { 12, 2, NONE }, { 14, 2, ACC } },
        { { 0, 3, ACC }, { 3, 1, NONE }, { 4, 4, MARC }, { 10, 2, ACC }, { 12, 4, DOIT } },
        { { 0, 2, NONE }, { 2, 2, ACC }, { 4, 1, NONE }, { 5, 1, NONE }, { 6, 2, MARC }, { 8, 2, STAC }, { 10, 2, ACC }, { 12, 4, MARC } },
        { { 0, 8, ACC }, { 10, 6, ACC | FALL } },
    };
    for (int b = 16; b < 24; ++b) {
        const std::vector<Ev>& r = shout[(b - 16) % 4];
        section(w, AS1, 5, b, r, { 74, 75, 77, 79 });
        section(w, TP1, 4, b, r, { 82, 84, 81, 79 });
        section(w, TB1, 4, b, r, { 67, 69, 70, 72 }, 34);
    }

    // D: ballad, sustained pads; trumpets tacet for four bars
    for (int b = 24; b < 32; ++b) {
        const std::vector<Ev> pad = (b % 2) ? std::vector<Ev> { { 0, 8, TEN }, { 8, 8, TEN } } : std::vector<Ev> { { 0, 16, NONE } };
        section(w, AS1, 5, b, pad, { 72, 70 });
        section(w, TB1, 4, b, pad, { 62, 60 }, 34);
        if (b >= 28) {
            section(w, TP1, 4, b, { { 0, 8, NONE }, { 8, 8, NONE } }, { 74, 72 });
        }
    }

    // E: sax melody with two chords a bar, brass background pads
    for (int b = 32; b < 40; ++b) {
        const std::vector<Ev> mel { { 0, 3, NONE }, { 3, 1, NONE }, { 4, 2, ACC }, { 6, 2, NONE }, { 8, 4, TEN }, { 12, 2, NONE }, { 14, 2, STAC } };
        section(w, AS1, 5, b, mel, { 72, 74, 75, 77, 76, 74, 72 });
        section(w, TP1, 4, b, { { 0, 8, NONE }, { 8, 8, ACC } }, { 77, 79 });
        section(w, TB1, 4, b, { { 0, 8, NONE }, { 8, 8, ACC } }, { 65, 67 }, 34);
    }

    for (int b = 0; b < 40; ++b) {
        rhythmSection(w, b, b >= 24 && b < 32);
        chordSymbols(w, b);
    }

    // Rehearsal marks, tempo, dynamics, hairpins
    const char16_t* marks[] = { u"A", u"B", u"C", u"D", u"E" };
    for (int i = 0; i < 5; ++i) {
        w.rehearsal(i * 8 * BAR, marks[i]);
    }
    w.tempo(0, u"Medium Swing <sym>metNoteQuarterUp</sym> = 144", 144);
    w.tempo(24 * BAR, u"Ballad <sym>metNoteQuarterUp</sym> = 66", 66);
    w.tempo(32 * BAR, u"A tempo <sym>metNoteQuarterUp</sym> = 144", 144);
    const std::vector<std::pair<int, DynamicType> > dyn {
        { 0, DynamicType::F }, { 8, DynamicType::MF }, { 16, DynamicType::FF }, { 24, DynamicType::PP }, { 32, DynamicType::MP } };
    for (const auto& [bar, d] : dyn) {
        for (staff_idx_t s = 0; s < 18; ++s) {
            const bool brassTacet = (bar == 8 && s >= TP1 && s <= BTB) || (bar == 24 && s >= TP1 && s < TB1);
            if (!brassTacet && s != PNO_L && w.chordAt(s, bar * BAR)) {
                w.dynamic(s, bar * BAR, d);
            }
        }
    }
    for (staff_idx_t s = AS1; s <= BS; ++s) {
        w.hairpin(s, 14 * BAR, 16 * BAR, true);
        w.hairpin(s, 28 * BAR, 30 * BAR, true);
        w.hairpin(s, 30 * BAR, 32 * BAR, false);
    }
    for (staff_idx_t s = TP1; s <= BTB; ++s) {
        w.hairpin(s, 22 * BAR, 24 * BAR, true);
    }
    score->endCmd();
    score->doLayout();
    return true;
}
}
