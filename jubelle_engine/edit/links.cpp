/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 */
#include "links.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

#include "operations.h"
#include "structure.h"
#include "tablature.h"
#include "model/fields.h"

namespace F = je::model::fields;
using je::Fraction;
using je::model::IdList;
using je::model::Kind;
using je::model::Object;

namespace je::edit {
namespace {
//! Fields that aren't the music: identities, links, the file's appearance, places in a structure, the tablature
//! fretting (which belongs to each staff)
const std::set<Field>& notMusic()
{
    static const std::set<Field> s { F::KIND, F::TIME, F::CHILDREN, F::PARENT, F::TUPLET, F::TIE_TO, F::ORIGIN_EID, F::DETAILS,
                                     F::DETAILS_2, F::DETAILS_AFTER, F::DETAILS_KEY, F::SEQ, F::ANCHOR, F::HOME_STAFF, F::START_EVENT,
                                     F::END_STAFF, F::END_ANCHOR, F::LINK_EID, F::LINK_MARK, F::TIE_EID, F::TIE_LINK, F::TAG,
                                     F::AT_END, F::LINKED_TO, F::LINKED_STAFF, F::FRET, F::STRING, F::TIE_MARK, F::LINE_DETAILS };
    return s;
}

std::string valueText(const model::Value& v)
{
    std::ostringstream o;
    if (std::holds_alternative<bool>(v)) {
        o << "b" << std::get<bool>(v);
    } else if (std::holds_alternative<int64_t>(v)) {
        o << "i" << std::get<int64_t>(v);
    } else if (std::holds_alternative<double>(v)) {
        o << "d" << std::get<double>(v);
    } else if (std::holds_alternative<model::Text>(v)) {
        o << "t" << std::get<model::Text>(v).str();
    } else if (std::holds_alternative<Fraction>(v)) {
        o << "f" << std::get<Fraction>(v).numerator() << "/" << std::get<Fraction>(v).denominator();
    } else if (std::holds_alternative<je::Id>(v)) {
        o << "id";   // references are described by place, not by id
    }
    return o.str();
}

class Linker
{
public:
    Linker(Transaction& tx, model::Model& m, IdGenerator& ids, LinkSyncReport& r)
        : tx(tx), m(m), ids(ids), ops(m, ids), report(r) {}

    const Object* obj(je::Id id) const { return m.find(id); }

    std::vector<je::Id> voices(je::Id staff) const
    {
        std::vector<je::Id> out;
        if (const Object* s = obj(staff)) {
            for (je::Id v : s->children()) {
                if (obj(v)->kind() == Kind::Voice) {
                    out.push_back(v);
                }
            }
        }
        return out;
    }

    //! The events and tuplets of a voice starting in [t0, t1), in order
    std::vector<je::Id> itemsIn(je::Id voice, const Fraction& t0, const Fraction& t1) const
    {
        std::vector<je::Id> out;
        for (je::Id c : obj(voice)->children()) {
            const Object* o = obj(c);
            const Kind k = o->kind();
            if (k != Kind::Chord && k != Kind::Rest && k != Kind::Tuplet) {
                continue;
            }
            const Fraction t = o->fractionOr(F::TIME, Fraction());
            if (t0 <= t && t < t1) {
                out.push_back(c);
            }
        }
        return out;
    }

    //! A description of an object's music and everything under it, references by place (`shape`: only what
    //! decides the structure: kinds, times, tuplets, and how many children)
    void describe(std::ostringstream& d, je::Id id, const std::map<je::Id, int>& local, bool shape = false) const
    {
        const Object* o = obj(id);
        d << "(" << int(o->kind());
        if (!shape) {
            o->forEachField([&](Field f, const model::Value& v) {
                if (!notMusic().count(f)) {
                    d << " " << f << "=" << valueText(v);
                }
            });
        }
        d << " T" << valueText(o->get(F::TIME));
        if (o->has(F::TUPLET)) {
            auto it = local.find(o->idOr(F::TUPLET));
            d << " tu" << (it == local.end() ? -1 : it->second);
        }
        if (o->has(F::TIE_TO) && !shape) {   // tied to: its time and pitch
            const Object* n = obj(o->idOr(F::TIE_TO));
            const Object* c = n ? obj(n->parent()) : nullptr;
            if (n && c) {
                d << " tie" << valueText(c->get(F::TIME)) << "p" << n->intOr(F::PITCH, 0);
            }
        }
        for (je::Id c : o->children()) {
            describe(d, c, local, shape);
        }
        d << ")";
    }

    je::Id voiceNumbered(je::Id staff, int64_t number) const
    {
        for (je::Id v : voices(staff)) {
            if (obj(v)->intOr(F::NUMBER, 0) == number) {
                return v;
            }
        }
        return je::Id();
    }

    std::string voiceSignature(je::Id voice, const Fraction& t0, const Fraction& t1, bool shape = false) const
    {
        std::ostringstream d;
        if (!voice.isValid()) {
            return std::string();
        }
        const std::vector<je::Id> items = itemsIn(voice, t0, t1);
        std::map<je::Id, int> local;
        for (je::Id i : items) {
            if (obj(i)->kind() == Kind::Tuplet) {
                local[i] = int(local.size());
            }
        }
        for (je::Id i : items) {
            describe(d, i, local, shape);
        }
        return d.str();
    }

    std::string signature(je::Id staff, const Fraction& t0, const Fraction& t1) const
    {
        std::ostringstream d;
        for (je::Id v : voices(staff)) {
            const std::string sv = voiceSignature(v, t0, t1);
            if (!sv.empty()) {
                d << "V" << obj(v)->intOr(F::NUMBER, 0) << ":" << sv;
            }
        }
        return d.str();
    }

    std::set<int64_t> voiceNumbers(je::Id a, je::Id b) const
    {
        std::set<int64_t> out;
        for (je::Id s : { a, b }) {
            for (je::Id v : voices(s)) {
                out.insert(obj(v)->intOr(F::NUMBER, 0));
            }
        }
        return out;
    }

    //! Events by place: (voice number, time, order at that time) → event, and notes by their event's place and
    //! their order in it
    using Place = std::tuple<int64_t, Fraction, int, int>;   // voice, time, event order, note order (-1: the event)
    std::map<Place, je::Id> places(je::Id staff) const
    {
        std::map<Place, je::Id> out;
        for (je::Id v : voices(staff)) {
            const int64_t vn = obj(v)->intOr(F::NUMBER, 0);
            std::map<Fraction, int> order;
            for (je::Id e : obj(v)->children()) {
                const Object* o = obj(e);
                if (o->kind() != Kind::Chord && o->kind() != Kind::Rest) {
                    continue;
                }
                const Fraction t = o->fractionOr(F::TIME, Fraction());
                const int k = order[t]++;
                out[{ vn, t, k, -1 }] = e;
                int ni = 0;
                for (je::Id n : o->children()) {
                    if (obj(n)->kind() == Kind::Note) {
                        out[{ vn, t, k, ni++ }] = n;
                    }
                }
            }
        }
        return out;
    }

    //! Same structure on both sides: each object takes the music of its counterpart, in place (so the objects,
    //! their identities and what the file said about their appearance stay; a changed object's own appearance
    //! details are dropped by their fingerprint when it is written)
    //! Whether a linked staff's element is linked to this element of its main staff: by Jubelle's own link, by
    //! the identity the file gave (4.60 on), or, for links by place (before 4.60), when both are the file's
    //! elements still in their place
    bool linksTo(const Object& copy, const Object& main) const
    {
        if (copy.has(F::LINKED_TO)) {
            return copy.idOr(F::LINKED_TO) == main.id();
        }
        if (copy.has(F::LINK_EID)) {
            return main.text(F::ORIGIN_EID) == copy.text(F::LINK_EID);
        }
        if (copy.has(F::LINK_MARK)) {
            return main.text(F::LINK_MARK) == "main";
        }
        return false;
    }

    //! The copy links to the original (an element replaced on one staff while the other kept its own)
    void relink(je::Id copy, je::Id main)
    {
        const Object* c = obj(copy);
        if (linksTo(*c, *obj(main))) {
            return;
        }
        const bool fileLink = c->has(F::LINK_EID) || c->has(F::LINK_MARK);
        tx.set(copy, F::LINKED_TO, main);
        if (fileLink) {
            tx.set(copy, F::LINK_EID, model::Value());
            tx.set(copy, F::LINK_MARK, model::Value());
        }
    }

    //! The same music: only the links are checked
    void relinkTree(je::Id from, je::Id to, bool toMain)
    {
        if (toMain) {
            relink(from, to);
        } else {
            relink(to, from);
        }
        const std::vector<je::Id> ca = obj(from)->children().ids(), cb = obj(to)->children().ids();
        for (size_t i = 0; i < ca.size() && i < cb.size(); ++i) {
            if (obj(ca[i])->kind() == obj(cb[i])->kind()) {
                relinkTree(ca[i], cb[i], toMain);
            }
        }
    }

    void patch(je::Id from, je::Id to, bool toMain)
    {
        if (toMain) {
            relink(from, to);
        } else {
            relink(to, from);
        }
        const Object* a = obj(from);
        const Object* b = obj(to);
        std::vector<std::pair<Field, model::Value> > set;
        a->forEachField([&](Field f, const model::Value& v) {
            if (!notMusic().count(f) && !(b->get(f) == v)) {
                set.emplace_back(f, v);
            }
        });
        b->forEachField([&](Field f, const model::Value&) {
            if (!notMusic().count(f) && !a->has(f)) {
                set.emplace_back(f, model::Value());
            }
        });
        const std::vector<je::Id> ca = a->children().ids(), cb = b->children().ids();
        for (const auto& [f, v] : set) {
            if (f == F::PITCH) {   // a new pitch on a tablature staff needs its string and fret again
                refret.insert({ staffOf(to), eventTime(to) });
            }
            tx.set(to, f, v);
        }
        if (!set.empty()) {
            ++report.objectsPatched;
        }
        for (size_t i = 0; i < ca.size() && i < cb.size(); ++i) {
            patch(ca[i], cb[i], toMain);
        }
    }

    //! Writes [t0, t1) of one voice of `to` again from `from`; elements made carry their original (LINKED_TO)
    //! unless `toMain` (the main staff written from an edited linked staff, which is then written from it)
    void rebuild(je::Id from, je::Id to, int64_t vn, const Fraction& t0, const Fraction& t1, std::map<je::Id, je::Id>& moved,
                 bool toMain)
    {
        // what was there, by place, so lines anchored to it can move to the new copies
        std::map<Place, je::Id> before;
        for (const auto& [p, id] : places(to)) {
            if (std::get<0>(p) == vn && t0 <= std::get<1>(p) && std::get<1>(p) < t1) {
                before[p] = id;
            }
        }
        if (const je::Id v = voiceNumbered(to, vn); v.isValid()) {
            const std::vector<je::Id> items = itemsIn(v, t0, t1);
            for (size_t i = items.size(); i-- > 0;) {
                removeTree(tx, items[i]);
            }
        }
        const je::Id sv = voiceNumbered(from, vn);
        const std::vector<je::Id> items = sv.isValid() ? itemsIn(sv, t0, t1) : std::vector<je::Id>();
        if (!items.empty()) {
            const je::Id target = ops.voice(tx, to, int(vn));
            std::map<je::Id, je::Id> copies;   // tuplets: source → copy
            // tuplets first, outer ones before those inside them, so events find theirs
            std::vector<je::Id> tuplets;
            for (je::Id i : items) {
                if (obj(i)->kind() == Kind::Tuplet) {
                    tuplets.push_back(i);
                }
            }
            for (size_t pass = 0; pass <= tuplets.size() && copies.size() < tuplets.size(); ++pass) {
                for (je::Id t : tuplets) {
                    const je::Id outer = obj(t)->idOr(F::TUPLET);
                    const bool outerHere = std::find(tuplets.begin(), tuplets.end(), outer) != tuplets.end();
                    if (!copies.count(t) && (!outerHere || copies.count(outer))) {
                        copyTree(t, target, copies, true, !toMain);
                    }
                }
            }
            for (je::Id i : items) {
                if (obj(i)->kind() != Kind::Tuplet) {
                    copyTree(i, target, copies, true, !toMain);
                }
            }
        }
        for (const auto& [p, id] : places(to)) {
            auto it = before.find(p);
            if (it != before.end()) {
                moved[it->second] = id;
            }
        }
        for (const auto& [p, id] : before) {
            moved.emplace(id, je::Id());   // gone: lines anchored to it go too
        }
        ++report.voicesRebuilt;
    }

    je::Id copyTree(je::Id source, je::Id parent, std::map<je::Id, je::Id>& copies, bool top, bool link)
    {
        const Object* o = obj(source);
        NewObject n;
        n.kind = o->kind();
        o->forEachField([&](Field f, const model::Value& v) {
            if (f == F::TIME || f == F::FRET || f == F::STRING || !notMusic().count(f)) {
                n.fields.emplace_back(f, v);
            }
        });
        if (o->has(F::TUPLET)) {
            auto it = copies.find(o->idOr(F::TUPLET));
            if (it != copies.end()) {
                n.fields.emplace_back(F::TUPLET, it->second);
            }
        }
        if (link) {
            n.fields.emplace_back(F::LINKED_TO, source);
        }
        const std::vector<je::Id> children = o->children().ids();   // read before the model changes
        const je::Id made = create(tx, ids, parent, n, top ? Placement::ByTime : Placement::Append);
        copies[source] = made;
        for (je::Id c : children) {
            copyTree(c, made, copies, false, link);
        }
        return made;
    }

    //! Ties on `to` as they are on `from` (a note's tie target by place)
    void reconnectTies(je::Id from, je::Id to)
    {
        const std::map<Place, je::Id> src = places(from);
        const std::map<Place, je::Id> dst = places(to);
        std::map<je::Id, Place> srcPlace;
        for (const auto& [p, id] : src) {
            srcPlace[id] = p;
        }
        for (const auto& [p, id] : dst) {
            if (std::get<3>(p) < 0) {
                continue;
            }
            je::Id want;
            auto s = src.find(p);
            const Object* dn = obj(id);
            if (s != src.end()) {
                const Object* sn = obj(s->second);
                if (sn->has(F::TIE_TO)) {
                    auto tp = srcPlace.find(sn->idOr(F::TIE_TO));
                    if (tp == srcPlace.end() && obj(dn->idOr(F::TIE_TO))) {
                        continue;   // to another staff: as it is
                    }
                    auto d = dst.find(tp->second);
                    want = d == dst.end() ? je::Id() : d->second;
                }
            }
            if (dn->idOr(F::TIE_TO) != want) {
                tx.set(id, F::TIE_TO, want.isValid() ? model::Value(want) : model::Value());
            }
        }
    }

    //! Lines anchored to replaced objects move to their new copies (or go, when there is none)
    void moveLines(const std::map<je::Id, je::Id>& moved)
    {
        if (moved.empty()) {
            return;
        }
        std::vector<je::Id> gone;
        const std::vector<je::Id> all = obj(m.score())->children().ids();
        for (je::Id c : all) {
            const Object* sp = obj(c);
            if (sp->kind() != Kind::Spanner) {
                continue;
            }
            bool lost = false;
            // a line from note to note goes with its notes (the music there was written again)
            const Object* an = obj(sp->idOr(F::ANCHOR));
            if (an && an->kind() == Kind::Note && (moved.count(sp->idOr(F::ANCHOR)) || moved.count(sp->idOr(F::END_ANCHOR)))) {
                gone.push_back(c);
                continue;
            }
            const std::map<Field, je::Id> refs { { F::ANCHOR, sp->idOr(F::ANCHOR) }, { F::END_ANCHOR, sp->idOr(F::END_ANCHOR) },
                                                 { F::START_EVENT, sp->idOr(F::START_EVENT) } };   // read before changing it
            for (const auto& [f, ref] : refs) {
                auto it = moved.find(ref);
                if (it == moved.end()) {
                    continue;
                }
                if (it->second.isValid()) {
                    tx.set(c, f, it->second);
                } else {
                    lost = true;
                }
            }
            if (lost) {
                gone.push_back(c);
            }
        }
        for (je::Id g : gone) {
            removeTree(tx, g);
        }
    }

    //! Each measure and voice whose music differs: patched in place where the structure is the same, written
    //! again where it isn't. With `toMain` (an edit on a linked staff), a voice written again on the main staff is
    //! then written again on the linked staff from it, so the main staff's elements are the originals
    void sync(je::Id from, je::Id to, bool toMain = false)
    {
        std::map<je::Id, je::Id> moved, movedBack;
        std::vector<std::tuple<int64_t, Fraction, Fraction> > back;
        for (je::Id ms : ops.measures()) {
            const Fraction t0 = ops.measureStart(ms);
            const Fraction t1 = ops.measureEnd(ms);
            const bool same = signature(from, t0, t1) == signature(to, t0, t1);
            for (int64_t vn : voiceNumbers(from, to)) {
                const je::Id va = voiceNumbered(from, vn), vb = voiceNumbered(to, vn);
                if (same || voiceSignature(va, t0, t1) == voiceSignature(vb, t0, t1)) {
                    // the same music: each copy must still be linked to its counterpart (an element deleted and
                    // entered again on one staff is a new element)
                    if (va.isValid() && vb.isValid()) {
                        const std::vector<je::Id> ia = itemsIn(va, t0, t1), ib = itemsIn(vb, t0, t1);
                        for (size_t i = 0; i < ia.size() && i < ib.size(); ++i) {
                            if (obj(ia[i])->kind() != Kind::Tuplet) {
                                relinkTree(ia[i], ib[i], toMain);
                            }
                        }
                    }
                    continue;
                }
                const std::string shapeA = voiceSignature(va, t0, t1, true);
                if (!shapeA.empty() && shapeA == voiceSignature(vb, t0, t1, true)) {
                    const std::vector<je::Id> ia = itemsIn(va, t0, t1), ib = itemsIn(vb, t0, t1);
                    for (size_t i = 0; i < ia.size(); ++i) {
                        patch(ia[i], ib[i], toMain);
                    }
                    continue;
                }
                rebuild(from, to, vn, t0, t1, moved, toMain);
                if (toMain) {
                    back.push_back({ vn, t0, t1 });
                }
            }
        }
        reconnectTies(from, to);
        moveLines(moved);
        for (const auto& [vn, t0, t1] : back) {   // the edited staff's copies, now of the new originals
            rebuild(to, from, vn, t0, t1, movedBack, false);
        }
        moveLines(movedBack);   // its ties follow when the main staff is copied to the linked staves
    }

    je::Id staffOf(je::Id id) const
    {
        for (const Object* o = obj(id); o; o = obj(o->parent())) {
            if (o->kind() == Kind::Staff) {
                return o->id();
            }
        }
        return je::Id();
    }

    Fraction eventTime(je::Id id) const
    {
        for (const Object* o = obj(id); o; o = obj(o->parent())) {
            if (o->kind() == Kind::Chord || o->kind() == Kind::Rest) {
                return o->fractionOr(F::TIME, Fraction());
            }
        }
        return Fraction();
    }

    Transaction& tx;
    model::Model& m;
    IdGenerator& ids;
    Operations ops;
    LinkSyncReport& report;
    std::set<std::pair<je::Id, Fraction> > refret;   // staff and time of notes whose pitch was changed here
};

je::Id staffOfObject(const model::Model& m, je::Id id)
{
    for (const Object* o = m.find(id); o; o = m.find(o->parent())) {
        if (o->kind() == Kind::Staff) {
            return o->id();
        }
    }
    return je::Id();
}
}

//! Lines (slurs, hairpins, pedal, …) and staff items (dynamics, staff texts, chord symbols, fretboard diagrams, …)
//! on linked staves: MuseScore adds, changes and removes them on every staff of a linked pair (undoAddElement with
//! its staff list). Unlike the notes, linked staves needn't hold the same ones (a file may have a dynamic on one
//! staff only), so only what this edit touched is mirrored: a counterpart is found by Jubelle's own link, by the
//! file's identities, or by place; it is changed in step, made, or removed.
class ItemMirror
{
public:
    ItemMirror(Transaction& tx, model::Model& m, IdGenerator& ids, LinkSyncReport& r,
               const std::map<je::Id, std::vector<je::Id> >& groups)
        : tx(tx), m(m), ids(ids), report(r)
    {
        for (const auto& [main, linked] : groups) {
            std::vector<je::Id> all { main };
            all.insert(all.end(), linked.begin(), linked.end());
            for (je::Id s : all) {
                groupOf[s] = all;
                mainOf[s] = main;
            }
        }
    }

    const Object* obj(je::Id id) const { return m.find(id); }

    static bool mirrored(const Object& o)
    {
        if (o.kind() == Kind::Spanner) {
            return true;
        }
        static const std::set<std::string> notOnLinked { "keySig", "timeSig", "mmrMeasure", "measureDetail", "frame",
                                                         "instrumentChange", "tempo", "rehearsalMark", "systemText", "jump",
                                                         "marker" };
        return o.kind() == Kind::FlowItem && o.has(F::ANCHOR) && !notOnLinked.count(o.text(F::SUBKIND));
    }

    //! The staff an item or line is on: its anchor, or the staff of the event or note it is anchored to
    je::Id staffOf(je::Id anchor) const
    {
        for (const Object* o = obj(anchor); o; o = obj(o->parent())) {
            if (o->kind() == Kind::Staff) {
                return o->id();
            }
        }
        return je::Id();
    }

    je::Id itemStaff(const Object& o) const
    {
        je::Id st = staffOf(o.idOr(F::ANCHOR));
        if (!st.isValid()) {
            st = staffOf(o.idOr(F::START_EVENT));
        }
        return st;
    }

    std::vector<je::Id> candidates(const Object& like) const
    {
        std::vector<je::Id> out;
        const je::Id parent = like.parent();
        if (const Object* p = obj(parent)) {
            for (je::Id c : p->children()) {
                const Object* o = obj(c);
                if (o->kind() == like.kind() && o->text(F::SUBKIND) == like.text(F::SUBKIND)) {
                    out.push_back(c);
                }
            }
        }
        return out;
    }

    //! The item on staff `on` that is the same as `x` (on another staff of the pair)
    je::Id counterpart(const Object& x, je::Id on) const
    {
        const std::vector<je::Id> cands = candidates(x);
        for (je::Id c : cands) {
            const Object* o = obj(c);
            if (c == x.id() || itemStaff(*o) != on) {
                continue;
            }
            const bool linked = (x.has(F::LINKED_TO) && (o->id() == x.idOr(F::LINKED_TO) || o->idOr(F::LINKED_TO) == x.idOr(F::LINKED_TO)))
                                || o->idOr(F::LINKED_TO) == x.id()
                                || (!x.text(F::ORIGIN_EID).empty() && o->text(F::LINK_EID) == x.text(F::ORIGIN_EID))
                                || (!x.text(F::LINK_EID).empty() && (o->text(F::ORIGIN_EID) == x.text(F::LINK_EID)
                                                                     || o->text(F::LINK_EID) == x.text(F::LINK_EID)));
            if (linked) {
                return c;
            }
        }
        if (x.has(F::LINK_MARK)) {   // links by place: the same kind at the same time
            for (je::Id c : cands) {
                const Object* o = obj(c);
                if (c != x.id() && itemStaff(*o) == on && o->has(F::LINK_MARK)
                    && o->fractionOr(F::TIME, Fraction()) == x.fractionOr(F::TIME, Fraction())) {
                    return c;
                }
            }
        }
        return je::Id();
    }

    //! An anchor on staff `from` translated to staff `to`: the staff itself, or the event or note at the same place
    je::Id mapAnchor(je::Id anchor, je::Id from, je::Id to, Linker& linker) const
    {
        if (!anchor.isValid() || anchor == from) {
            return anchor == from ? to : anchor;
        }
        if (staffOf(anchor) != from) {
            return anchor;   // elsewhere (a line ending on another instrument)
        }
        const auto src = linker.places(from);
        const auto dst = linker.places(to);
        for (const auto& [p, id] : src) {
            if (id == anchor) {
                auto d = dst.find(p);
                return d == dst.end() ? je::Id() : d->second;
            }
        }
        return je::Id();
    }

    std::vector<std::pair<Field, model::Value> > itemFields(const Object& o, je::Id from, je::Id to, Linker& linker, bool& lost) const
    {
        std::vector<std::pair<Field, model::Value> > f;
        o.forEachField([&](Field k, const model::Value& v) {
            if (k == F::ANCHOR || k == F::END_ANCHOR || k == F::START_EVENT) {
                const je::Id mapped = mapAnchor(std::get<je::Id>(v), from, to, linker);
                lost = lost || !mapped.isValid();
                f.emplace_back(k, mapped);
            } else if (k == F::END_STAFF) {
                f.emplace_back(k, std::get<je::Id>(v) == from ? model::Value(to) : v);
            } else if (k == F::HOME_STAFF) {
                // where a system-wide item is kept: not mirrored
            } else if (k == F::TIME || k == F::DURATION || k == F::VOICE_INDEX || k == F::SEQ || k == F::AT_END
                       || !notMusicItem().count(k)) {
                f.emplace_back(k, v);
            }
        });
        return f;
    }

    static const std::set<Field>& notMusicItem()
    {
        static const std::set<Field> s { F::KIND, F::CHILDREN, F::PARENT, F::ORIGIN_EID, F::DETAILS, F::DETAILS_2, F::DETAILS_AFTER,
                                         F::DETAILS_KEY, F::LINK_EID, F::LINK_MARK, F::TIE_EID, F::TIE_LINK, F::TAG, F::LINKED_TO,
                                         F::LINKED_STAFF, F::ANCHOR, F::END_ANCHOR, F::START_EVENT, F::END_STAFF, F::HOME_STAFF };
        return s;
    }

    void mirror(const ChangeSet& changes, Linker& linker)
    {
        // what the edit made, changed or removed (and, for a removed item, what it was)
        std::vector<je::Id> order;
        std::map<je::Id, std::map<Field, model::Value> > before;
        for (const ChangeRecord& r : changes.records) {
            if (!before.count(r.object)) {
                order.push_back(r.object);
            }
            before[r.object].emplace(r.field, r.before);
        }
        for (je::Id id : order) {
            const Object* now = obj(id);
            if (now) {
                if (mirrored(*now)) {
                    changed(*now, linker);
                }
                continue;
            }
            // removed: its counterparts go too
            const auto& b = before[id];
            auto kindIt = b.find(F::KIND);
            if (kindIt == b.end() || !std::holds_alternative<int64_t>(kindIt->second)) {
                continue;
            }
            const Kind kind = Kind(std::get<int64_t>(kindIt->second));
            if (kind != Kind::FlowItem && kind != Kind::Spanner) {
                continue;
            }
            removed(id, b);
        }
    }

    void changed(const Object& x, Linker& linker)
    {
        const je::Id st = itemStaff(x);
        auto g = groupOf.find(st);
        if (g == groupOf.end() || done.count(x.id())) {
            return;
        }
        done.insert(x.id());
        const je::Id main = mainOf.at(st);
        const je::Id xid = x.id();
        // the original is the main staff's: made first when the edit was on a linked staff
        je::Id original = st == main ? xid : counterpart(x, main);
        if (!original.isValid()) {
            const bool fileLink = x.has(F::LINK_EID) || x.has(F::LINK_MARK);
            original = make(*obj(xid), st, main, linker, false);
            if (!original.isValid()) {
                return;
            }
            tx.set(xid, F::LINKED_TO, original);
            if (fileLink) {   // a link the file gave it, to an item that isn't there any more
                tx.set(xid, F::LINK_EID, model::Value());
                tx.set(xid, F::LINK_MARK, model::Value());
            }
        } else if (original != xid) {
            patchItem(xid, original, st, main, linker);
        }
        for (je::Id s : g->second) {
            if (s == st || s == main) {
                continue;
            }
            const je::Id cp = counterpart(*obj(xid), s);
            if (cp.isValid()) {
                patchItem(xid, cp, st, s, linker);
            } else {
                make(*obj(xid), st, s, linker, true, original);
            }
        }
    }

    je::Id make(const Object& x, je::Id from, je::Id to, Linker& linker, bool link, je::Id original = je::Id())
    {
        bool lost = false;
        NewObject n { x.kind(), itemFields(x, from, to, linker, lost) };
        if (lost) {
            return je::Id();   // its note or chord isn't on the other staff
        }
        if (link) {
            n.fields.emplace_back(F::LINKED_TO, original.isValid() ? original : x.id());
        }
        const std::vector<je::Id> kids = x.children().ids();
        const je::Id made = create(tx, ids, x.parent(), n, x.kind() == Kind::FlowItem ? Placement::ByTime : Placement::Append);
        done.insert(made);
        ++report.itemsMirrored;
        (void)kids;
        return made;
    }

    void patchItem(je::Id from, je::Id to, je::Id fromStaff, je::Id toStaff, Linker& linker)
    {
        done.insert(to);
        bool lost = false;
        const std::vector<std::pair<Field, model::Value> > want = itemFields(*obj(from), fromStaff, toStaff, linker, lost);
        if (lost) {
            return;
        }
        const Object* t = obj(to);
        std::vector<std::pair<Field, model::Value> > set;
        for (const auto& [f, v] : want) {
            if (!(t->get(f) == v)) {
                set.emplace_back(f, v);
            }
        }
        t->forEachField([&](Field f, const model::Value&) {
            if (notMusicItem().count(f) || f == F::TIME || f == F::DURATION) {
                return;
            }
            if (std::none_of(want.begin(), want.end(), [f](const auto& w) { return w.first == f; })) {
                set.emplace_back(f, model::Value());
            }
        });
        for (const auto& [f, v] : set) {
            tx.set(to, f, v);
        }
        if (!set.empty()) {
            ++report.itemsMirrored;
        }
    }

    void removed(je::Id id, const std::map<Field, model::Value>& b)
    {
        auto get = [&b](Field f) { auto it = b.find(f); return it == b.end() ? model::Value() : it->second; };
        const model::Value linkedTo = get(F::LINKED_TO), origin = get(F::ORIGIN_EID), linkEid = get(F::LINK_EID);
        const je::Id original = std::holds_alternative<je::Id>(linkedTo) ? std::get<je::Id>(linkedTo) : je::Id();
        const std::string originEid = std::holds_alternative<model::Text>(origin) ? std::get<model::Text>(origin).str() : std::string();
        const std::string toEid = std::holds_alternative<model::Text>(linkEid) ? std::get<model::Text>(linkEid).str() : std::string();
        std::vector<je::Id> gone;
        m.snapshot().forEachObject([&](const Object& o) {
            if ((o.kind() != Kind::FlowItem && o.kind() != Kind::Spanner) || done.count(o.id())) {
                return;
            }
            if (!groupOf.count(itemStaff(o))) {
                return;
            }
            const bool same = o.idOr(F::LINKED_TO) == id || (original.isValid() && (o.id() == original || o.idOr(F::LINKED_TO) == original))
                              || (!originEid.empty() && o.text(F::LINK_EID) == originEid)
                              || (!toEid.empty() && (o.text(F::ORIGIN_EID) == toEid || o.text(F::LINK_EID) == toEid));
            if (same) {
                gone.push_back(o.id());
            }
        });
        for (je::Id g : gone) {
            done.insert(g);
            removeTree(tx, g);
            ++report.itemsMirrored;
        }
    }

    Transaction& tx;
    model::Model& m;
    IdGenerator& ids;
    LinkSyncReport& report;
    std::map<je::Id, std::vector<je::Id> > groupOf;
    std::map<je::Id, je::Id> mainOf;
    std::set<je::Id> done;
};

LinkSyncReport syncLinkedStaves(Transaction& tx, model::Model& m, IdGenerator& ids, const ChangeSet* changes)
{
    LinkSyncReport report;
    const Object* score = m.find(m.score());
    if (!score) {
        return report;
    }
    // groups: a main staff and the staves linked to it (a link to a linked staff counts as a link to its main)
    std::map<je::Id, std::vector<je::Id> > groups;
    std::vector<je::Id> allStaves;
    for (je::Id p : score->children()) {
        const Object* player = m.find(p);
        if (player->kind() != Kind::Player) {
            continue;
        }
        for (je::Id s : player->children()) {
            if (m.find(s)->kind() == Kind::Staff) {
                allStaves.push_back(s);
            }
        }
    }
    for (je::Id s : allStaves) {
        je::Id main = s;
        std::set<je::Id> seen;
        while (m.find(main)->has(F::LINKED_STAFF) && seen.insert(main).second) {
            const je::Id next = m.find(main)->idOr(F::LINKED_STAFF);
            if (!m.find(next)) {
                break;
            }
            main = next;
        }
        if (main != s) {
            groups[main].push_back(s);
        }
    }
    std::set<je::Id> touched;
    Linker linker(tx, m, ids, report);
    if (changes) {
        for (je::Id o : changes->objects()) {
            const je::Id st = staffOfObject(m, o);
            if (st.isValid()) {
                touched.insert(st);
            }
        }
        for (const ChangeRecord& r : changes->records) {   // pitches changed by the edit itself
            const Object* o = m.find(r.object);
            if (r.field == F::PITCH && o && o->kind() == Kind::Note) {
                linker.refret.insert({ staffOfObject(m, r.object), linker.eventTime(r.object) });
            }
        }
    }
    std::set<je::Id> fret;
    for (const auto& [main, linked] : groups) {
        if (changes && !touched.count(main)
            && std::none_of(linked.begin(), linked.end(), [&](je::Id s) { return touched.count(s) > 0; })) {
            continue;
        }
        // an edit made on a linked staff (and not on its main staff) is made on the main staff first
        if (changes && !touched.count(main)) {
            for (je::Id s : linked) {
                if (touched.count(s)) {
                    linker.sync(s, main, true);
                    fret.insert(main);
                    break;
                }
            }
        }
        for (je::Id s : linked) {
            linker.sync(main, s);
            fret.insert(s);
        }
    }
    // lines and staff items the edit touched, on every staff of their linked pair
    if (changes && !groups.empty()) {
        ItemMirror(tx, m, ids, report, groups).mirror(*changes, linker);
    }
    // tablature staves that were edited, or written again here, get strings and frets
    for (je::Id s : allStaves) {
        if ((!changes || touched.count(s) || fret.count(s)) && isTablature(*m.find(s))) {
            const std::vector<je::Id> conflicts = fretStaff(tx, m, s);
            report.conflicts += conflicts.size();
            for (const auto& [staff, t] : linker.refret) {
                if (staff == s) {
                    report.conflicts += fretNotes(tx, m, s, t).size();
                }
            }
        }
    }
    return report;
}

std::vector<std::string> linkedStaffDifferences(model::Model& m)
{
    std::vector<std::string> out;
    IdGenerator ids(1);
    Transaction tx(m, "check");
    LinkSyncReport r;
    Linker linker(tx, m, ids, r);
    m.snapshot().forEachObject([&](const Object& o) {
        if (o.kind() != Kind::Staff || !o.has(F::LINKED_STAFF) || !m.find(o.idOr(F::LINKED_STAFF))) {
            return;
        }
        int index = 0;
        for (je::Id ms : linker.ops.measures()) {
            const Fraction t0 = linker.ops.measureStart(ms), t1 = linker.ops.measureEnd(ms);
            const std::string a = linker.signature(o.idOr(F::LINKED_STAFF), t0, t1), b = linker.signature(o.id(), t0, t1);
            if (a != b) {
                out.push_back("measure " + std::to_string(index + 1) + "\n  main:   " + a + "\n  linked: " + b);
            }
            ++index;
        }
    });
    tx.rollback();
    return out;
}

Transaction::Hook linkedStavesHook(model::Model& m, IdGenerator& ids)
{
    return [&m, &ids](Transaction& tx, const ChangeSet& soFar) {
        syncLinkedStaves(tx, m, ids, &soFar);
    };
}
}
