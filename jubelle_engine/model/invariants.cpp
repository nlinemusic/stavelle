/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 */
#include "invariants.h"

#include <algorithm>
#include <unordered_set>

namespace F = je::model::fields;

namespace je::model {
bool allowedChild(Kind parent, Kind child)
{
    switch (parent) {
    case Kind::Score: return child == Kind::Flow || child == Kind::Player || child == Kind::Spanner || child == Kind::Layout;
    case Kind::Flow: return child == Kind::Measure || child == Kind::FlowItem;
    case Kind::Player: return child == Kind::Staff;
    case Kind::Staff: return child == Kind::Voice;
    case Kind::Voice: return child == Kind::Chord || child == Kind::Rest || child == Kind::Tuplet;
    case Kind::Chord: return child == Kind::Note || child == Kind::Attachment;
    case Kind::Rest: return child == Kind::Attachment;
    case Kind::Note: return child == Kind::Attachment;
    default: return false;
    }
}

namespace {
//! The product of the ratios of the tuplets an event sits in (actual / written), or nullopt if the chain is broken
bool tupletFactor(const Snapshot& s, Id tuplet, Fraction& factor, int depth = 0)
{
    factor = Fraction(1, 1);
    while (tuplet.isValid()) {
        const Object* t = s.find(tuplet);
        if (!t || t->kind() != Kind::Tuplet || depth++ > 16) {
            return false;
        }
        const int64_t num = t->intOr(F::RATIO_NUM, 0);
        const int64_t den = t->intOr(F::RATIO_DEN, 0);
        if (num <= 0 || den <= 0) {
            return false;
        }
        factor = factor * Fraction(den, num);
        tuplet = t->idOr(F::TUPLET);
    }
    return true;
}

Fraction dotted(const Fraction& base, int64_t dots)
{
    Fraction total = base;
    Fraction add = base;
    for (int64_t i = 0; i < dots && i < 4; ++i) {
        add = add * Fraction(1, 2);
        total += add;
    }
    return total;
}

bool isAfterGrace(const Object& o)
{
    const std::string t = o.text(F::GRACE_TYPE);
    return t.size() > 5 && t.compare(t.size() - 5, 5, "after") == 0;
}
}

std::vector<std::string> checkInvariants(const Snapshot& s, size_t limit)
{
    std::vector<std::string> problems;
    auto report = [&problems, limit](const Object& o, const std::string& what) {
        if (problems.size() < limit) {
            problems.push_back(std::string(kindName(o.kind())) + " #" + o.id().toString() + ": " + what);
        }
    };

    const Object* score = s.find(s.score());
    if (!score || score->kind() != Kind::Score) {
        problems.push_back("the score object is missing");
        return problems;
    }

    s.forEachObject([&](const Object& o) {
        const Kind kind = o.kind();
        if (kind == Kind::None || int64_t(kind) > int64_t(Kind::Layout)) {
            report(o, "has no valid kind");
            return;
        }

        // parent link, and the parent lists this object exactly once
        if (kind == Kind::Score) {
            if (o.parent().isValid()) {
                report(o, "the score has a parent");
            }
            if (o.id() != s.score()) {
                report(o, "a second score object");
            }
        } else {
            const Object* parent = s.find(o.parent());
            if (!parent) {
                report(o, "its parent is missing");
            } else {
                if (!allowedChild(parent->kind(), kind)) {
                    report(o, std::string("can't be inside a ") + std::string(kindName(parent->kind())));
                }
                const auto& kids = parent->children().ids();
                if (std::count(kids.begin(), kids.end(), o.id()) != 1) {
                    report(o, "isn't listed exactly once by its parent");
                }
            }
        }

        // children exist, point back, and aren't repeated
        std::unordered_set<Id> seen;
        for (Id c : o.children()) {
            const Object* child = s.find(c);
            if (!child) {
                report(o, "lists a missing child #" + c.toString());
            } else if (child->parent() != o.id()) {
                report(o, "lists a child whose parent is another object");
            }
            if (!seen.insert(c).second) {
                report(o, "lists a child twice");
            }
        }

        // time order and no overlaps in a voice (grace notes and tuplet groups aside); measures contiguous
        if (kind == Kind::Voice || kind == Kind::Flow) {
            Fraction lastStart;
            Fraction lastEnd;
            bool first = true;
            for (Id c : o.children()) {
                const Object* e = s.find(c);
                if (!e) {
                    continue;
                }
                const Kind ek = e->kind();
                const bool timed = (kind == Kind::Voice && (ek == Kind::Chord || ek == Kind::Rest) && !e->has(F::GRACE))
                                   || (kind == Kind::Flow && ek == Kind::Measure);
                if (!timed) {
                    continue;
                }
                const Fraction t = e->fractionOr(F::TIME, Fraction());
                const Fraction d = e->fractionOr(F::DURATION, Fraction());
                if (!(Fraction() < d)) {
                    report(*e, "has no positive duration");
                }
                if (!first) {
                    if (t < lastStart) {
                        report(o, "children out of time order");
                    }
                    if (kind == Kind::Voice && t < lastEnd) {
                        report(*e, "overlaps the event before it in its voice");
                    }
                    if (kind == Kind::Flow && !(t == lastEnd)) {
                        report(*e, "doesn't start where the previous measure ends");
                    }
                }
                first = false;
                lastStart = t;
                lastEnd = t + d;
            }
        }

        // written note values, dots and tuplets add up to the actual duration
        if ((kind == Kind::Chord || kind == Kind::Rest) && o.has(F::BASE_DURATION) && !o.has(F::MEASURE_REST)) {
            Fraction factor;
            if (!tupletFactor(s, o.idOr(F::TUPLET), factor)) {
                report(o, "belongs to a missing or invalid tuplet");
            } else if (!o.has(F::GRACE)) {
                const Fraction expected = dotted(o.fractionOr(F::BASE_DURATION, Fraction()), o.intOr(F::DOTS, 0)) * factor;
                if (!(expected == o.fractionOr(F::DURATION, Fraction()))) {
                    report(o, "its written value, dots and tuplets don't add up to its duration");
                }
            }
            if (o.has(F::TUPLET)) {
                const Object* t = s.find(o.idOr(F::TUPLET));
                if (t) {
                    const Fraction ts = t->fractionOr(F::TIME, Fraction());
                    const Fraction te = ts + t->fractionOr(F::DURATION, Fraction());
                    const Fraction es = o.fractionOr(F::TIME, Fraction());
                    if (es < ts || te < es + (o.has(F::GRACE) ? Fraction() : o.fractionOr(F::DURATION, Fraction()))) {
                        report(o, "lies outside its tuplet");
                    }
                    if (t->parent() != o.parent()) {
                        report(o, "its tuplet is in another voice");
                    }
                }
            }
        }
        if (kind == Kind::Tuplet) {
            Fraction outer;
            if (o.intOr(F::RATIO_NUM, 0) <= 0 || o.intOr(F::RATIO_DEN, 0) <= 0) {
                report(o, "has no valid ratio");
            } else if (!tupletFactor(s, o.idOr(F::TUPLET), outer)) {
                report(o, "is inside a missing or invalid tuplet");
            } else if (o.has(F::BASE_DURATION)) {
                const Fraction expected = o.fractionOr(F::BASE_DURATION, Fraction()) * Fraction(o.intOr(F::RATIO_DEN, 0), 1) * outer;
                if (!(expected == o.fractionOr(F::DURATION, Fraction()))) {
                    report(o, "its span doesn't match its ratio and note value");
                }
            }
        }

        // grace chords sit at their main chord's time: before it, or after it for "after" graces
        if (kind == Kind::Chord && o.has(F::GRACE)) {
            const Object* voice = s.find(o.parent());
            if (voice) {
                const auto& kids = voice->children().ids();
                auto it = std::find(kids.begin(), kids.end(), o.id());
                const bool after = isAfterGrace(o);
                const Object* main = nullptr;
                if (after) {
                    for (auto j = it; j != kids.begin();) {
                        const Object* e = s.find(*--j);
                        if (e && !e->has(F::GRACE)) {
                            main = e;
                            break;
                        }
                    }
                } else {
                    for (auto j = it + 1; j != kids.end(); ++j) {
                        const Object* e = s.find(*j);
                        if (e && !e->has(F::GRACE)) {
                            main = e;
                            break;
                        }
                    }
                }
                if (!main || main->kind() != Kind::Chord || !(main->fractionOr(F::TIME, Fraction()) == o.fractionOr(F::TIME, Fraction()))) {
                    report(o, after ? "a grace-after chord with no main chord before it" : "a grace chord with no main chord after it");
                }
            }
        }

        if (kind == Kind::Attachment && o.text(F::SUBKIND) == "lyric") {
            const std::string syl = o.text(F::SYLLABIC);
            if (o.intOr(F::VERSE, 0) < 0 || !(syl.empty() || syl == "single" || syl == "begin" || syl == "middle" || syl == "end")) {
                report(o, "a lyric with an invalid verse or syllable");
            }
        }
        if ((kind == Kind::Attachment || kind == Kind::FlowItem || kind == Kind::Spanner) && o.text(F::SUBKIND).empty()) {
            report(o, "has no subkind");
        }
        if (kind == Kind::FlowItem && o.has(F::ANCHOR)) {
            const Object* staff = s.find(o.idOr(F::ANCHOR));
            if (!staff || staff->kind() != Kind::Staff) {
                report(o, "is anchored to something that isn't a staff");
            }
        }
        if (kind == Kind::Spanner) {
            if (!s.find(o.idOr(F::ANCHOR))) {
                report(o, "starts at a missing object");
            }
            if (o.has(F::END_ANCHOR) && !s.find(o.idOr(F::END_ANCHOR))) {
                report(o, "ends at a missing object");
            }
            if (o.fractionOr(F::DURATION, Fraction()) < Fraction()) {
                report(o, "has a negative length");
            }
        }
        if (kind == Kind::Measure && o.has(F::REPEAT_END) && o.intOr(F::REPEAT_END, 0) < 2) {
            report(o, "an end repeat played fewer than 2 times");
        }
        if (kind == Kind::Layout) {
            const Value players = o.get(F::PLAYERS);
            const IdList list = std::holds_alternative<IdList>(players) ? std::get<IdList>(players) : IdList();
            for (Id p : list) {
                const Object* player = s.find(p);
                if (!player || player->kind() != Kind::Player) {
                    report(o, "lists something that isn't a player");
                }
            }
        }

        if (kind == Kind::Voice) {
            const Object* flow = s.find(o.idOr(F::FLOW));
            if (!flow || flow->kind() != Kind::Flow) {
                report(o, "doesn't belong to a flow");
            }
        }
        if (kind == Kind::Chord) {
            bool hasNote = false;
            for (Id c : o.children()) {
                const Object* n = s.find(c);
                hasNote = hasNote || (n && n->kind() == Kind::Note);
            }
            if (!hasNote) {
                report(o, "has no notes");
            }
        }
        if (kind == Kind::Note) {
            const int64_t pitch = o.intOr(F::PITCH, -1);
            if (pitch < 0 || pitch > 127) {
                report(o, "has no valid pitch");
            }
            if (o.has(F::TIE_TO)) {
                const Object* to = s.find(o.idOr(F::TIE_TO));
                const Object* chord = s.find(o.parent());
                const Object* toChord = to ? s.find(to->parent()) : nullptr;
                if (!to || to->kind() != Kind::Note) {
                    report(o, "is tied to something that isn't a note");
                } else if (chord && toChord) {
                    const Fraction end = chord->fractionOr(F::TIME, Fraction()) + chord->fractionOr(F::DURATION, Fraction());
                    if (!(toChord->fractionOr(F::TIME, Fraction()) == end)) {
                        report(o, "is tied to a note that doesn't follow it");
                    }
                    if (to->intOr(F::PITCH, -1) != o.intOr(F::PITCH, -2)) {
                        report(o, "is tied to a note of another pitch");
                    }
                }
            }
        }
    });
    return problems;
}
}
