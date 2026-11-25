/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * The fretting rules are ported from MuseScore Studio's StringData (src/engraving/dom/stringdata.cpp,
 * Copyright (C) MuseScore Limited and others, GPL v3).
 */
#include "tablature.h"

#include <climits>
#include <map>
#include <set>
#include <sstream>

#include "model/fields.h"

namespace F = je::model::fields;
using je::Fraction;
using je::model::Kind;
using je::model::Object;

namespace je::edit {
StringTuning StringTuning::of(const Object& player)
{
    StringTuning t;
    std::istringstream in(player.text(F::STRING_TUNING));
    for (std::string w; in >> w;) {
        String s;
        s.open = !w.empty() && w.back() == '*';
        s.pitch = std::atoi(w.c_str());
        t.strings.push_back(s);
    }
    t.frets = int(player.intOr(F::FRETS, 24));
    if (t.fiveStringBanjo()) {   // configBanjo5thString
        t.frets = 24;
        String& fifth = t.strings[0];
        fifth.startFret = (fifth.pitch > 67 && fifth.pitch < 74) ? fifth.pitch - 62 : 5;
    }
    return t;
}

bool StringTuning::convertPitch(int pitch, int offset, int& string, int& fret) const
{
    const int n = int(strings.size());
    if (n < 1) {
        return false;
    }
    pitch += offset;
    if (pitch > strings[size_t(n - 1)].pitch + frets) {   // above the highest string's last fret
        string = 0;
        fret = 0;
        return false;
    }
    if (fiveStringBanjo()) {
        if (pitch == strings[0].pitch) {   // the open drone string
            string = 4;
            fret = 0;
            return true;
        }
        for (int i = 4; i > 0; --i) {
            if (pitch >= strings[size_t(i)].pitch) {
                string = n - i - 1;
                fret = pitch - strings[size_t(i)].pitch;
                return true;
            }
        }
    } else {
        for (int i = n - 1; i >= 0; --i) {   // the highest string that reaches it
            if (pitch >= strings[size_t(i)].pitch) {
                string = n - i - 1;
                fret = pitch - strings[size_t(i)].pitch;
                return true;
            }
        }
    }
    string = n - 1;   // below the lowest string
    fret = 0;
    return false;
}

int StringTuning::fretFor(int pitch, int string, int offset) const
{
    const int n = int(strings.size());
    if (n < 1 || string < 0 || string >= n) {
        return -1;
    }
    pitch += offset;
    const String& s = strings[size_t(n - string - 1)];
    int fret = pitch - s.pitch;
    if (fret > 0 && s.startFret > 0) {
        fret += s.startFret;
    }
    if (fret < 0 || fret > frets || (fret > 0 && s.open)) {
        return -1;
    }
    return fret;
}

int StringTuning::pitchOf(int string, int fret, int offset) const
{
    const int n = int(strings.size());
    if (string < 0 || string >= n) {
        return -1;
    }
    const String& s = strings[size_t(n - string - 1)];
    int pitch = s.pitch - offset + (s.open ? 0 : fret);
    if (s.startFret > 0 && fret >= s.startFret) {
        pitch -= s.startFret;
    }
    return pitch;
}

bool isTablature(const Object& staff)
{
    return staff.kind() == Kind::Staff && staff.text(F::SUBKIND) == "tablature";
}

namespace {
const Object* playerOfStaff(const model::Model& m, je::Id staff)
{
    const Object* s = m.find(staff);
    return s ? m.find(s->parent()) : nullptr;
}

//! StringData::fretChords for one group of chords (a segment's chords on the staff, or one grace chord)
void fretGroup(Transaction& tx, const model::Model& m, const StringTuning& tuning, int offset, const std::vector<je::Id>& chords,
               std::vector<je::Id>& conflicts)
{
    const int strings = int(tuning.strings.size());
    struct N {
        je::Id id;
        int pitch;
        int string;
        int fret;
    };
    // sorted by string (unfretted first), then from the highest pitch (sortChordNotes)
    std::map<int64_t, N> sorted;
    int count = 0;
    for (je::Id c : chords) {
        const Object* chord = m.find(c);
        for (je::Id nid : chord->children()) {
            const Object* note = m.find(nid);
            if (note->kind() != Kind::Note) {
                continue;
            }
            N n { nid, int(note->intOr(F::PITCH, 60)), int(note->intOr(F::STRING, -1)), int(note->intOr(F::FRET, -1)) };
            const int p = tuning.pitchOf(n.string, n.fret, offset);
            if (n.string <= -1 || n.fret <= -1 || (p >= 0 && p <= 127 && p != n.pitch)) {
                n.string = -1;   // not fretted yet, or no longer valid
                n.fret = -1;
            }
            const int64_t key = int64_t(n.string) * 100000 - int64_t(n.pitch + offset) * 100 + count;
            sorted.emplace(key, n);
            ++count;
        }
    }
    for (auto& [k, n] : sorted) {
        (void)k;
        if (n.string >= strings) {
            n.string = -1;
            n.fret = -1;
        }
    }
    std::vector<je::Id> conflict;
    if (strings > 0) {
        std::vector<int> used(size_t(strings), 0);
        for (const auto& [k, n] : sorted) {
            (void)k;
            if (n.string >= 0) {
                used[size_t(n.string)]++;
            }
        }
        for (auto& [k, n] : sorted) {
            (void)k;
            int s = n.string;
            int f = n.fret;
            if (s == -1) {
                if (tuning.convertPitch(n.pitch, offset, s, f)) {
                    f = tuning.fretFor(n.pitch, s, offset);
                    used[size_t(s)]++;
                } else {
                    n.string = s;   // where MuseScore shows a note it can't fret
                    n.fret = f;
                    conflict.push_back(n.id);
                    continue;
                }
            }
            if (used[size_t(s)] > 1) {   // taken: the first free string from the top that can play it
                for (int t = 0; t < strings; ++t) {
                    int tf = -1;
                    if (used[size_t(t)] < 1 && (tf = tuning.fretFor(n.pitch, t, offset)) != -1) {
                        used[size_t(s)]--;
                        used[size_t(t)]++;
                        s = t;
                        f = tf;
                        break;
                    }
                }
            }
            n.string = s;
            n.fret = f;
        }
        for (const auto& [k, n] : sorted) {
            (void)k;
            if (n.string == -1 || used[size_t(n.string)] > 1) {
                conflict.push_back(n.id);
            }
        }
    }
    for (const auto& [k, n] : sorted) {
        (void)k;
        // read before setting: a changed object is a new version (the old one may be gone)
        const Object* note = m.find(n.id);
        const bool hasString = note->has(F::STRING), hasFret = note->has(F::FRET);
        const int64_t string = note->intOr(F::STRING, -1), fret = note->intOr(F::FRET, -1);
        if (n.string < 0) {
            if (hasString) {
                tx.set(n.id, F::STRING, model::Value());
            }
            if (hasFret) {
                tx.set(n.id, F::FRET, model::Value());
            }
            continue;
        }
        if (string != n.string || !hasString) {
            tx.set(n.id, F::STRING, int64_t(n.string));
        }
        if (fret != n.fret || !hasFret) {
            tx.set(n.id, F::FRET, int64_t(n.fret));
        }
    }
    std::set<je::Id> seen;
    for (je::Id c : conflict) {
        if (seen.insert(c).second) {
            conflicts.push_back(c);
        }
    }
}

std::vector<je::Id> voicesOf(const model::Model& m, je::Id staff)
{
    std::vector<je::Id> out;
    if (const Object* s = m.find(staff)) {
        for (je::Id v : s->children()) {
            if (m.find(v)->kind() == Kind::Voice) {
                out.push_back(v);
            }
        }
    }
    return out;
}
}

std::vector<je::Id> fretNotes(Transaction& tx, const model::Model& m, je::Id staff, const Fraction& t)
{
    std::vector<je::Id> conflicts;
    const Object* st = m.find(staff);
    const Object* player = playerOfStaff(m, staff);
    if (!st || !player || !isTablature(*st)) {
        return conflicts;
    }
    const StringTuning tuning = StringTuning::of(*player);
    const int offset = -int(player->intOr(F::TRANSPOSE_CHROMATIC, 0));
    std::vector<je::Id> segment, graces;
    for (je::Id v : voicesOf(m, staff)) {
        for (je::Id e : m.find(v)->children()) {
            const Object* o = m.find(e);
            if (o->kind() != Kind::Chord || o->fractionOr(F::TIME, Fraction()) != t) {
                continue;
            }
            (o->has(F::GRACE) ? graces : segment).push_back(e);
        }
    }
    for (je::Id g : graces) {
        fretGroup(tx, m, tuning, offset, { g }, conflicts);   // a grace chord by itself
    }
    if (!segment.empty()) {
        fretGroup(tx, m, tuning, offset, segment, conflicts);
    }
    return conflicts;
}

std::vector<je::Id> fretStaff(Transaction& tx, const model::Model& m, je::Id staff)
{
    std::vector<je::Id> conflicts;
    const Object* st = m.find(staff);
    const Object* player = playerOfStaff(m, staff);
    if (!st || !player || !isTablature(*st)) {
        return conflicts;
    }
    std::set<Fraction> times;
    for (je::Id v : voicesOf(m, staff)) {
        for (je::Id e : m.find(v)->children()) {
            const Object* o = m.find(e);
            if (o->kind() != Kind::Chord) {
                continue;
            }
            for (je::Id nid : o->children()) {
                const Object* n = m.find(nid);
                if (n->kind() != Kind::Note) {
                    continue;
                }
                if (!n->has(F::STRING) || !n->has(F::FRET)) {
                    times.insert(o->fractionOr(F::TIME, Fraction()));
                    break;
                }
            }
        }
    }
    for (const Fraction& t : times) {
        std::vector<je::Id> c = fretNotes(tx, m, staff, t);
        conflicts.insert(conflicts.end(), c.begin(), c.end());
    }
    return conflicts;
}
}
