/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::layout). Not part of MuseScore.
 *
 * Ported from MuseScore's ChordLayout::computeUp and computeAutoStemDirection (rendering/score/chordlayout.cpp), the
 * beam direction of BeamLayout::layout1 (rendering/score/beamlayout.cpp), Measure::hasVoices (dom/measure.cpp) and
 * Chord::noteDistances (dom/chord.cpp) (GPL-3.0).
 */
#include "stems.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

#include "model/detailskey.h"
#include "model/fields.h"
#include "model/fragment.h"

namespace je::layout {
namespace F = model::fields;
using model::Kind;
using model::Object;

namespace {
//! A chord's or beam's own stem direction as the file gives it (MuseScore's DirectionV): 1 up, -1 down, 0 auto
int directionOf(const Object* o)
{
    if (!o) {
        return 0;
    }
    if (o->has(F::DETAILS_KEY) && model::detailsKey(*o) != o->intOr(F::DETAILS_KEY, 0)) {
        return 0;   // its kept appearance no longer applies (the chord was changed)
    }
    std::string v = model::fragment::value(o->text(F::DETAILS), "StemDirection");
    if (v.empty()) {
        v = model::fragment::value(o->text(F::DETAILS_AFTER), "StemDirection");
    }
    return v == "up" ? 1 : v == "down" ? -1 : 0;
}

//! ChordLayout::computeAutoStemDirection: 1 up, 0 in the middle, -1 down
int autoDirection(const std::vector<int>& distances)
{
    int left = 0, right = int(distances.size()) - 1;
    while (left <= right) {
        const int net = distances[size_t(left)] + distances[size_t(right)];
        if (net == 0) {
            ++left;
            --right;
            continue;
        }
        return net > 0 ? 1 : -1;
    }
    return 0;
}

bool invisible(const Object* o)
{
    const std::string v = model::fragment::value(o->text(F::DETAILS), "visible");
    return v == "0" || v == "false";
}

struct Stems {
    const ScoreContext& ctx;
    std::vector<ChordLayout>& chords;
    const std::vector<Beam>& beams;
    std::map<Id, ChordLayout*> layoutOf;
    std::map<Id, int> beamOf;
    // the beams' own settings: (staff, voice, time) → the <Beam> kept before the first chord or rest it starts at
    std::map<std::tuple<int, int, Fraction>, const Object*> beamDetails;

    Stems(const ScoreContext& c, std::vector<ChordLayout>& ch, const std::vector<Beam>& b)
        : ctx(c), chords(ch), beams(b)
    {
        for (ChordLayout& cl : chords) {
            layoutOf[cl.chord] = &cl;
        }
        for (size_t i = 0; i < beams.size(); ++i) {
            for (Id e : beams[i].elements) {
                beamOf[e] = int(i);
            }
        }
        const Object* score = ctx.obj(ctx.snapshot().score());
        for (Id f : score->children()) {
            const Object* flow = ctx.obj(f);
            if (!flow || flow->kind() != Kind::Flow) {
                continue;
            }
            for (Id it : flow->children()) {
                const Object* o = ctx.obj(it);
                if (o && o->kind() == Kind::FlowItem && o->text(F::SUBKIND) == "detail" && o->text(F::TAG) == "Beam") {
                    const int st = ctx.staffIndex(o->idOr(F::ANCHOR));
                    if (st >= 0) {
                        beamDetails.emplace(std::make_tuple(st, int(o->intOr(F::VOICE_INDEX, 0)), o->fractionOr(F::TIME, Fraction())), o);
                    }
                }
            }
            break;
        }
    }

    const MeasureInfo* measureAt(const Fraction& t) const
    {
        const auto& ms = ctx.measures();
        auto it = std::upper_bound(ms.begin(), ms.end(), t, [](const Fraction& x, const MeasureInfo& m) { return x < m.start; });
        return it == ms.begin() ? nullptr : &*std::prev(it);
    }

    //! The chords and rests of voices 2 to 4 of a staff, by time (worked out once per staff)
    struct OtherEvent {
        Fraction time, end;
        bool shown;
    };
    mutable std::map<int, std::vector<OtherEvent> > otherVoices;

    const std::vector<OtherEvent>& otherVoicesOf(int staff) const
    {
        auto it = otherVoices.find(staff);
        if (it != otherVoices.end()) {
            return it->second;
        }
        std::vector<OtherEvent> out;
        const StaffInfo& st = ctx.staves()[size_t(staff)];
        for (int vn = 1; vn < 4; ++vn) {
            const Object* voice = ctx.obj(st.voices[size_t(vn)]);
            if (!voice) {
                continue;
            }
            for (Id e : voice->children()) {
                const Object* ev = ctx.obj(e);
                if (!ev || (ev->kind() != Kind::Chord && ev->kind() != Kind::Rest) || ev->has(F::GRACE)) {
                    continue;
                }
                const Fraction t = ev->fractionOr(F::TIME, Fraction());
                out.push_back({ t, t + ev->fractionOr(F::DURATION, Fraction()), shown(ev, staff) });
            }
        }
        std::stable_sort(out.begin(), out.end(), [](const OtherEvent& a, const OtherEvent& b) { return a.time < b.time; });
        return otherVoices.emplace(staff, std::move(out)).first->second;
    }

    //! Measure::hasVoices: whether voices 2 to 4 of the staff have something shown in [t, t + len) of the measure
    bool hasVoices(int staff, Fraction t, Fraction len) const
    {
        const MeasureInfo* m = measureAt(t);
        if (!m) {
            return false;
        }
        if (ctx.staves()[size_t(staff)].group == "tablature") {
            t = m->start;   // tablature: the whole measure
            len = m->length;
        }
        const Fraction end = t + len;
        const auto& evs = otherVoicesOf(staff);
        auto it = std::lower_bound(evs.begin(), evs.end(), m->start, [](const OtherEvent& e, const Fraction& x) { return e.time < x; });
        for (; it != evs.end() && it->time < m->end() && it->time < end; ++it) {
            if (it->end > t && it->shown) {
                return true;
            }
        }
        return false;
    }

    //! Shown, for hasVoices: a chord with a visible stem or a visible note; a visible rest
    bool shown(const Object* ev, int staff) const
    {
        if (ev->kind() == Kind::Rest) {
            return !invisible(ev);
        }
        const auto it = layoutOf.find(ev->id());
        const int type = it != layoutOf.end() ? it->second->durationType : 4;
        const std::string& d = ev->text(F::DETAILS);
        const bool noStem = model::fragment::value(d, "noStem") == "1" || model::fragment::value(ev->text(F::DETAILS_AFTER), "noStem") == "1";
        const bool hasStem = type >= 3 && type <= 12 && !noStem && !ctx.stemlessMeasure(staff, ev->fractionOr(F::TIME, Fraction()));
        if (hasStem) {
            const std::string stem = model::fragment::value(d, "Stem");
            if (stem.find("<visible>0</visible>") == std::string::npos) {
                return true;
            }
        }
        for (Id n : ev->children()) {
            const Object* note = ctx.obj(n);
            if (note && note->kind() == Kind::Note && !invisible(note)) {
                return true;
            }
        }
        return false;
    }

    //! Chord::noteDistances
    std::vector<int> distances(const ChordLayout& c) const
    {
        std::vector<int> out;
        const int mid = ctx.middleLine(c.staff, c.tick);
        for (const NoteLayout& n : c.notes) {
            out.push_back(n.line - mid);
        }
        return out;
    }

    int staffMoveOf(Id e) const
    {
        const Object* o = ctx.obj(e);
        return o ? int(o->intOr(F::STAFF_MOVE, 0)) : 0;
    }

    void run()
    {
        // the beams' directions first (BeamLayout::layout1), then every chord (ChordLayout::computeUp)
        std::vector<int> beamUp(beams.size(), 1);
        std::vector<bool> beamKnown(beams.size(), true);
        for (size_t i = 0; i < beams.size(); ++i) {
            const Beam& b = beams[i];
            int minMove = 1 << 20, maxMove = -(1 << 20);
            std::vector<std::pair<int, int> > positions;   // (distance, staff shown)
            const ChordLayout* custom = nullptr;
            Fraction ticks;
            for (Id e : b.elements) {
                const Object* o = ctx.obj(e);
                if (o) {
                    ticks += o->fractionOr(F::DURATION, Fraction());
                }
                const auto it = layoutOf.find(e);
                if (it == layoutOf.end()) {
                    continue;   // a rest
                }
                const int move = staffMoveOf(e);
                minMove = std::min(minMove, move);
                maxMove = std::max(maxMove, move);
                for (int d : distances(*it->second)) {
                    positions.push_back({ d, it->second->shownStaff });
                }
                if (!custom && directionOf(ctx.obj(e)) != 0) {
                    custom = it->second;
                }
            }
            const Object* first = ctx.obj(b.elements.front());
            const Fraction tick = first ? first->fractionOr(F::TIME, Fraction()) : Fraction();
            const bool cross = minMove != maxMove && minMove <= maxMove;
            const Object* detail = nullptr;
            if (const auto d = beamDetails.find({ b.staff, b.voice, tick }); d != beamDetails.end()) {
                detail = d->second;
            }
            const std::string beamXml = detail ? detail->text(F::DETAILS) : std::string();
            if (cross || beamXml.find("<crossStaffMove>") != std::string::npos) {
                beamKnown[i] = false;   // computeUpForMovedCross and isBelowCrossBeam: positions on the page
                continue;
            }
            const int beamDirection = beamXml.find("<StemDirection>up</StemDirection>") != std::string::npos ? 1
                                      : beamXml.find("<StemDirection>down</StemDirection>") != std::string::npos ? -1 : 0;
            if (beamXml.find("<Fragment>") != std::string::npos) {
                beamKnown[i] = false;   // placed by hand: its chords' directions follow the beam's position
            }
            const bool voices = hasVoices(b.staff, tick, ticks);
            const int voiceTrack = b.voice;
            bool up = true;
            if (beamDirection != 0) {
                up = beamDirection > 0;
            } else if (maxMove > 0 && minMove <= maxMove) {
                up = false;
            } else if (minMove < 0 && minMove <= maxMove) {
                up = true;
            } else if (b.grace) {
                up = voices ? voiceTrack % 2 == 0 : true;
            } else if (!positions.empty()) {
                if (voices) {
                    up = voiceTrack % 2 == 0;
                } else if (custom) {
                    up = directionOf(ctx.obj(custom->chord)) > 0;
                } else {
                    std::set<std::pair<int, int> > set(positions.begin(), positions.end());
                    std::vector<int> lines;
                    for (const auto& p : set) {
                        lines.push_back(p.first);
                    }
                    up = autoDirection(lines) > 0;
                }
            }
            if (minMove == maxMove && minMove != 0 && beamDirection == 0) {
                up = maxMove > 0;   // entirely moved to another staff
            }
            beamUp[i] = up ? 1 : 0;
        }

        for (ChordLayout& c : chords) {
            const Object* o = ctx.obj(c.chord);
            if (!o) {
                continue;
            }
            const auto bi = beamOf.find(c.chord);
            const bool hasBeam = bi != beamOf.end() && !c.stemless;   // ChordRest::beam: none in a stemless measure
            const bool tremolo = o->has(F::EXPLICIT_DURATION);   // one of a two-note tremolo's chords
            const int own = directionOf(o);
            if (own != 0 && !hasBeam && !tremolo) {
                c.up = own > 0;
                continue;
            }
            if (hasBeam) {
                c.up = beamUp[size_t(bi->second)] != 0;
                c.upKnown = beamKnown[size_t(bi->second)];
                continue;
            }
            if (tremolo) {
                c.upKnown = false;   // TremoloLayout: the tremolo's own direction (with S7's tremolo stage)
                continue;
            }
            if (hasVoices(c.staff, o->fractionOr(F::TIME, Fraction()), o->fractionOr(F::DURATION, Fraction()))) {
                c.up = c.voice % 2 == 0;
                continue;
            }
            if (c.kind != 'C') {
                c.up = true;   // grace notes
                continue;
            }
            const int move = staffMoveOf(c.chord);
            if (move != 0) {
                c.up = move > 0;
                continue;
            }
            c.up = autoDirection(distances(c)) > 0;
        }
    }
};
}

void layoutStemDirections(const ScoreContext& ctx, std::vector<ChordLayout>& chords, const std::vector<Beam>& beams)
{
    Stems s(ctx, chords, beams);
    s.run();
}

namespace {
//! MuseScore's DurationType::hooks for the shown value (0 longa … 12 1024th)
int hooksOf(int durationType)
{
    return durationType >= 5 && durationType <= 12 ? durationType - 4 : 0;
}

//! StemLayout::minStaffOverlap
int minStaffOverlap(bool up, int staffLines, int beamCount, bool hasHook, double beamSpacing, bool useWideBeams, bool isFullSize)
{
    int beamOverlap = 8;
    if (isFullSize) {
        if (beamCount == 3 && !hasHook) {
            beamOverlap = 12;
        } else if (beamCount >= 4 && !hasHook) {
            beamOverlap = int((beamCount - 4) * beamSpacing + (useWideBeams ? 16 : 14));
        }
    }
    const int staffLineOffset = isFullSize ? 1 : 4;
    const int staffOverlap = std::min(beamOverlap, (staffLines - staffLineOffset) * 4);
    return up ? (staffLines - 1) * 4 - staffOverlap : staffOverlap;
}
}

void layoutStemLengths(const ScoreContext& ctx, std::vector<ChordLayout>& chords, const std::vector<Beam>& beams)
{
    const style::Style& st = ctx.style();
    const style::MusicFont& font = ctx.font();
    const double unit = style::Style::def(Sid::spatium).a;
    std::set<Id> beamed;
    for (const Beam& b : beams) {
        for (Id e : b.elements) {
            beamed.insert(e);
        }
    }
    const bool wide = st.boolean(Sid::useWideBeams);
    const bool straight = st.boolean(Sid::useStraightNoteFlags);
    for (ChordLayout& c : chords) {
        if (c.notes.empty()) {
            continue;
        }
        const Object* co = ctx.obj(c.chord);
        auto detail = [&](const std::string& tag) {
            std::string v = model::fragment::value(co->text(F::DETAILS), tag);
            return v.empty() ? model::fragment::value(co->text(F::DETAILS_AFTER), tag) : v;
        };
        const bool hasBeam = beamed.count(c.chord) && !c.stemless;
        const bool tremoloTwo = co->has(F::EXPLICIT_DURATION);
        c.hasStem = c.durationType >= 3 && c.durationType <= 12 && detail("noStem") != "1" && !c.stemless;
        c.hook = c.hasStem && hooksOf(c.durationType) > 0 && !hasBeam && !tremoloTwo;
        if (c.hook) {
            static const SymId up[8] = { SymId::flag8thUp, SymId::flag16thUp, SymId::flag32ndUp, SymId::flag64thUp, SymId::flag128thUp,
                                         SymId::flag256thUp, SymId::flag512thUp, SymId::flag1024thUp };
            static const SymId down[8] = { SymId::flag8thDown, SymId::flag16thDown, SymId::flag32ndDown, SymId::flag64thDown,
                                           SymId::flag128thDown, SymId::flag256thDown, SymId::flag512thDown, SymId::flag1024thDown };
            static const SymId upS[8] = { SymId::flag8thUpStraight, SymId::flag16thUpStraight, SymId::flag32ndUpStraight,
                                          SymId::flag64thUpStraight, SymId::flag128thUpStraight, SymId::flag256thUpStraight,
                                          SymId::flag512thUpStraight, SymId::flag1024thUpStraight };
            static const SymId downS[8] = { SymId::flag8thDownStraight, SymId::flag16thDownStraight, SymId::flag32ndDownStraight,
                                            SymId::flag64thDownStraight, SymId::flag128thDownStraight, SymId::flag256thDownStraight,
                                            SymId::flag512thDownStraight, SymId::flag1024thDownStraight };
            const int i = hooksOf(c.durationType) - 1;
            c.hookSym = straight ? (c.up ? upS[i] : downS[i]) : (c.up ? up[i] : down[i]);
        }
        // a one-chord tremolo sets a minimum length (not ported yet); before 4.20 files call it <Tremolo>
        bool tremoloOne = model::fragment::has(co->text(F::DETAILS), "Tremolo") || model::fragment::has(co->text(F::DETAILS_AFTER), "Tremolo");
        for (Id a : co->children()) {
            const Object* ao = ctx.obj(a);
            tremoloOne = tremoloOne || (ao && ao->kind() == Kind::Attachment && ao->text(F::SUBKIND) == "tremoloSingleChord");
        }
        if (!c.standard || hasBeam || tremoloTwo || tremoloOne || !c.upKnown) {
            continue;   // a beam or tremolo decides the length (later stages)
        }
        const StaffTypeInfo& type = ctx.staffType(c.staff, c.tick);
        const bool small = detail("small") == "1";
        const double intrinsic = (small ? st.value(Sid::smallNoteMag) : 1.0) * (c.kind != 'C' ? st.value(Sid::graceNoteMag) : 1.0);
        const NoteLayout& top = c.notes.back();      // upNote
        const NoteLayout& bottom = c.notes.front();  // downNote
        const NoteLayout& ref = c.up ? top : bottom;
        double ax = 0.0, ay = 0.0;
        const Object* refObj = ctx.obj(ref.note);
        const bool refSmall = model::fragment::value(refObj->text(F::DETAILS), "small") == "1";
        font.anchor(c.up ? ref.headUp : ref.headDown, c.up ? style::SmuflAnchor::stemUpSE : style::SmuflAnchor::stemDownNW,
                    refSmall ? st.value(Sid::smallNoteMag) : 1.0, ax, ay);
        const double extraHeight = ay / unit;   // in the staff's spaces, without the chord's own size

        // all in quarter spaces, in integers, as MuseScore works it out
        const int defaultStem = int(st.value(Sid::stemLength) * 4);   // no beam: no addition
        const int shortestStem = wide ? 12 : int((st.value(Sid::shortestStem) + std::abs(extraHeight)) * 4);
        const int qspl = int(std::floor(type.lineDistance * 2));
        const int upLine = top.line, downLine = bottom.line;
        const int chordHeight = (downLine - upLine) * qspl;
        const int lines = type.lines;
        const int shortStemStart = st.integer(Sid::shortStemStartLocation) * qspl + 1;
        const bool fullSize = !(c.kind != 'C' || small);
        const int middleLine = minStaffOverlap(c.up, lines, 0, c.hook, wide ? 4 : 3, wide, fullSize);
        // StemLayout::maxReduction, without beams
        auto maxReduction = [&](int extensionOutsideStaff) {
            if (!st.boolean(Sid::shortenStem)) {
                return 0;
            }
            static const int reductions[5] = { 1, 2, 3, 4, 4 };   // 0 beams (a flag counts as none here)
            const bool tradHook = c.hook && !straight;
            const int halfSpaces = std::min(int(std::floor(extensionOutsideStaff / 2.0)), 4);
            int r = reductions[halfSpaces];
            if (intrinsic < 1) {
                if (tradHook) {
                    r = std::min(r, 1);
                }
            } else if (tradHook) {
                r = std::min(r, 1);
            } else if (c.hook && hooksOf(c.durationType) > 2) {
                r += 1;
            }
            return r;
        };
        int ideal = defaultStem;
        if (c.up) {
            const int end = upLine * qspl - defaultStem;
            const double endMag = double(upLine) * qspl - defaultStem * intrinsic;
            if (endMag <= -shortStemStart) {
                ideal = std::max(ideal - maxReduction(std::abs(int(std::floor(endMag)) + shortStemStart)), shortestStem);
            } else if (end > middleLine) {
            } else {
                ideal = std::max(ideal, shortestStem);   // no optical adjustment without beams
            }
        } else {
            const int end = downLine * qspl + defaultStem;
            const double endMag = double(downLine) * qspl + defaultStem * intrinsic;
            const int downShortStemStart = (lines - 1) * (2 * qspl) + shortStemStart;
            if (endMag >= downShortStemStart) {
                ideal = std::max(ideal - maxReduction(std::abs(int(std::ceil(endMag)) - downShortStemStart)), shortestStem);
            } else if (end < middleLine) {
            } else {
                ideal = std::max(ideal, shortestStem);
            }
        }
        const int stemLength = std::max(ideal, 0);   // the minimum: tremolos and beams only
        double length = chordHeight / 4.0 + stemLength / 4.0 * intrinsic;
        const NoteLayout& start = c.up ? bottom : top;
        const Object* startObj = ctx.obj(start.note);
        const std::string fixed = model::fragment::value(startObj->text(F::DETAILS_AFTER), "fixed").empty()
                                      ? model::fragment::value(startObj->text(F::DETAILS), "fixed")
                                      : model::fragment::value(startObj->text(F::DETAILS_AFTER), "fixed");
        if (fixed != "1") {
            // a small chord's stem would end short of the middle line: lengthen it to there
            const double up = c.up ? -1.0 : 1.0;
            const double stemStart = (start.line + type.stepOffset) * 0.5 * type.lineDistance;
            const double endMag = stemStart + length * up;
            const double ld = type.lineDistance;
            const double bottomLine = ld * (lines - 1.0);
            const double midLine = middleLine / 4.0 * ld;
            double target;
            if (ld >= 1.0 - 1e-9) {
                if (bottomLine < 2) {
                    target = c.up ? 0.0 : bottomLine;
                } else {
                    const double twoIn = c.up ? bottomLine - 2 : 2;
                    target = std::abs(ld - 1.0) < 1e-9 ? midLine : twoIn;
                }
            } else {
                target = lines < 3 ? (c.up ? 0.0 : bottomLine) : (c.up ? bottomLine - 2 * ld : 2 * ld);
            }
            if (c.up && endMag > target) {
                length += endMag - target;
            } else if (!c.up && endMag < target) {
                length += target - endMag;
            }
        }
        // the user's length (<Stem><userLen>)
        const std::string stem = detail("Stem");
        const std::string user = model::fragment::anyValue("<x>" + stem + "</x>", "userLen");
        if (!user.empty()) {
            length += std::atof(user.c_str());
        }
        c.stemLength = length;
        c.stemLengthKnown = c.hasStem;
    }
}
}
