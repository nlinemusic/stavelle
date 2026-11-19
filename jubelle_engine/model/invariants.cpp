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
