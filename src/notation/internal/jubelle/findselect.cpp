/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G5: Advanced Selection and Filtering. See findselect.h.

#include "findselect.h"

#include <algorithm>

#include "engraving/dom/articulation.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/chordrest.h"
#include "engraving/dom/engravingitem.h"
#include "engraving/dom/lyrics.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/select.h"
#include "engraving/dom/spanner.h"
#include "engraving/dom/staff.h"
#include "engraving/types/symnames.h"

using namespace mu::engraving;
using muse::String;

namespace mu::notation::jubelle {
namespace {
struct Area {
    Fraction startTick = Fraction(0, 1);
    Fraction endTick = Fraction::max();
    std::set<size_t> staves;   // empty: all
};

Area searchArea(const Score* score, const FindQuery& q)
{
    Area a;
    a.staves = q.staves;
    if (q.inSelection && score->selection().isRange()) {
        const Selection& sel = score->selection();
        a.startTick = sel.tickStart();
        a.endTick = sel.tickEnd();
        std::set<size_t> selStaves;
        for (size_t s = sel.staffStart(); s < sel.staffEnd(); ++s) {
            if (q.staves.empty() || q.staves.count(s)) {
                selStaves.insert(s);
            }
        }
        a.staves = selStaves.empty() ? std::set<size_t> { size_t(-1) } : selStaves;   // nothing if they don't meet
    }
    if (q.fromBar || q.toBar) {
        int index = 0;
        for (const Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
            ++index;
            if (q.fromBar && index == *q.fromBar) {
                a.startTick = std::max(a.startTick, m->tick());
            }
            if (q.toBar && index == *q.toBar) {
                a.endTick = std::min(a.endTick, m->endTick());
            }
        }
    }
    return a;
}

bool inArea(const Area& a, const Fraction& tick, size_t staffIdx)
{
    return tick >= a.startTick && tick < a.endTick && (a.staves.empty() || a.staves.count(staffIdx));
}

bool voiceOk(const FindQuery& q, const EngravingItem* item)
{
    const unsigned v = unsigned(item->voice());
    return v < 4 ? (q.voices & (1u << v)) != 0 : true;
}

bool lengthOk(const FindQuery& q, const ChordRest* cr)
{
    const Fraction len = cr->durationTypeTicks();
    if (q.shortest && len < *q.shortest) {
        return false;
    }
    if (q.longest && len > *q.longest) {
        return false;
    }
    return true;
}

bool noteOk(const FindQuery& q, const Note* n)
{
    const Chord* c = n->chord();
    if (!q.includeGraceNotes && c->isGrace()) {
        return false;
    }
    if (q.skipTiedContinuations && n->tieBack()) {
        return false;
    }
    const int pitch = q.writtenPitch ? n->pitch() - n->transposition() : n->pitch();
    if (q.lowestPitch && pitch < *q.lowestPitch) {
        return false;
    }
    if (q.highestPitch && pitch > *q.highestPitch) {
        return false;
    }
    if (!lengthOk(q, c)) {
        return false;
    }
    if (!q.withArticulation.empty()) {
        bool has = false;
        for (const Articulation* a : c->articulations()) {
            has = has || q.withArticulation.count(a->symId()) > 0;
        }
        if (!has) {
            return false;
        }
    }
    return true;
}

bool wants(const FindQuery& q, FindKind k)
{
    return q.kinds.count(k) > 0;
}

void addChordItems(const FindQuery& q, const Chord* chord, std::vector<EngravingItem*>& out)
{
    if (wants(q, FindKind::Notes)) {
        for (Note* n : chord->notes()) {
            if (noteOk(q, n)) {
                out.push_back(n);
            }
        }
    }
    if (wants(q, FindKind::Articulations)) {
        for (Articulation* a : chord->articulations()) {
            if (q.articulationSymbols.empty() || q.articulationSymbols.count(a->symId())) {
                out.push_back(a);
            }
        }
    }
}

std::optional<FindKind> kindOfAnnotation(const EngravingItem* e)
{
    switch (e->type()) {
    case ElementType::DYNAMIC: return FindKind::Dynamics;
    case ElementType::STAFF_TEXT:
    case ElementType::EXPRESSION: return FindKind::StaffText;
    case ElementType::HARMONY: return FindKind::ChordSymbols;
    default: return std::nullopt;
    }
}

struct KindName {
    FindKind kind;
    const char16_t* name;
};

constexpr KindName KIND_NAMES[] = {
    { FindKind::Notes, u"notes" },
    { FindKind::Rests, u"rests" },
    { FindKind::Articulations, u"articulations" },
    { FindKind::Dynamics, u"dynamics" },
    { FindKind::Hairpins, u"hairpins" },
    { FindKind::Slurs, u"slurs" },
    { FindKind::StaffText, u"stafftext" },
    { FindKind::ChordSymbols, u"chordsymbols" },
    { FindKind::Lyrics, u"lyrics" },
};

String fractionText(const Fraction& f)
{
    return String::number(f.numerator()) + u"/" + String::number(f.denominator());
}

std::optional<Fraction> fractionFrom(const String& s)
{
    const auto parts = s.split(u'/');
    if (parts.size() != 2) {
        return std::nullopt;
    }
    bool ok1 = false;
    bool ok2 = false;
    const int n = parts[0].toInt(&ok1);
    const int d = parts[1].toInt(&ok2);
    if (!ok1 || !ok2 || n <= 0 || d <= 0) {
        return std::nullopt;
    }
    return Fraction(n, d);
}
}

std::vector<EngravingItem*> findItems(const Score* score, const FindQuery& q)
{
    std::vector<EngravingItem*> out;
    if (!score || q.kinds.empty()) {
        return out;
    }
    const Area area = searchArea(score, q);

    for (const Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        if (m->endTick() <= area.startTick || m->tick() >= area.endTick) {
            continue;
        }
        for (const Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
            const Fraction tick = seg->tick();
            if (tick < area.startTick || tick >= area.endTick) {
                continue;
            }
            for (EngravingItem* e : seg->elist()) {
                if (!e || !e->isChordRest() || !inArea(area, tick, e->staffIdx()) || !voiceOk(q, e)) {
                    continue;
                }
                ChordRest* cr = toChordRest(e);
                if (cr->isChord()) {
                    const Chord* chord = toChord(cr);
                    for (const Chord* grace : chord->graceNotesBefore()) {
                        addChordItems(q, grace, out);
                    }
                    addChordItems(q, chord, out);
                    for (const Chord* grace : chord->graceNotesAfter()) {
                        addChordItems(q, grace, out);
                    }
                } else if (cr->isRest() && wants(q, FindKind::Rests) && lengthOk(q, cr)) {
                    out.push_back(cr);
                }
                if (wants(q, FindKind::Lyrics)) {
                    for (Lyrics* l : cr->lyrics()) {
                        out.push_back(l);
                    }
                }
            }
            for (EngravingItem* a : seg->annotations()) {
                const std::optional<FindKind> k = a ? kindOfAnnotation(a) : std::nullopt;
                if (k && wants(q, *k) && inArea(area, tick, a->staffIdx()) && voiceOk(q, a)) {
                    out.push_back(a);
                }
            }
        }
    }

    if (wants(q, FindKind::Hairpins) || wants(q, FindKind::Slurs)) {
        for (const auto& [tick, sp] : score->spanner()) {
            if (!sp) {
                continue;
            }
            const bool isWanted = (sp->isHairpin() && wants(q, FindKind::Hairpins)) || (sp->isSlur() && wants(q, FindKind::Slurs));
            if (isWanted && inArea(area, sp->tick(), sp->staffIdx()) && voiceOk(q, sp)) {
                out.push_back(sp);
            }
        }
    }
    return out;
}

size_t selectItems(Score* score, const std::vector<EngravingItem*>& items)
{
    if (!score) {
        return 0;
    }
    score->deselectAll();
    if (items.empty()) {
        return 0;
    }
    score->select(items, SelectType::ADD);
    return items.size();
}

String toText(const FindQuery& q)
{
    String out;
    auto line = [&out](const String& s) {
        out += s + u"\n";
    };
    String kinds;
    for (const KindName& k : KIND_NAMES) {
        if (q.kinds.count(k.kind)) {
            kinds += (kinds.isEmpty() ? u"" : u",") + String(k.name);
        }
    }
    line(u"kinds=" + kinds);
    if (!q.staves.empty()) {
        String s;
        for (size_t st : q.staves) {
            s += (s.isEmpty() ? u"" : u",") + String::number(int(st));
        }
        line(u"staves=" + s);
    }
    line(u"voices=" + String::number(int(q.voices)));
    if (q.lowestPitch) {
        line(u"lowest=" + String::number(*q.lowestPitch));
    }
    if (q.highestPitch) {
        line(u"highest=" + String::number(*q.highestPitch));
    }
    line(String(u"written=") + (q.writtenPitch ? u"1" : u"0"));
    line(String(u"grace=") + (q.includeGraceNotes ? u"1" : u"0"));
    line(String(u"skiptied=") + (q.skipTiedContinuations ? u"1" : u"0"));
    if (q.shortest) {
        line(u"shortest=" + fractionText(*q.shortest));
    }
    if (q.longest) {
        line(u"longest=" + fractionText(*q.longest));
    }
    auto syms = [](const std::set<SymId>& set) {
        String s;
        for (SymId id : set) {
            s += (s.isEmpty() ? u"" : u",") + String::fromAscii(SymNames::nameForSymId(id).ascii());
        }
        return s;
    };
    if (!q.withArticulation.empty()) {
        line(u"witharticulation=" + syms(q.withArticulation));
    }
    if (!q.articulationSymbols.empty()) {
        line(u"articulations=" + syms(q.articulationSymbols));
    }
    if (q.fromBar) {
        line(u"from=" + String::number(*q.fromBar));
    }
    if (q.toBar) {
        line(u"to=" + String::number(*q.toBar));
    }
    line(String(u"inselection=") + (q.inSelection ? u"1" : u"0"));
    return out;
}

FindQuery fromText(const String& text)
{
    FindQuery q;
    for (String l : text.split(u'\n')) {
        l = l.trimmed();
        const size_t eq = l.indexOf(u'=');
        if (eq == muse::nidx) {
            continue;
        }
        const String key = l.left(eq);
        const String value = l.mid(eq + 1);
        bool ok = false;
        if (key == u"kinds") {
            q.kinds.clear();
            for (const String& k : value.split(u',')) {
                for (const KindName& kn : KIND_NAMES) {
                    if (k == kn.name) {
                        q.kinds.insert(kn.kind);
                    }
                }
            }
        } else if (key == u"staves") {
            for (const String& s : value.split(u',')) {
                const int st = s.toInt(&ok);
                if (ok && st >= 0) {
                    q.staves.insert(size_t(st));
                }
            }
        } else if (key == u"voices") {
            const int v = value.toInt(&ok);
            if (ok) {
                q.voices = unsigned(v) & 0xF;
            }
        } else if (key == u"lowest" || key == u"highest" || key == u"from" || key == u"to") {
            const int v = value.toInt(&ok);
            if (ok) {
                (key == u"lowest" ? q.lowestPitch : key == u"highest" ? q.highestPitch : key == u"from" ? q.fromBar : q.toBar) = v;
            }
        } else if (key == u"written") {
            q.writtenPitch = value == u"1";
        } else if (key == u"grace") {
            q.includeGraceNotes = value == u"1";
        } else if (key == u"skiptied") {
            q.skipTiedContinuations = value == u"1";
        } else if (key == u"inselection") {
            q.inSelection = value == u"1";
        } else if (key == u"shortest") {
            q.shortest = fractionFrom(value);
        } else if (key == u"longest") {
            q.longest = fractionFrom(value);
        } else if (key == u"witharticulation" || key == u"articulations") {
            std::set<SymId>& set = key == u"witharticulation" ? q.withArticulation : q.articulationSymbols;
            for (const String& s : value.split(u',')) {
                const SymId id = SymNames::symIdByName(s);
                if (id != SymId::noSym) {
                    set.insert(id);
                }
            }
        }
    }
    return q;
}
}
