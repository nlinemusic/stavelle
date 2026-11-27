/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::layout). Not part of MuseScore.
 *
 * Ported from MuseScore's ChordLayout::layoutChords1, calculateChordPosInfo, calculateMaxNoteWidths and layoutChords2
 * (rendering/score/chordlayout.cpp) and Chord::combineVoice (dom/chord.cpp) (GPL-3.0).
 */
#include "chords.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <tuple>

#include "model/fields.h"
#include "model/fragment.h"

namespace je::layout {
namespace F = model::fields;
using model::Kind;
using model::Object;

namespace {
std::string valueOf(const Object* o, const std::string& tag)
{
    if (!o) {
        return std::string();
    }
    std::string v = model::fragment::value(o->text(F::DETAILS), tag);
    return v.empty() ? model::fragment::value(o->text(F::DETAILS_AFTER), tag) : v;
}

struct Mirrors {
    const ScoreContext& ctx;
    const bool combineByDefault;

    explicit Mirrors(const ScoreContext& c)
        : ctx(c), combineByDefault(c.style().boolean(Sid::combineVoice))
    {
    }

    //! Chord::shouldCombineVoice
    bool combines(const ChordLayout& c) const
    {
        const std::string v = valueOf(ctx.obj(c.chord), "combineVoice");
        return v == "on" || v == "1" || ((v.empty() || v == "auto") && combineByDefault);
    }

    struct Entry {
        NoteLayout* note;
        ChordLayout* chord;
    };

    //! layoutChords2: which notes are mirrored, the notes in order (bottom up: the first encountered wins a conflict)
    void mirror(std::vector<Entry>& notes, bool up) const
    {
        if (notes.empty()) {
            return;
        }
        const int n = int(notes.size());
        const int start = up ? 0 : n - 1, end = up ? n : -1, inc = up ? 1 : -1;
        int prevLine = 1000;   // so that the first note is no conflict
        bool prevVisible = false;
        bool isLeft = notes[size_t(start)].chord->up;
        const ChordLayout* prevChord = notes[size_t(start)].chord;
        for (int i = start; i != end; i += inc) {
            NoteLayout& note = *notes[size_t(i)].note;
            const ChordLayout& chord = *notes[size_t(i)].chord;
            const Object* no = ctx.obj(note.note);
            const std::string vis = valueOf(no, "visible");
            const bool visible = !(vis == "0" || vis == "false");
            const bool sameTrack = chord.staff == prevChord->staff && chord.voice == prevChord->voice;
            const bool conflict = std::abs(prevLine - note.line) < 2 && prevChord->shownStaff == chord.shownStaff && visible && prevVisible
                                  && (sameTrack || (combines(chord) && combines(*prevChord)));
            if (conflict || chord.up != isLeft) {
                isLeft = !isLeft;
            }
            const bool natural = chord.up != isLeft;
            const std::string user = valueOf(no, "mirror");
            if (user == "left" || user == "right") {
                note.mirror = user == "left" ? !chord.up : chord.up;
            } else {
                note.mirror = natural;
            }
            prevChord = &chord;
            prevVisible = visible;
            prevLine = note.line;
        }
    }

    void run(std::vector<ChordLayout>& chords) const
    {
        // the segments: main chords by time and the staff they are shown on, in track order; grace chords with theirs
        std::map<std::pair<Fraction, int>, std::vector<size_t> > segments;
        std::map<size_t, std::vector<size_t> > gracesOf;
        std::vector<size_t> pendingBefore;
        size_t lastMain = size_t(-1);
        for (size_t i = 0; i < chords.size(); ++i) {
            const ChordLayout& c = chords[i];
            if (c.kind == 'G') {
                pendingBefore.push_back(i);
            } else if (c.kind == 'A') {
                if (lastMain != size_t(-1)) {
                    gracesOf[lastMain].push_back(i);
                }
            } else {
                gracesOf[i] = std::move(pendingBefore);
                pendingBefore.clear();
                lastMain = i;
                segments[{ c.tick, c.shownStaff }].push_back(i);
            }
        }
        for (auto& [key, list] : segments) {
            std::stable_sort(list.begin(), list.end(), [&](size_t a, size_t b) {
                return chords[a].staff * 4 + chords[a].voice < chords[b].staff * 4 + chords[b].voice;
            });
            std::vector<Entry> upNotes, downNotes;
            int upVoices = 0, downVoices = 0;
            bool known = true;
            for (size_t i : list) {
                known = known && chords[i].upKnown;
            }
            for (size_t i : list) {
                chords[i].mirrorKnown = known;
            }
            for (size_t i : list) {
                ChordLayout& c = chords[i];
                for (size_t g : gracesOf[i]) {
                    std::vector<Entry> notes;
                    for (NoteLayout& n : chords[g].notes) {
                        notes.push_back({ &n, &chords[g] });
                    }
                    mirror(notes, chords[g].up);
                }
                std::vector<Entry>& into = c.up ? upNotes : downNotes;
                ++(c.up ? upVoices : downVoices);
                for (NoteLayout& n : c.notes) {
                    into.push_back({ &n, &c });
                }
            }
            // (only staves whose stems go through the staff: every staff but tablature, which isn't laid out here)
            auto byLine = [](const Entry& a, const Entry& b) { return a.note->line > b.note->line; };
            if (upVoices > 1) {
                std::sort(upNotes.begin(), upNotes.end(), byLine);
            }
            mirror(upNotes, true);
            if (downVoices > 1) {
                std::sort(downNotes.begin(), downNotes.end(), byLine);
            }
            mirror(downNotes, false);
        }
    }
};
}

void layoutMirrors(const ScoreContext& ctx, std::vector<ChordLayout>& chords)
{
    Mirrors(ctx).run(chords);
}

void layoutNotePositions(const ScoreContext& ctx, std::vector<ChordLayout>& chords)
{
    const style::Style& st = ctx.style();
    const style::MusicFont& font = ctx.font();
    const double unit = style::Style::def(Sid::spatium).a;   // the font's engraving units per space
    auto small = [&](const Object* o) {
        const std::string v = valueOf(o, "small");
        return v == "1" || v == "true";
    };
    for (ChordLayout& c : chords) {
        const Object* co = ctx.obj(c.chord);
        // Chord::mag and intrinsicMag
        double mag = ctx.staffMag(c.staff, c.tick);
        if (small(co)) {
            mag *= st.value(Sid::smallNoteMag);
        }
        if (c.kind != 'C') {
            mag *= st.value(Sid::graceNoteMag);
        }
        c.mag = mag;
        if (c.notes.empty()) {
            continue;
        }
        const StaffTypeInfo& type = ctx.staffType(c.staff, c.tick);
        const double staffMag = ctx.staffMag(c.staff, c.tick);
        auto noteMag = [&](const NoteLayout& n) { return small(ctx.obj(n.note)) ? mag * st.value(Sid::smallNoteMag) : mag; };
        auto head = [&](const NoteLayout& n) { return c.up ? n.headUp : n.headDown; };
        // Note::headBodyWidth: the notehead's width and its left side bearing, twice
        auto bodyWidth = [&](const NoteLayout& n) {
            const style::RectF b = font.bbox(head(n), noteMag(n));
            return (b.width() + 2 * b.left()) / unit;
        };
        const double noteHeadWidth = font.width(SymId::noteheadBlack) / unit * mag;   // Chord::noteHeadWidth
        // StemLayout::stemPosX, from the top note's stem-up or the bottom note's stem-down anchor
        const NoteLayout& ref = c.up ? c.notes.back() : c.notes.front();
        double ax = 0.0, ay = 0.0;
        font.anchor(head(ref), c.up ? style::SmuflAnchor::stemUpSE : style::SmuflAnchor::stemDownNW, noteMag(ref), ax, ay);
        double stemX;
        if (ax == 0.0 && ay == 0.0) {
            stemX = c.up ? noteHeadWidth : 0.0;
        } else {
            stemX = ax / unit - (c.up ? bodyWidth(ref) - noteHeadWidth : 0.0);
        }
        c.stemX = stemX / staffMag;
        // the stem's width overlaps a mirrored notehead (none without a stem, but for a whole note's)
        const std::string noStem = valueOf(co, "noStem");
        const bool hasStem = c.durationType >= 3 && c.durationType <= 12 && noStem != "1" && !c.stemless;
        const double overlap = hasStem || c.durationType == 2 ? st.value(Sid::stemWidth) * mag : 0.0;
        for (NoteLayout& n : c.notes) {
            // worked out in the score's spaces, kept in the staff's (a small staff's notes are a small staff's spaces)
            if (n.mirror) {
                n.x = c.up ? stemX - overlap : -bodyWidth(n) + overlap;
            } else {
                n.x = c.up ? noteHeadWidth - bodyWidth(n) : 0.0;
            }
            n.x /= staffMag;
            n.y = (n.line + type.stepOffset) * 0.5 * type.lineDistance;
            // the user's offset (the note's own <offset x="…" y="…"/>, in the staff's spaces)
            const Object* no = ctx.obj(n.note);
            for (const std::string* d : { &no->text(F::DETAILS), &no->text(F::DETAILS_AFTER) }) {
                size_t b = 0, e = 0;
                std::vector<model::fragment::Child> kids;
                if (!model::fragment::children(*d, b, e, kids)) {
                    continue;
                }
                bool found = false;
                for (const model::fragment::Child& k : kids) {
                    if (k.name != "offset") {
                        continue;
                    }
                    const std::string tag = d->substr(k.begin, d->find('>', k.begin) - k.begin);
                    auto attr = [&](const char* name) {
                        const std::string key = std::string(" ") + name + "=\"";
                        const size_t a = tag.find(key);
                        return a == std::string::npos ? 0.0 : std::atof(tag.c_str() + a + key.size());
                    };
                    n.x += attr("x");
                    n.y += attr("y");
                    found = true;
                    break;
                }
                if (found) {
                    break;
                }
            }
        }
    }
}
}

namespace je::layout {
namespace {
//! MuseScore's NoteHeadType of a value: 0 whole, 1 half, 2 quarter, 3 breve (and longer)
int headTypeOf(int durationType)
{
    return durationType <= 1 ? 3 : durationType == 2 ? 0 : durationType == 3 ? 1 : 2;
}

int userHeadType(const Object* note)
{
    const std::string v = valueOf(note, "headType");
    return v == "whole" ? 0 : v == "half" ? 1 : v == "quarter" ? 2 : v == "breve" ? 3 : -1;   // -1: auto
}

struct Offsets {
    const ScoreContext& ctx;
    const style::Style& st;
    const style::MusicFont& font;
    const double unit;

    explicit Offsets(const ScoreContext& c)
        : ctx(c), st(c.style()), font(c.font()), unit(style::Style::def(Sid::spatium).a)
    {
    }

    struct N {
        NoteLayout* note;
        ChordLayout* chord;
        int line() const { return note->line; }
        int track() const { return chord->staff * 4 + chord->voice; }
    };

    bool combines(const ChordLayout& c) const
    {
        const std::string v = valueOf(ctx.obj(c.chord), "combineVoice");
        return v == "on" || v == "1" || ((v.empty() || v == "auto") && st.boolean(Sid::combineVoice));
    }
    bool small(const Object* o) const
    {
        const std::string v = valueOf(o, "small");
        return v == "1" || v == "true";
    }
    double noteMag(const N& n) const { return small(ctx.obj(n.note->note)) ? n.chord->mag * st.value(Sid::smallNoteMag) : n.chord->mag; }
    SymId head(const N& n) const { return n.chord->up ? n.note->headUp : n.note->headDown; }
    bool visible(const N& n) const
    {
        const std::string v = valueOf(ctx.obj(n.note->note), "visible");
        return !(v == "0" || v == "false");
    }

    //! The stem's bounding height (TLayout::layoutStem), in the score's spaces
    double stemHeight(const ChordLayout& c, double staffMag) const
    {
        const NoteLayout& start = c.up ? c.notes.front() : c.notes.back();
        const double up = c.up ? -1.0 : 1.0;
        const double m = small(ctx.obj(start.note)) ? c.mag * st.value(Sid::smallNoteMag) : c.mag;
        double ax = 0.0, ay = 0.0;
        const SymId h = c.up ? start.headUp : start.headDown;
        const bool useUpAnchor = (c.up && !start.mirror) || (!c.up && start.mirror);
        font.anchor(h, useUpAnchor ? style::SmuflAnchor::stemUpSE : style::SmuflAnchor::stemDownNW, m, ax, ay);
        const double y1 = ay / unit;
        double y2 = up * c.stemLength * staffMag;
        if (c.hook && !c.beamed) {
            double hx = 0.0, hy = 0.0;
            font.anchor(c.hookSym, c.up ? style::SmuflAnchor::stemUpNW : style::SmuflAnchor::stemDownSW, c.mag, hx, hy);
            y2 += hy / unit;
        }
        if (c.beamed) {
            y2 -= up * st.value(Sid::beamWidth) * staffMag * 0.5 * (small(ctx.obj(c.chord)) ? st.value(Sid::smallNoteMag) : 1.0);
        }
        return std::abs(y2 - y1);
    }

    void segment(std::vector<ChordLayout*>& list, const std::map<ChordLayout*, bool>& graceBefore) const
    {
        for (ChordLayout* c : list) {
            c->x = 0.0;
            c->xKnown = false;
        }
        for (ChordLayout* c : list) {
            if (!c->standard || !c->upKnown || !c->mirrorKnown || (c->hasStem && !c->defaultStemKnown)) {
                return;   // depends on what isn't worked out yet
            }
            for (const NoteLayout& n : c->notes) {
                if (!n.headKnown) {
                    return;
                }
            }
        }
        const int staff = list.front()->shownStaff;
        const Fraction tick = list.front()->tick;
        const double staffMag = ctx.staffMag(staff, tick);
        const double sp = staffMag;   // the staff's space, in the score's
        const int lines = ctx.staffType(staff, tick).lines;

        // calculateChordPosInfo
        int upVoices = 0, downVoices = 0, upDots = 0, downDots = 0;
        double maxUpMag = 0.0, maxDownMag = 0.0;
        const ChordLayout* upHooks = nullptr;
        const ChordLayout* downHooks = nullptr;
        bool upGrace = false, downGrace = false;
        std::vector<N> upNotes, downNotes;
        for (ChordLayout* c : list) {
            const bool gb = graceBefore.count(c) && graceBefore.at(c);
            if (c->up) {
                ++upVoices;
                for (NoteLayout& n : c->notes) {
                    upNotes.push_back({ &n, c });
                }
                upDots = std::max(upDots, c->dots);
                maxUpMag = std::max(maxUpMag, c->mag);
                if (!upHooks && !c->beamed && c->hook) {
                    upHooks = c;
                }
                upGrace = upGrace || gb;
            } else {
                ++downVoices;
                for (NoteLayout& n : c->notes) {
                    downNotes.push_back({ &n, c });
                }
                downDots = std::max(downDots, c->dots);
                maxDownMag = std::max(maxDownMag, c->mag);
                if (!downHooks && !c->beamed && c->hook) {
                    downHooks = c;
                }
                downGrace = downGrace || gb;
            }
        }
        // calculateMaxNoteWidths (the widths of the noteheads left unmirrored: layoutChords2's result)
        const double noteHeadWidth = font.width(SymId::noteheadBlack) / unit;
        const double nominal = noteHeadWidth * staffMag;
        double maxUpWidth = nominal * maxUpMag, maxDownWidth = nominal * maxDownMag;
        auto byLine = [](const N& a, const N& b) { return a.line() > b.line(); };
        if (upVoices > 1) {
            std::sort(upNotes.begin(), upNotes.end(), byLine);
        }
        if (downVoices > 1) {
            std::sort(downNotes.begin(), downNotes.end(), byLine);
        }
        auto widest = [&](const std::vector<N>& notes) {
            double w = 0.0;
            for (const N& n : notes) {
                if (!n.note->mirror) {
                    w = std::max(w, font.bbox(head(n), noteMag(n)).right() / unit);
                }
            }
            return w;
        };
        if (upVoices) {
            maxUpWidth = std::max(maxUpWidth, widest(upNotes));
        }
        if (downVoices) {
            maxDownWidth = std::max(maxDownWidth, widest(downNotes));
        }

        // centreChords
        double centerUp = 0, centerDown = 0, centerAdjustUp = 0, centerAdjustDown = 0, oversizeUp = 0;
        double upOffset = 0, downOffset = 0, dotAdjust = 0, dotAdjustThreshold = 0;
        std::set<int> tracksToAdjust;
        const double threshold = 0.01 * sp;
        {
            double headDiff = maxUpWidth - nominal;
            const double headDiff2 = maxUpWidth - nominal * (maxUpMag / staffMag);
            if (headDiff > threshold) {
                centerUp = headDiff * -0.5;
                maxUpWidth += centerUp;
                if (headDiff2 > threshold) {
                    centerUp += headDiff2;
                    oversizeUp = headDiff2;
                }
            } else if (-headDiff > threshold) {
                centerUp = -headDiff * 0.5;
                if (headDiff2 > threshold) {
                    centerUp += headDiff2;
                    oversizeUp = headDiff2;
                }
                centerAdjustDown = centerUp;
            }
            headDiff = maxDownWidth - nominal;
            if (headDiff > threshold) {
                centerDown = headDiff * -0.5;
                maxDownWidth = nominal - centerDown;
            } else if (-headDiff > threshold) {
                centerDown = -headDiff * 0.5;
                centerAdjustUp = centerDown;
            }
        }

        // calculateChordOffsets: a chord with its stem up against one with its stem down
        int separation = 0;
        if (upVoices && downVoices) {
            const N bottomUp = upNotes.front();
            const N topDown = downNotes.back();
            separation = topDown.line() - bottomUp.line();
            const double stemWidth = st.value(Sid::stemWidth);
            if (separation == 1) {
                if (upDots && !downDots) {
                    upOffset = maxDownWidth + 0.1 * sp;
                    tracksToAdjust.insert(bottomUp.track());
                } else {
                    downOffset = maxUpWidth;
                    if (topDown.chord->hasStem && bottomUp.chord->hasStem) {
                        downOffset -= stemWidth * topDown.chord->mag;
                    } else if (headTypeOf(topDown.chord->durationType) != 3 && headTypeOf(bottomUp.chord->durationType) != 3) {
                        downOffset -= stemWidth * topDown.chord->mag;
                    }
                    tracksToAdjust.insert(topDown.track());
                }
            } else if (separation < 1) {
                std::vector<N> overlap;
                for (const N& n : upNotes) {
                    if (n.line() >= topDown.line() - 1) {
                        overlap.push_back(n);
                    } else {
                        break;
                    }
                }
                for (size_t i = downNotes.size(); i > 0; --i) {
                    if (downNotes[i - 1].line() <= bottomUp.line() + 1) {
                        overlap.push_back(downNotes[i - 1]);
                    } else {
                        break;
                    }
                }
                std::sort(overlap.begin(), overlap.end(), byLine);
                bool shareHeads = true, matchPending = false, conflictUnison = false;
                bool secondUpHigher = false, secondDownHigher = false;
                int lastLine = 1000;
                int lastTrack = -1;
                N p = overlap[0];
                for (const N& n : overlap) {
                    if (n.note->mirror && separation < 0) {
                        shareHeads = false;
                        continue;
                    }
                    const int d = lastLine - n.line();
                    if (d == 0) {
                        conflictUnison = true;
                        matchPending = false;
                        const Object* no = ctx.obj(n.note->note);
                        const Object* po = ctx.obj(p.note->note);
                        const int nUser = userHeadType(no), pUser = userHeadType(po);
                        const int nHead = nUser < 0 ? headTypeOf(n.chord->durationType) : nUser;
                        const int pHead = pUser < 0 ? headTypeOf(p.chord->durationType) : pUser;
                        const bool nSmall = small(ctx.obj(n.chord->chord)), pSmall = small(ctx.obj(p.chord->chord));
                        if (valueOf(no, "head") != valueOf(po, "head") || n.note->tpc != p.note->tpc || n.note->mirror || p.note->mirror
                            || (nSmall != pSmall && (nHead != 2 || pHead != 2))) {
                            shareHeads = false;
                        } else if ((n.chord->dots != p.chord->dots || !n.chord->hasStem || !p.chord->hasStem || nHead != pHead
                                    || small(no) || small(po))
                                   && ((nUser < 0 && pUser < 0) || nHead != pHead) && visible(n) == visible(p)) {
                            shareHeads = false;
                        }
                    } else if (d == 1) {
                        if (separation < 0) {
                            (n.chord->up ? secondUpHigher : secondDownHigher) = true;
                            shareHeads = false;
                        }
                    } else {
                        if (matchPending) {
                            shareHeads = false;
                        }
                        matchPending = true;
                    }
                    if (!shareHeads) {
                        tracksToAdjust.insert(n.track());
                        tracksToAdjust.insert(lastTrack);
                    }
                    p = n;
                    lastLine = n.line();
                    lastTrack = n.track();
                }
                if (matchPending) {
                    shareHeads = false;
                }
                const bool conflict = conflictUnison || secondDownHigher || secondUpHigher;
                bool ledgerBelow = false, ledgerAbove = false;
                const double ledgerGap = 0.15 * sp;
                const double ledgerLen = st.value(Sid::ledgerLineLength) * sp;
                const int firstLedgerBelow = lines * 2;
                int topDownStemLen = 0;
                // a beamed chord's stem is laid out again once its beam is placed (a later stage), and MuseScore lays
                // the chords out again with it: where its length decides, the offsets aren't known yet
                bool beamedStemDecides = false;
                if (!conflictUnison && topDown.chord->hasStem && topDown.chord->beamed && bottomUp.line() > firstLedgerBelow - 1
                    && topDown.line() < bottomUp.line()) {
                    beamedStemDecides = true;
                }
                if (!conflictUnison && bottomUp.chord->hasStem && bottomUp.chord->beamed && topDown.line() < -1
                    && topDown.line() < bottomUp.line()) {
                    beamedStemDecides = true;
                }
                if (!conflictUnison && topDown.chord->hasStem && topDown.chord->beamed && downHooks) {
                    beamedStemDecides = true;
                }
                if (beamedStemDecides) {
                    return;
                }
                if (!conflictUnison && topDown.chord->hasStem) {
                    topDownStemLen = int(std::round(stemHeight(*topDown.chord, staffMag) / sp * 2));
                    if (bottomUp.line() > firstLedgerBelow - 1 && topDown.line() < bottomUp.line()
                        && topDown.line() + topDownStemLen >= firstLedgerBelow) {
                        ledgerBelow = true;
                    }
                }
                const int firstLedgerAbove = -2;
                if (!conflictUnison && bottomUp.chord->hasStem) {
                    const int bottomUpStemLen = int(std::round(stemHeight(*bottomUp.chord, staffMag) / sp * 2));
                    if (topDown.line() < -1 && topDown.line() < bottomUp.line() && bottomUp.line() - bottomUpStemLen <= firstLedgerAbove) {
                        ledgerAbove = true;
                    }
                }
                auto hookWidth = [&](const ChordLayout* c) { return c && c->hook ? font.width(c->hookSym, c->mag) / unit : 0.0; };
                if (shareHeads) {
                    for (int i = int(overlap.size()) - 1; i >= 1; i -= 2) {
                        const N& prev = overlap[size_t(i - 1)];
                        const N& n = overlap[size_t(i)];
                        if (!userOffset(prev.chord->chord).first.empty() || !userOffset(n.chord->chord).first.empty()) {
                            continue;   // a chord moved by hand (isNudged)
                        }
                        const bool prevSmall = small(ctx.obj(prev.chord->chord)), nSmall = small(ctx.obj(n.chord->chord));
                        const ChordLayout* smallChord = nSmall ? n.chord : prevSmall ? prev.chord : nullptr;
                        if (smallChord && !(prevSmall && nSmall)) {
                            if (smallChord->up) {
                                centerUp *= 2;
                            } else {
                                centerDown = 0;
                            }
                        }
                    }
                } else if (conflict && (upDots && !downDots)) {
                    upOffset = maxDownWidth + 0.1 * sp;
                } else if (conflictUnison && separation == 0 && (!downGrace || upGrace)) {
                    downOffset = maxUpWidth + 0.15 * sp;
                } else if (conflictUnison) {
                    upOffset = maxDownWidth + 0.15 * sp;
                } else if (secondUpHigher) {
                    upOffset = maxDownWidth + 0.15 * sp;
                } else if ((downHooks && !upHooks) && !(upDots && !downDots)) {
                    const double adj = (ledgerAbove || ledgerBelow) ? ledgerGap + ledgerLen : 0.3 * sp;
                    downOffset = maxUpWidth + adj;
                } else if (secondDownHigher) {
                    if (downDots && !upDots) {
                        const double adj = (ledgerAbove || ledgerBelow) ? ledgerGap + ledgerLen : 0.2 * sp;
                        downOffset = maxUpWidth + adj;
                    } else {
                        const double adj = (topDown.line() <= firstLedgerAbove || bottomUp.line() >= firstLedgerBelow)
                                           ? ledgerLen - ledgerGap - 0.2 * sp : -0.2 * sp;
                        upOffset = maxDownWidth + adj;
                        if (downHooks) {
                            const bool needsHookSpace = ledgerBelow || ledgerAbove;
                            upOffset = needsHookSpace ? hookWidth(topDown.chord) + ledgerLen + ledgerGap : upOffset + 0.3 * sp;
                        }
                    }
                } else {
                    // no direct conflict: the stems only need to clear the other noteheads and ledger lines
                    double clearLeft = 0.0, clearRight = 0.0;
                    const double padding = 0.3 * sp;
                    if (topDown.chord->hasStem) {
                        clearLeft += ledgerBelow ? ledgerLen + ledgerGap + stemWidth * topDown.chord->mag : stemWidth * topDown.chord->mag + padding;
                    }
                    if (bottomUp.chord->hasStem) {
                        if (ledgerAbove) {
                            clearRight += maxDownWidth + ledgerLen + ledgerGap - maxUpWidth + stemWidth * bottomUp.chord->mag;
                        } else {
                            clearRight += stemWidth * bottomUp.chord->mag + std::max(maxDownWidth - maxUpWidth, 0.0) + padding;
                        }
                    } else {
                        downDots = 0;
                    }
                    upOffset = std::max(clearLeft, clearRight);
                    const N topUp = upNotes.back();
                    const int pad = st.boolean(Sid::useStraightNoteFlags) ? 2 : 1;
                    const bool overlapsFlag = topDown.line() + topDownStemLen + pad > topUp.line();
                    if (downHooks && (ledgerBelow || overlapsFlag)) {
                        const double hw = hookWidth(topDown.chord);
                        if (ledgerBelow) {
                            upOffset = hw + ledgerLen + ledgerGap;
                        }
                        upOffset = std::max(upOffset, maxDownWidth + 0.1 * sp);
                        dotAdjustThreshold = maxUpWidth - 0.3 * sp;
                    }
                    if (centerDown > 0.0) {
                        centerDown = 0.0;
                        centerAdjustUp = 0.0;
                        dotAdjustThreshold = (upOffset - maxDownWidth) + maxUpWidth - 0.3 * sp;
                    }
                }
            }
            // dots of one direction only go between the chords
            if ((upDots && !downDots) || (downDots && !upDots)) {
                const int dots = upDots ? upDots : downDots;
                const double mag = upDots ? maxUpMag : maxDownMag;
                dotAdjust = st.value(Sid::dotNoteDistance) + font.width(SymId::augmentationDot) / unit;
                if (dots > 1) {
                    dotAdjust += st.value(Sid::dotDotDistance) * (dots - 1);
                }
                dotAdjust *= mag;
                dotAdjust = std::max(dotAdjust - dotAdjustThreshold, 0.0);
            }
            if (separation == 1) {
                dotAdjust += 0.1 * sp;
            }
        }

        // applyChordOffsets
        for (ChordLayout* c : list) {
            double x = 0.0;
            const int track = c->staff * 4 + c->voice;
            if (!combines(*c) && !tracksToAdjust.count(track)) {
                x = c->up ? centerUp : centerDown;
            } else if (c->up) {
                if (upOffset != 0.0) {
                    x = upOffset + centerAdjustUp + oversizeUp;
                    if (downDots && !upDots) {
                        x += dotAdjust;
                    }
                } else {
                    x = centerUp;
                }
            } else {
                if (downOffset != 0.0) {
                    x = downOffset + centerAdjustDown;
                    if (upDots && !downDots) {
                        x += dotAdjust;
                    }
                } else {
                    x = centerDown;
                }
            }
            c->x = x / staffMag;
            const auto user = userOffset(c->chord);
            if (!user.first.empty()) {
                c->x += std::atof(user.first.c_str());
            }
            c->xKnown = true;
        }
    }

    //! A chord's own <offset x="…" y="…"/> (x and y as written; empty when it has none)
    std::pair<std::string, std::string> userOffset(Id id) const
    {
        const Object* o = ctx.obj(id);
        for (const std::string* d : { &o->text(F::DETAILS), &o->text(F::DETAILS_AFTER) }) {
            size_t b = 0, e = 0;
            std::vector<model::fragment::Child> kids;
            if (!model::fragment::children(*d, b, e, kids)) {
                continue;
            }
            for (const model::fragment::Child& k : kids) {
                if (k.name != "offset") {
                    continue;
                }
                const std::string tag = d->substr(k.begin, d->find('>', k.begin) - k.begin);
                auto attr = [&](const char* name) {
                    const std::string key = std::string(" ") + name + "=\"";
                    const size_t a = tag.find(key);
                    if (a == std::string::npos) {
                        return std::string("0");
                    }
                    const size_t z = tag.find('"', a + key.size());
                    return tag.substr(a + key.size(), z - a - key.size());
                };
                return { attr("x"), attr("y") };
            }
        }
        return {};
    }

    void run(std::vector<ChordLayout>& chords) const
    {
        std::map<std::pair<Fraction, int>, std::vector<ChordLayout*> > segments;
        std::map<ChordLayout*, bool> graceBefore;
        bool pending = false;
        for (ChordLayout& c : chords) {
            if (c.kind == 'G') {
                pending = true;
            } else if (c.kind == 'C') {
                graceBefore[&c] = pending;
                pending = false;
                segments[{ c.tick, c.shownStaff }].push_back(&c);
            }
        }
        for (auto& [key, list] : segments) {
            std::stable_sort(list.begin(), list.end(), [](const ChordLayout* a, const ChordLayout* b) {
                return a->staff * 4 + a->voice < b->staff * 4 + b->voice;
            });
            segment(list, graceBefore);
        }
    }
};
}

void layoutChordOffsets(const ScoreContext& ctx, std::vector<ChordLayout>& chords)
{
    Offsets(ctx).run(chords);
}
}

namespace je::layout {
namespace {
struct Dots {
    const ScoreContext& ctx;
    const style::Style& st;
    const style::MusicFont& font;
    const double unit;
    std::map<std::tuple<Fraction, int, int>, ChordLayout*> mainAt;   // (time, staff, voice)

    explicit Dots(const ScoreContext& c)
        : ctx(c), st(c.style()), font(c.font()), unit(style::Style::def(Sid::spatium).a)
    {
    }

    struct N {
        NoteLayout* note;
        ChordLayout* chord;
        int dotPosition = 0;   // MuseScore's DirectionV: 0 auto, 1 up, 2 down
    };

    bool combines(const ChordLayout& c) const
    {
        const std::string v = valueOf(ctx.obj(c.chord), "combineVoice");
        return v == "on" || v == "1" || ((v.empty() || v == "auto") && st.boolean(Sid::combineVoice));
    }
    bool visible(const NoteLayout& n) const
    {
        const std::string v = valueOf(ctx.obj(n.note), "visible");
        return !(v == "0" || v == "false");
    }
    bool small(const Object* o) const
    {
        const std::string v = valueOf(o, "small");
        return v == "1" || v == "true";
    }

    //! Whether voices 2–4 of the staff have anything at all in [t, t + len) of the measure (hasVoices, invisible too)
    bool anyOtherVoice(int staff, const Fraction& t, const Fraction& len) const
    {
        const auto& ms = ctx.measures();
        auto it = std::upper_bound(ms.begin(), ms.end(), t, [](const Fraction& x, const MeasureInfo& m) { return x < m.start; });
        if (it == ms.begin()) {
            return false;
        }
        const MeasureInfo& m = *std::prev(it);
        const StaffInfo& si = ctx.staves()[size_t(staff)];
        for (int vn = 1; vn < 4; ++vn) {
            const Object* voice = ctx.obj(si.voices[size_t(vn)]);
            if (!voice) {
                continue;
            }
            for (Id e : voice->children()) {
                const Object* ev = ctx.obj(e);
                if (!ev || (ev->kind() != Kind::Chord && ev->kind() != Kind::Rest) || ev->has(F::GRACE)) {
                    continue;
                }
                const Fraction et = ev->fractionOr(F::TIME, Fraction());
                if (et < m.start || et >= m.end() || et >= t + len) {
                    continue;
                }
                if (et + ev->fractionOr(F::DURATION, Fraction()) > t) {
                    return true;
                }
            }
        }
        return false;
    }

    //! getNoteListForDots: the notes on lines whose dots go up (top down) or down (bottom up), and the dots' fixed spaces
    void noteLists(ChordLayout& c, std::vector<NoteLayout*>& topDown, std::vector<NoteLayout*>& bottomUp, std::vector<int>& anchored) const
    {
        const Object* co = ctx.obj(c.chord);
        const int vStaff = c.shownStaff;
        const bool voices = anyOtherVoice(vStaff, c.tick, co->fractionOr(F::DURATION, Fraction()));
        const StaffInfo& si = ctx.staves()[size_t(c.staff)];
        const int partTop = si.partStaves.empty() ? c.staff : si.partStaves.front();
        const int partBottom = si.partStaves.empty() ? c.staff + 1 : si.partStaves.back() + 1;   // endTrack / VOICES
        int startTrack = c.staff * 4;
        int lastTrack = std::min(vStaff + 2, partBottom) * 4;
        bool upperCross = false, lowerCross = false;
        auto chordAt = [&](int track) -> ChordLayout* {
            auto it = mainAt.find({ c.tick, track / 4, track % 4 });
            return it == mainAt.end() ? nullptr : it->second;
        };
        if (partTop != vStaff) {
            for (int i = partTop * 4; i < (partTop + 1) * 4; ++i) {
                if (ChordLayout* vc = chordAt(i); vc && vc->shownStaff == vStaff) {
                    upperCross = true;
                    startTrack = i;
                    break;
                }
            }
        }
        if (partBottom != vStaff) {
            for (int i = (vStaff + 1) * 4; i < lastTrack; ++i) {
                if (ChordLayout* vc = chordAt(i); vc && vc->shownStaff == vStaff) {
                    lowerCross = true;
                    break;
                }
            }
        }
        if (!voices && !(upperCross || lowerCross)) {
            for (NoteLayout& n : c.notes) {
                if (n.line & 1) {
                    int offset = 0;
                    const bool adjustDown = (c.voice & 1) && !c.up;
                    if (!anchored.empty() && anchored.back() == n.line) {
                        if (anchored.size() >= 2 && anchored[anchored.size() - 2] == n.line + (adjustDown ? 2 : -2)) {
                            offset = adjustDown ? -2 : 2;
                        } else {
                            offset = adjustDown ? 2 : -2;
                        }
                    }
                    anchored.push_back(n.line + offset);
                } else {
                    topDown.push_back(&n);
                }
            }
        } else {
            lastTrack = lowerCross ? lastTrack : (vStaff + 1) * 4;
            for (int i = startTrack; i < lastTrack; ++i) {
                ChordLayout* vc = chordAt(i);
                if (!vc || vc->shownStaff != vStaff) {
                    continue;
                }
                const int move = int(ctx.obj(vc->chord)->intOr(F::STAFF_MOVE, 0));
                const bool fromTop = !((vc->voice & 1) && !vc->up) && move != -1;
                for (NoteLayout& n : vc->notes) {
                    if (n.line & 1) {
                        anchored.push_back(n.line);
                    } else {
                        (fromTop ? topDown : bottomUp).push_back(&n);
                    }
                }
            }
        }
        std::sort(topDown.begin(), topDown.end(), [](const NoteLayout* a, const NoteLayout* b) { return a->line < b->line; });
        std::sort(bottomUp.begin(), bottomUp.end(), [](const NoteLayout* a, const NoteLayout* b) { return a->line > b->line; });
    }

    void segment(std::vector<ChordLayout*>& list) const
    {
        for (ChordLayout* c : list) {
            for (NoteLayout& n : c->notes) {
                n.dotKnown = false;
            }
            if (!c->xKnown) {
                return;
            }
            for (const NoteLayout& n : c->notes) {
                if (valueOf(ctx.obj(n.note), "NoteDot").find("<visible>0") != std::string::npos) {
                    return;   // invisible dots are laid out apart (not yet)
                }
            }
        }
        const int staff = list.front()->shownStaff;
        const double staffMag = ctx.staffMag(staff, list.front()->tick);
        const StaffTypeInfo& type = ctx.staffType(staff, list.front()->tick);
        // the notes as applyChordOffsets passes them: stems up, then down, each sorted when it has several voices
        std::vector<N> up, down;
        int upVoices = 0, downVoices = 0;
        for (ChordLayout* c : list) {
            ++(c->up ? upVoices : downVoices);
            for (NoteLayout& n : c->notes) {
                (c->up ? up : down).push_back({ &n, c });
            }
        }
        auto byLine = [](const N& a, const N& b) { return a.note->line > b.note->line; };
        if (upVoices > 1) {
            std::sort(up.begin(), up.end(), byLine);
        }
        if (downVoices > 1) {
            std::sort(down.begin(), down.end(), byLine);
        }
        std::vector<N> notes = up;
        notes.insert(notes.end(), down.begin(), down.end());
        if (upVoices + downVoices > 1) {
            std::sort(notes.begin(), notes.end(), byLine);
        }
        // layoutChords3: each note's dot direction
        const int nNotes = int(notes.size());
        for (int i = nNotes - 1; i >= 0; --i) {
            N& n = notes[size_t(i)];
            const std::string user = valueOf(ctx.obj(n.note->note), "dotPosition");
            int pos = user == "up" ? 1 : user == "down" ? 2 : 0;
            if (n.chord->dots && pos == 0 && nNotes > 1 && visible(*n.note)) {
                const int line = n.note->line;
                N* above = i < nNotes - 1 ? &notes[size_t(i + 1)] : nullptr;
                if (above && (!visible(*above->note) || above->chord->dots == 0 || !(combines(*n.chord) && combines(*above->chord)))) {
                    above = nullptr;
                }
                const int intervalAbove = above ? line - above->note->line : 1000;
                N* below = i > 0 ? &notes[size_t(i - 1)] : nullptr;
                if (below && (!visible(*below->note) || below->chord->dots == 0 || !(combines(*n.chord) && combines(*below->chord)))) {
                    below = nullptr;
                }
                const int intervalBelow = below ? below->note->line - line : 1000;
                if ((line & 1) == 0) {
                    if (intervalAbove == 1 && intervalBelow != 1) {
                        pos = 2;
                    } else if (intervalBelow == 1 && intervalAbove != 1) {
                        pos = 1;
                    } else if (intervalAbove == 0 || intervalBelow == 0) {
                        pos = 0;
                    }
                } else if (intervalAbove == 0 && above->chord->dots) {
                    if (!(n.chord->voice & 1)) {
                        pos = 1;
                    } else if (!(above->chord->voice & 1)) {
                        above->dotPosition = 1;
                    } else {
                        pos = 2;
                    }
                }
            }
            if (pos == 0) {
                pos = (n.chord->voice & 1) ? 2 : 1;
            }
            n.dotPosition = pos;
        }
        std::map<const NoteLayout*, int> dotPos;
        for (const N& n : notes) {
            dotPos[n.note] = n.dotPosition;
        }
        // placeDots
        ChordLayout* dotted = nullptr;
        for (ChordLayout* c : list) {
            if (c->dots > 0) {
                dotted = c;
                break;
            }
            for (NoteLayout& n : c->notes) {
                n.dotLine = 0;
            }
        }
        if (dotted) {
            std::vector<NoteLayout*> topDown, bottomUp;
            std::vector<int> anchored;
            noteLists(*dotted, topDown, bottomUp, anchored);
            for (N& n : notes) {
                NoteLayout& note = *n.note;
                if (!(note.line & 1)) {
                    std::map<int, NoteLayout*> added;
                    bool finished = false;
                    for (NoteLayout* other : bottomUp) {
                        if (visible(*other) != visible(note)) {
                            continue;
                        }
                        int move = dotPos[other] == 1 ? -1 : 1;
                        const int loc = other->line + move;
                        const bool was = added.count(loc);
                        if (!was && std::find(anchored.begin(), anchored.end(), loc) != anchored.end()) {
                            move = -move;
                        } else if (was && added[loc] != other) {
                            move = -move;
                        }
                        if (&note == other) {
                            note.dotLine = move;
                            finished = true;
                            anchored.push_back(note.line + move);
                            added[other->line + move] = other;
                            break;
                        }
                    }
                    if (!finished) {
                        added.clear();
                        for (NoteLayout* other : topDown) {
                            if (visible(*other) != visible(note)) {
                                continue;
                            }
                            int move = dotPos[other] == 2 ? 1 : -1;
                            const int loc = other->line + move;
                            const bool was = added.count(loc);
                            if (!was && std::find(anchored.begin(), anchored.end(), loc) != anchored.end()) {
                                move = -move;
                            } else if (was && added[loc] != other) {
                                move = -move;
                            }
                            if (&note == other) {
                                note.dotLine = move;
                                finished = true;
                                anchored.push_back(note.line + move);
                                break;
                            }
                            if (!was) {
                                added[other->line + move] = other;
                            }
                        }
                    }
                    if (!finished) {
                        note.dotLine = 0;
                    }
                } else {
                    for (const NoteLayout& other : n.chord->notes) {
                        if (&note == &other) {
                            note.dotLine = 0;
                            break;
                        }
                        if (note.line == other.line) {
                            const bool adjustDown = (n.chord->voice & 1) && !n.chord->up;
                            note.dotLine = adjustDown ? 2 : -2;
                            break;
                        }
                    }
                }
            }
        }
        // the chords' dot positions (layoutChords3), in the score's spaces
        double upDotPosX = 0.0, downDotPosX = 0.0;
        std::map<int, double> trackDotPos;   // MuseScore's dotPos, by staff and voice
        std::map<ChordLayout*, double> own;
        for (ChordLayout* c : list) {
            double minDotPosX = 0.0;
            for (const NoteLayout& n : c->notes) {
                const double m = small(ctx.obj(n.note)) ? c->mag * st.value(Sid::smallNoteMag) : c->mag;
                const style::RectF b = font.bbox(c->up ? n.headUp : n.headDown, m);
                const double body = (b.width() + 2 * b.left()) / unit;
                const double dotX = n.x * staffMag + body + c->x * staffMag;
                if (c->dots) {
                    const int idx = (c->staff - staff + 1) * 4 + c->voice;
                    trackDotPos[idx] = std::max(trackDotPos[idx], dotX);
                }
                minDotPosX = std::max(minDotPosX, dotX);
            }
            own[c] = minDotPosX;
            if (c->dots) {
                (c->up ? upDotPosX : downDotPosX) = std::max(c->up ? upDotPosX : downDotPosX, minDotPosX);
            }
        }
        // setDotX
        std::set<int> combineConflicts, separateConflicts;
        auto track = [](const ChordLayout* c) { return c->staff * 4 + c->voice; };
        for (ChordLayout* a : list) {
            for (ChordLayout* b : list) {
                if (a == b) {
                    continue;
                }
                const int aUp = a->notes.back().line, aDown = a->notes.front().line;   // upNote, downNote lines
                const int bUp = b->notes.back().line, bDown = b->notes.front().line;
                const bool both = combines(*a) && combines(*b);
                if (((a->up && !b->up && bUp - aDown < 2) || (!a->up && b->up && aUp - bDown < 2)) && both) {
                    combineConflicts.insert(track(a));
                    combineConflicts.insert(track(b));
                } else if (((a->up && b->up && aUp - bDown < 2) || (!a->up && !b->up && aUp - bDown < 2)) && !both) {
                    separateConflicts.insert(track(a));
                    separateConflicts.insert(track(b));
                }
            }
        }
        const double maxPosX = std::max(upDotPosX, downDotPosX);
        std::map<ChordLayout*, double> dotPosX;
        for (ChordLayout* c : list) {
            const int idx = (c->staff - staff + 1) * 4 + c->voice;
            const double mine = trackDotPos.count(idx) ? trackDotPos[idx] : 0.0;
            double x;
            if (!combineConflicts.empty()) {
                x = combineConflicts.count(track(c)) ? maxPosX : (combines(*c) ? maxPosX : mine);
            } else if (combines(*c)) {
                x = separateConflicts.count(track(c)) ? mine : (c->up ? upDotPosX : downDotPosX);
            } else {
                x = mine;
            }
            dotPosX[c] = x;
        }
        // layoutNote2: each dot from its note
        const double sp = staffMag;
        for (ChordLayout* c : list) {
            if (!c->dots) {
                continue;
            }
            const double chordX = c->x * staffMag;
            double hookRight = 0.0, hookBottom = 0.0;
            const bool hookCheck = c->up && c->hook;
            if (hookCheck) {
                if (!c->stemLengthKnown) {
                    continue;   // the flag's place isn't known
                }
                const style::RectF hb = font.bbox(c->hookSym, c->mag);
                const double hookX = c->stemX * staffMag - st.value(Sid::stemWidth) * c->mag;
                const NoteLayout& start = c->notes.front();
                const double startY = (start.line + type.stepOffset) * 0.5 * type.lineDistance * staffMag;
                const double hookY = startY - c->stemLength * staffMag;
                hookRight = hb.width() / unit + hookX + chordX;
                hookBottom = hb.height() / unit + hookY + 0.25 * sp;
            }
            for (NoteLayout& n : c->notes) {
                const bool many = c->notes.size() > 1;
                const double nm = small(ctx.obj(n.note)) ? c->mag * st.value(Sid::smallNoteMag) : c->mag;
                const double correctMag = many ? c->mag : nm;
                double d = st.value(Sid::dotNoteDistance) * correctMag;
                double x = dotPosX[c] - n.x * staffMag - chordX;
                if (nm != c->mag && many) {
                    const double rel = nm / c->mag;
                    x += font.width(SymId::augmentationDot, nm) / unit * (1 / rel - 1) / 2;
                }
                if (hookCheck) {
                    const NoteLayout& top = c->notes.back();
                    const double dotY = top.y * staffMag + top.dotLine / 2.0 * sp * type.lineDistance;
                    if (dotPosX[c] < hookRight && dotY < hookBottom) {
                        d = font.width(c->hookSym, c->mag) / unit;
                    }
                }
                n.dotX = (x + d) / staffMag;
                n.dotKnown = true;
            }
        }
    }

    void run(std::vector<ChordLayout>& chords)
    {
        std::map<std::pair<Fraction, int>, std::vector<ChordLayout*> > segments;
        for (ChordLayout& c : chords) {
            if (c.kind == 'C') {
                segments[{ c.tick, c.shownStaff }].push_back(&c);
                mainAt[{ c.tick, c.staff, c.voice }] = &c;
            }
        }
        for (auto& [key, list] : segments) {
            std::stable_sort(list.begin(), list.end(), [](const ChordLayout* a, const ChordLayout* b) {
                return a->staff * 4 + a->voice < b->staff * 4 + b->voice;
            });
            segment(list);
        }
    }
};
}

void layoutDots(const ScoreContext& ctx, std::vector<ChordLayout>& chords)
{
    Dots(ctx).run(chords);
}
}
