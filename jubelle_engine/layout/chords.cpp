/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::layout). Not part of MuseScore.
 *
 * Ported from MuseScore's ChordLayout::layoutChords1, calculateChordPosInfo, calculateMaxNoteWidths and layoutChords2
 * (rendering/score/chordlayout.cpp) and Chord::combineVoice (dom/chord.cpp) (GPL-3.0).
 */
#include "chords.h"

#include <algorithm>
#include <cstdlib>
#include <map>

#include "model/fields.h"
#include "model/fragment.h"

namespace je::layout {
namespace F = model::fields;
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
