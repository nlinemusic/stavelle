/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "harmonyrules.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace mu::notation::jubelle::harmony {
namespace {
int mod12(int v)
{
    return ((v % 12) + 12) % 12;
}

// MuseScore tonal pitch classes of the natural notes
int letterTpc(char letter)
{
    switch (letter) {
    case 'F': return 13;
    case 'C': return 14;
    case 'G': return 15;
    case 'D': return 16;
    case 'A': return 17;
    case 'E': return 18;
    case 'B': return 19;
    default: break;
    }
    return -1;
}

std::string lower(std::string s)
{
    for (char& c : s) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool has(const std::string& s, const char* what)
{
    return s.find(what) != std::string::npos;
}

//! The interval that plays the 3rd's role (the 4th for a sus chord)
int thirdOf(const ChordSymbol& c)
{
    if (c.sus) {
        return 5;
    }
    switch (c.family) {
    case Family::Minor:
    case Family::Diminished:
    case Family::HalfDiminished:
        return 3;
    case Family::Major:
    case Family::Dominant:
        break;
    }
    return 4;
}

//! How much a chord tone is worth in a four-voice closed voicing: the 3rd and the 7th (or 6th) say what
//! the chord is; the colours follow; the 5th and root are the first to go (§1.2)
int toneWeight(int interval, const ChordSymbol& c)
{
    if (interval == thirdOf(c)) {
        return 100;
    }
    // an alteration written or chosen must be heard
    if ((c.alterations & Flat9) && interval == 1) {
        return 45;
    }
    if ((c.alterations & Sharp9) && interval == 3 && c.family == Family::Dominant) {
        return 45;
    }
    if ((c.alterations & Flat5) && interval == 6) {
        return 45;
    }
    if ((c.alterations & Sharp5) && interval == 8) {
        return 45;
    }
    switch (c.family) {
    case Family::Dominant:
        if (interval == 10) {
            return 60;
        }
        if (interval == 2 || interval == 9) {
            return 20;
        }
        if (interval == 7) {
            return 10;
        }
        break;
    case Family::Major:
        if (interval == 9 || interval == 11) {
            return 50;
        }
        if (interval == 2) {
            return 20;
        }
        if (interval == 7) {
            return 10;
        }
        break;
    case Family::Minor:
        if (interval == 10) {
            return 50;
        }
        // the lesson's own preference under a melody on the root: the 11th over the 6th (§1.2)
        if (interval == 5) {
            return 30;
        }
        if (interval == 9 || interval == 2) {
            return 20;
        }
        if (interval == 7) {
            return 10;
        }
        break;
    case Family::HalfDiminished:
        if (interval == 6) {
            return 60;
        }
        if (interval == 10) {
            return 50;
        }
        if (interval == 5) {
            return 20;
        }
        break;
    case Family::Diminished:
        return 40;
    }
    return 0;
}
}

int tpcToPc(int tpc)
{
    // C = 14 → 0; each step of the line of fifths is 7 semitones
    return mod12((tpc - 14) * 7);
}

int simplerSpelling(int tpc)
{
    // keeps G♭ … A♯ (8 … 24): no B♯, C♭, E♯, F♭ and no double accidentals (§1.10)
    while (tpc < 8) {
        tpc += 12;
    }
    while (tpc > 24) {
        tpc -= 12;
    }
    return tpc;
}

// ---------------------------------------------------------------------------
// Chord symbols
// ---------------------------------------------------------------------------

ChordSymbol parseChordSymbol(const std::string& raw)
{
    ChordSymbol c;
    c.text = raw;
    size_t i = 0;
    while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) {
        ++i;
    }
    if (i >= raw.size()) {
        return c;
    }
    const char letter = char(std::toupper(static_cast<unsigned char>(raw[i])));
    int tpc = letterTpc(letter);
    if (tpc < 0) {
        return c;
    }
    ++i;
    // accidentals: # b ♯ ♭
    while (i < raw.size()) {
        if (raw[i] == '#') {
            tpc += 7;
            ++i;
        } else if (raw[i] == 'b' && !(i + 1 < raw.size() && std::isdigit(static_cast<unsigned char>(raw[i + 1])) && i > 1)) {
            tpc -= 7;
            ++i;
        } else if (raw.compare(i, 3, "\xE2\x99\xAF") == 0) {
            tpc += 7;
            i += 3;
        } else if (raw.compare(i, 3, "\xE2\x99\xAD") == 0) {
            tpc -= 7;
            i += 3;
        } else {
            break;
        }
    }
    std::string rest = raw.substr(i);
    // a slash bass: "/E" (not the "/9" of 6/9)
    const size_t slash = rest.rfind('/');
    if (slash != std::string::npos && slash + 1 < rest.size() && letterTpc(char(std::toupper(static_cast<unsigned char>(rest[slash + 1])))) >= 0
        && !std::isdigit(static_cast<unsigned char>(rest[slash + 1]))) {
        const ChordSymbol bass = parseChordSymbol(rest.substr(slash + 1));
        if (bass.valid) {
            c.bassPc = bass.rootPc;
        }
        rest = rest.substr(0, slash);
    }
    const std::string q = lower(rest);
    c.rootTpc = tpc;
    c.rootPc = tpcToPc(tpc);

    // a leading "-" is the minor sign, not a flat alteration
    const bool dashMinor = !rest.empty() && rest[0] == '-';
    const std::string alt = dashMinor ? q.substr(1) : q;
    const bool minorStart = dashMinor || (!q.empty() && q[0] == 'm' && q.compare(0, 2, "ma") != 0) || q.compare(0, 3, "min") == 0;
    const bool majorWord = has(rest, "maj") || has(rest, "Maj") || has(rest, "ma7") || has(rest, "M7") || has(rest, "M9")
                           || has(rest, "\xCE\x94") || has(rest, "^") || (!rest.empty() && rest[0] == 'M');
    if (has(q, "m7b5") || has(q, "m7(b5)") || has(rest, "\xC3\xB8") || has(q, "0")) {
        c.family = Family::HalfDiminished;
    } else if (has(q, "dim") || (!q.empty() && q[0] == 'o') || has(rest, "\xC2\xB0")) {
        c.family = Family::Diminished;
    } else if (minorStart) {
        c.family = Family::Minor;
    } else if (majorWord) {
        c.family = Family::Major;
    } else if (has(q, "sus")) {
        c.family = Family::Dominant;
        c.sus = true;
    } else if (has(q, "7") || has(q, "9") || has(q, "11") || has(q, "13") || has(q, "alt")) {
        // 6/9 and add9 are major colours, not dominants
        c.family = (has(q, "6") || has(q, "add")) && !has(q, "7") ? Family::Major : Family::Dominant;
    } else {
        c.family = Family::Major;
    }
    if (has(q, "sus")) {
        c.sus = c.family == Family::Dominant || c.family == Family::Major;
        if (c.sus) {
            c.family = Family::Dominant;
        }
    }
    // alterations written in the symbol
    if (has(alt, "b9") || has(alt, "-9")) {
        c.alterations |= Flat9;
    }
    if (has(alt, "#9") || has(alt, "+9")) {
        c.alterations |= Sharp9;
    }
    if (c.family != Family::HalfDiminished && (has(alt, "b5") || has(alt, "-5"))) {
        c.alterations |= Flat5;
    }
    if (has(alt, "#5") || has(alt, "+5") || has(alt, "b13") || has(alt, "aug") || (!alt.empty() && alt[0] == '+') || has(alt, "7+")) {
        c.alterations |= Sharp5;
    }
    if (has(alt, "alt")) {
        c.alterations |= Flat9 | Sharp9 | Sharp5;
    }
    c.valid = true;
    return c;
}

ChordSymbol chordFromIntervals(int rootTpc, unsigned mask, int bassPc)
{
    auto in = [mask](int iv) { return (mask >> iv) & 1u; };
    ChordSymbol c;
    if (rootTpc < -1 || !(mask & 1u)) {
        return c;
    }
    c.valid = true;
    c.rootTpc = rootTpc;
    c.rootPc = tpcToPc(rootTpc);
    c.bassPc = bassPc;
    const bool m3 = in(3);
    const bool M3 = in(4);
    const bool b7 = in(10);
    const bool M7 = in(11);
    if (m3 && !M3) {
        if (in(6) && !in(7)) {
            c.family = in(9) && !b7 ? Family::Diminished : Family::HalfDiminished;
        } else {
            c.family = Family::Minor;
        }
    } else if (M3 || !m3) {
        if (b7) {
            c.family = Family::Dominant;
            c.sus = !M3 && in(5);
        } else if (!M3 && in(5)) {
            c.family = Family::Dominant;   // sus chords behave as dominants (§1.7)
            c.sus = true;
        } else {
            c.family = Family::Major;
        }
        (void)M7;
    }
    if (c.family == Family::Dominant && !c.sus) {
        if (in(1)) {
            c.alterations |= Flat9;
        }
        if (m3 && M3) {
            c.alterations |= Sharp9;
        }
        if (!in(7) && in(6)) {
            c.alterations |= Flat5;
        }
        if (!in(7) && in(8)) {
            c.alterations |= Sharp5;
        }
    }
    if (c.family == Family::Major && in(8) && !in(7)) {
        c.alterations |= Sharp5;   // augmented
    }
    c.text = chordText(c);
    return c;
}

std::vector<int> colourTones(const ChordSymbol& c)
{
    std::vector<int> t;
    switch (c.family) {
    case Family::Major: t = { 0, 4, 7, 9, 11, 2 };
        break;
    case Family::Minor: t = { 0, 3, 7, 10, 9, 2, 5 };
        break;
    case Family::Dominant: t = { 0, 4, 7, 10, 2, 9 };
        break;
    case Family::Diminished: t = { 0, 3, 6, 9 };
        break;
    case Family::HalfDiminished: t = { 0, 3, 6, 10, 5 };
        break;
    }
    auto drop = [&t](int v) { t.erase(std::remove(t.begin(), t.end(), v), t.end()); };
    auto add = [&t](int v) {
        if (std::find(t.begin(), t.end(), v) == t.end()) {
            t.push_back(v);
        }
    };
    if (c.sus) {
        drop(4);
        add(5);
    }
    if (c.alterations & (Flat9 | Sharp9)) {
        drop(2);
        if (c.alterations & Flat9) {
            add(1);
        }
        if (c.alterations & Sharp9) {
            add(3);
        }
    }
    if (c.alterations & (Flat5 | Sharp5)) {
        drop(7);
        if (c.alterations & Flat5) {
            add(6);
        }
        if (c.alterations & Sharp5) {
            add(8);
            drop(9);   // the 13th and the ♭13 would rub
        }
    }
    return t;
}

bool isChordTone(int pitch, const ChordSymbol& chord)
{
    if (!chord.valid) {
        return false;
    }
    const std::vector<int> t = colourTones(chord);
    return std::find(t.begin(), t.end(), mod12(pitch - chord.rootPc)) != t.end();
}

std::string chordText(const ChordSymbol& c)
{
    static const char* names[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    std::string root;
    // the root's own spelling, without double accidentals
    const int tpc = simplerSpelling(c.rootTpc);
    static const char letters[] = { 'F', 'C', 'G', 'D', 'A', 'E', 'B' };
    const int idx = tpc - 13;
    const int letterIdx = ((idx % 7) + 7) % 7;
    const int acc = (idx - letterIdx) / 7;   // −1 flat, 0 natural, +1 sharp
    root = std::string(1, letters[letterIdx]) + (acc > 0 ? "#" : acc < 0 ? "b" : "");
    (void)names;
    std::string q;
    switch (c.family) {
    case Family::Major: q = "maj7";
        break;
    case Family::Minor: q = "m7";
        break;
    case Family::Dominant: q = c.sus ? "9sus" : "7";
        break;
    case Family::Diminished: q = "o7";
        break;
    case Family::HalfDiminished: q = "m7b5";
        break;
    }
    if (c.family == Family::Dominant && !c.sus) {
        if (c.alterations & Flat9) {
            q += "b9";
        }
        if (c.alterations & Sharp9) {
            q += "#9";
        }
        if (c.alterations & Flat5) {
            q += "b5";
        }
        if (c.alterations & Sharp5) {
            q += "#5";
        }
    }
    return root + q;
}

// ---------------------------------------------------------------------------
// Voicings
// ---------------------------------------------------------------------------

Voicing closedVoicing(int lead, const ChordSymbol& chord)
{
    Voicing best;
    if (!chord.valid) {
        best.reason = "no chord";
        return best;
    }
    const int mel = mod12(lead - chord.rootPc);
    if (chord.family == Family::Diminished) {
        // a diminished seventh closes itself: minor thirds down from the lead, every tone a chord tone (lesson 3)
        if (mel % 3 == 0) {
            return diminishedVoicing(lead);
        }
    }
    const int third = thirdOf(chord);
    const std::vector<int> tones = colourTones(chord);
    const bool melodyOnThird = mel == third;
    const bool melodyOnRoot = mel == 0;

    // the candidates under the lead: the chord's colours, never the melody's own pitch class (§1.2)
    std::vector<int> cand;
    for (int t : tones) {
        if (t == mel) {
            continue;
        }
        if (t == 0 && !melodyOnThird) {
            continue;   // leave out the root, unless the melody is on the 3rd
        }
        cand.push_back(t);
    }
    auto pitchFor = [&](int interval) {
        // the one pitch of this pitch class strictly inside the octave below the lead
        int p = lead - mod12(lead - (chord.rootPc + interval));
        if (p == lead) {
            p -= 12;
        }
        return p;
    };

    int bestScore = -1;
    int bestSum = -1;
    // the alterations the chord asks for, other than the melody itself: the horns must play them (lessons 5 and 7)
    std::vector<int> required;
    if (chord.family == Family::Dominant) {
        for (auto [flag, iv] : { std::pair<unsigned, int> { Flat9, 1 }, { Sharp9, 3 }, { Flat5, 6 }, { Sharp5, 8 } }) {
            if ((chord.alterations & flag) && iv != mel) {
                required.push_back(iv);
            }
        }
    }
    // pass 1: every rule; pass 2: without the second-voice preferences; pass 3: without the alterations.
    // The minor third under the lead is never given up.
    for (int pass = 0; pass < 3 && bestScore < 0; ++pass) {
        const bool strict = pass == 0;
        const bool needAlterations = pass < 2;
        for (size_t a = 0; a < cand.size(); ++a) {
            for (size_t b = a + 1; b < cand.size(); ++b) {
                for (size_t d = b + 1; d < cand.size(); ++d) {
                    std::array<std::pair<int, int>, 3> v { std::make_pair(pitchFor(cand[a]), cand[a]),
                                                           std::make_pair(pitchFor(cand[b]), cand[b]),
                                                           std::make_pair(pitchFor(cand[d]), cand[d]) };
                    std::sort(v.begin(), v.end(), [](auto x, auto y) { return x.first > y.first; });
                    // at least a minor third between the lead and the second voice
                    if (lead - v[0].first < 3) {
                        continue;
                    }
                    if (strict) {
                        // melody on the 3rd: the second voice takes the root, unless a ♭9 asks for the root to be
                        // left out (lesson 7)
                        if (melodyOnThird && v[0].second != 0 && !(chord.alterations & Flat9)) {
                            continue;   // melody on the 3rd: the second voice takes the root
                        }
                        if (melodyOnRoot) {
                            const bool sixth = v[0].second == 9;
                            const bool minorEleven = chord.family == Family::Minor && v[0].second == 7;   // Dm11, D–A–G–F
                            if ((chord.family == Family::Major || chord.family == Family::Dominant) && !sixth) {
                                continue;
                            }
                            if (chord.family == Family::Minor && !sixth && !minorEleven) {
                                continue;
                            }
                        }
                    }
                    // ♯9 above the 3rd; ♭9 never below a root in the voicing (§1.7)
                    int pSharp9 = -1;
                    int pThird = mel == 4 ? lead : -1;   // the lead counts too
                    int pFlat9 = -1;
                    int pRoot = mel == 0 ? lead : -1;
                    for (auto [p, iv] : v) {
                        if (iv == 3 && chord.family == Family::Dominant) {
                            pSharp9 = p;
                        }
                        if (iv == 4) {
                            pThird = p;
                        }
                        if (iv == 1) {
                            pFlat9 = p;
                        }
                        if (iv == 0) {
                            pRoot = p;
                        }
                    }
                    if (pSharp9 >= 0 && pThird >= 0 && pSharp9 < pThird) {
                        continue;
                    }
                    if (pFlat9 >= 0 && pRoot >= 0 && pFlat9 < pRoot) {
                        continue;
                    }
                    if (needAlterations) {
                        bool all = true;
                        for (int iv : required) {
                            all = all && (v[0].second == iv || v[1].second == iv || v[2].second == iv);
                        }
                        if (!all) {
                            continue;
                        }
                    }
                    // what the chord sounds like with the melody
                    std::set<int> present { mel };
                    for (auto [p, iv] : v) {
                        present.insert(iv);
                    }
                    int score = 0;
                    for (int iv : present) {
                        score += toneWeight(iv, chord);
                    }
                    // both the 6th and the maj7 (or 6th and ♭7) in one closed voicing say the same thing twice
                    if (chord.family == Family::Major && present.count(9) && present.count(11)) {
                        score -= 60;
                    }
                    // under a root melody on a minor chord the lesson names Dm11 (D–A–G–F) first, Dm6 second (§1.2)
                    if (strict && melodyOnRoot && chord.family == Family::Minor && v[0].second == 7) {
                        score += 15;
                    }
                    const int sum = v[0].first + v[1].first + v[2].first;   // closer voices first when equal
                    if (score > bestScore || (score == bestScore && sum > bestSum)) {
                        bestScore = score;
                        bestSum = sum;
                        best.ok = true;
                        best.pitches = { lead, v[0].first, v[1].first, v[2].first };
                    }
                }
            }
        }
    }
    if (!best.ok) {
        best.reason = "no closed voicing keeps a minor third under the lead";
    }
    return best;
}

Voicing diminishedVoicing(int lead)
{
    Voicing v;
    v.ok = true;
    v.pitches = { lead, lead - 3, lead - 6, lead - 9 };
    return v;
}

ChordSymbol diatonicPassingChord(const ChordSymbol& chord)
{
    ChordSymbol p;
    if (!chord.valid) {
        return p;
    }
    p.valid = true;
    p.rootPc = mod12(chord.rootPc + 7);
    p.rootTpc = chord.rootTpc + 1;
    switch (chord.family) {
    case Family::Major:
    case Family::Minor:
        p.family = Family::Dominant;   // its V7 (G7 on C, E7 on Am)
        break;
    case Family::Dominant:
        p.family = Family::Minor;      // its ii (Dm on G7)
        break;
    case Family::Diminished:
    case Family::HalfDiminished:
        p.valid = false;               // the lessons give no passing chord for these
        break;
    }
    return p;
}

Voicing planedVoicing(const Voicing& target, int leadStep)
{
    Voicing v = target;
    for (int& p : v.pitches) {
        p += leadStep;
    }
    return v;
}

unsigned allowedAlterations(int leadPitch, const ChordSymbol& dom)
{
    if (!dom.valid || dom.family != Family::Dominant || dom.sus) {
        return 0;
    }
    const int mel = mod12(leadPitch - dom.rootPc);
    auto fits = [mel](int t) {
        if (t == mel) {
            return true;   // the melody is the alteration
        }
        const int below = mod12(mel - t);   // how far under the melody it would sit
        return below >= 3;                  // keep at least a minor third under the lead
    };
    const bool melodyOnNinth = mel == 2;
    const bool melodyOnFifth = mel == 7 || mel == 9;   // the 5th, or the 13th that ♯5 rubs against
    unsigned out = 0;
    // ninths: ♭9 preferred, else ♯9 (never with the melody on the 3rd: a half step)
    if (!melodyOnNinth) {
        if (fits(1)) {
            out |= Flat9;
        } else if (fits(3) && mel != 4) {
            out |= Sharp9;
        }
    }
    // fifths: ♯5 preferred, else ♭5
    if (!melodyOnFifth) {
        if (fits(8)) {
            out |= Sharp5;
        } else if (fits(6)) {
            out |= Flat5;
        }
    }
    return out;
}

ChordSymbol halfStepSlide(const ChordSymbol& dominant)
{
    ChordSymbol s = dominant;
    s.rootPc = mod12(dominant.rootPc + 6);
    s.rootTpc = dominant.rootTpc - 6;
    if (s.rootTpc < 8) {
        s.rootTpc += 12;
    }
    s.alterations = 0;
    s.text = chordText(s);
    return s;
}

bool resolvesAsV(const std::vector<ChordSymbol>& chords, size_t i)
{
    if (i + 1 >= chords.size() || !chords[i].valid || chords[i].family != Family::Dominant || !chords[i + 1].valid) {
        return false;
    }
    const int to = chords[i + 1].rootPc;
    return to == mod12(chords[i].rootPc + 5) || to == mod12(chords[i].rootPc - 1);
}

std::vector<VoicedNote> voiceMelody(const std::vector<MelodyNote>& melody, std::vector<ChordSymbol>& chords,
                                    const HarmonyOptions& options)
{
    // §1.8: the half-step slide on the V of a ii–V–I
    if (options.halfStepSlide) {
        for (size_t i = 1; i + 1 < chords.size(); ++i) {
            const bool ii = chords[i - 1].valid && chords[i - 1].family == Family::Minor
                            && chords[i - 1].rootPc == mod12(chords[i].rootPc + 7);
            if (ii && chords[i].family == Family::Dominant && !chords[i].sus
                && chords[i + 1].rootPc == mod12(chords[i].rootPc + 5)) {
                chords[i] = halfStepSlide(chords[i]);
            }
        }
    }
    // §1.7: alterations on a V that resolves, chosen so every melody note on that chord allows them
    if (options.addAlterations) {
        for (size_t c = 0; c < chords.size(); ++c) {
            if (!resolvesAsV(chords, c) || chords[c].alterations || chords[c].sus) {
                continue;
            }
            unsigned allowed = Flat9 | Sharp9 | Flat5 | Sharp5;
            bool any = false;
            for (const MelodyNote& n : melody) {
                if (!n.rest && n.chordIndex == int(c)) {
                    allowed &= allowedAlterations(n.pitch, chords[c]);
                    any = true;
                }
            }
            if (!any) {
                continue;
            }
            // one ninth and one fifth alteration at most; ♭9 and ♯5 first
            unsigned chosen = 0;
            if (allowed & Flat9) {
                chosen |= Flat9;
            } else if (allowed & Sharp9) {
                chosen |= Sharp9;
            }
            if (allowed & Sharp5) {
                chosen |= Sharp5;
            } else if (allowed & Flat5) {
                chosen |= Flat5;
            }
            if (chosen) {
                chords[c].alterations = chosen;
                chords[c].text = chordText(chords[c]);
            }
        }
    }

    std::vector<VoicedNote> out(melody.size());
    auto chordAt = [&chords](const MelodyNote& n) {
        return n.chordIndex >= 0 && size_t(n.chordIndex) < chords.size() ? chords[size_t(n.chordIndex)] : ChordSymbol();
    };
    for (size_t i = 0; i < melody.size(); ++i) {
        const MelodyNote& n = melody[i];
        VoicedNote& r = out[i];
        if (n.rest) {
            continue;
        }
        const ChordSymbol chord = chordAt(n);
        r.chord = chord;
        r.writtenRootPc = chord.valid ? chord.rootPc : -1;
        if (!chord.valid) {
            r.note = "no chord symbol here";
            continue;
        }
        if (isChordTone(n.pitch, chord)) {
            r.voicing = closedVoicing(n.pitch, chord);
            r.chordTone = true;
            if (!r.voicing.ok) {
                r.note = r.voicing.reason;
            }
            continue;
        }
        // a non-chord tone (§1.5)
        r.chordTone = false;
        auto tryPlaning = [&]() -> bool {
            if (i + 1 >= melody.size() || melody[i + 1].rest) {
                return false;
            }
            const MelodyNote& next = melody[i + 1];
            const ChordSymbol nextChord = chordAt(next);
            const int step = n.pitch - next.pitch;
            if (std::abs(step) != 1 || !isChordTone(next.pitch, nextChord)) {
                return false;
            }
            const Voicing target = closedVoicing(next.pitch, nextChord);
            if (!target.ok) {
                return false;
            }
            r.voicing = planedVoicing(target, step);
            r.chord = nextChord;
            r.tool = NctTool::ChromaticPlaning;
            return true;
        };
        auto tryDiatonic = [&]() -> bool {
            const ChordSymbol pass = diatonicPassingChord(chord);
            if (!pass.valid || !isChordTone(n.pitch, pass)) {
                return false;
            }
            const Voicing v = closedVoicing(n.pitch, pass);
            if (!v.ok) {
                return false;
            }
            r.voicing = v;
            r.chord = pass;
            r.tool = NctTool::Diatonic;
            return true;
        };
        auto useDiminished = [&]() {
            r.voicing = diminishedVoicing(n.pitch);
            r.tool = NctTool::Diminished;
        };
        bool done = false;
        switch (options.nct) {
        case NctTool::ChromaticPlaning:
            done = tryPlaning();
            break;
        case NctTool::Diatonic:
            done = tryDiatonic();
            break;
        case NctTool::Diminished:
            useDiminished();
            done = true;
            break;
        case NctTool::Automatic:
            break;
        }
        if (!done) {
            if (options.nct != NctTool::Automatic) {
                r.note = "the chosen tool doesn't fit this note, so another was used";
            }
            if (!tryPlaning() && !tryDiatonic()) {
                useDiminished();
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Orchestration
// ---------------------------------------------------------------------------

std::optional<std::string> techniqueUnsuitable(SaxTechnique t, const std::vector<MelodyNote>& melody)
{
    const bool spread = t == SaxTechnique::Drop2 || t == SaxTechnique::Drop2And4 || t == SaxTechnique::FourPartDrop2
                        || t == SaxTechnique::Drop2BariRoots;
    if (!spread) {
        return std::nullopt;
    }
    std::vector<double> lengths;
    int fast = 0;
    for (const MelodyNote& n : melody) {
        if (!n.rest) {
            lengths.push_back(n.seconds);
            fast += n.seconds < FAST_NOTE_SECONDS ? 1 : 0;
        }
    }
    if (lengths.empty()) {
        return std::nullopt;
    }
    std::sort(lengths.begin(), lengths.end());
    const double median = lengths[lengths.size() / 2];
    if (fast * 4 > int(lengths.size())) {
        return std::string("This line moves fast: spread voicings lose clarity. Unison, octave unison or closed suit it better.");
    }
    if (median < SPREAD_NOTE_SECONDS) {
        return std::string("Spread voicings need slower notes (medium tempo or slower). Closed suits this line better.");
    }
    return std::nullopt;
}

namespace {
//! The chord tone nearest the middle of two pitches, strictly between them
int nearestChordToneBetween(int hi, int lo, const std::vector<int>& pcs, const std::set<int>& avoid)
{
    const double mid = (hi + lo) / 2.0;
    int best = -1;
    double bestD = 1e9;
    for (int p = hi - 1; p > lo; --p) {
        const int pc = mod12(p);
        if (std::find(pcs.begin(), pcs.end(), pc) == pcs.end() || avoid.count(pc)) {
            continue;
        }
        const double d = std::abs(p - mid);
        if (d < bestD) {   // scanning down, so a tie keeps the higher one
            bestD = d;
            best = p;
        }
    }
    return best;
}

int rootBelow(int rootPc, int below, int low, int high)
{
    int p = below - 1;
    while (mod12(p) != rootPc) {
        --p;
    }
    while (p > high) {
        p -= 12;
    }
    while (p < low) {
        p += 12;
    }
    return p;
}
}

namespace {
std::vector<SectionNotes> orchestrateAt(const std::vector<MelodyNote>& melody, const std::vector<VoicedNote>& voiced,
                                        const OrchestrationOptions& o, const Ranges& rg, int saxShift, bool triads)
{
    std::vector<SectionNotes> out(melody.size());
    // §1.9: the bari leaves a unison line it can't play comfortably
    bool bariInUnison = true;
    for (const MelodyNote& n : melody) {
        if (!n.rest && (n.pitch + saxShift > rg.saxHigh[4] || n.pitch + saxShift < rg.saxLow[4])) {
            bariInUnison = false;
        }
    }

    for (size_t i = 0; i < melody.size(); ++i) {
        const MelodyNote& n = melody[i];
        SectionNotes& s = out[i];
        if (n.rest) {
            continue;
        }
        const VoicedNote& vn = voiced[i];
        const bool harmonised = vn.voicing.ok;
        const std::array<int, 4> v = harmonised ? vn.voicing.pitches : std::array<int, 4> { n.pitch, n.pitch, n.pitch, n.pitch };
        // the bari plays the written chord's root (it stays put under passing chords)
        const int writtenRootPc = vn.writtenRootPc >= 0 ? vn.writtenRootPc : (vn.chord.valid ? vn.chord.rootPc : -1);

        // trumpets
        std::array<int, 4> tpt = v;
        if (o.trumpets == TrumpetTechnique::Unison) {
            tpt = { n.pitch, n.pitch, n.pitch, n.pitch };
        } else if (triads && harmonised) {
            // §1.6: 1 and 4 in octaves, 2 at least a minor third under the lead, 3 between 2 and 4
            const int t1 = n.pitch;
            const int t4 = n.pitch - 12;
            const int t2 = v[1];
            std::vector<int> pcs;
            for (int p : v) {
                pcs.push_back(mod12(p));
            }
            const int t3 = nearestChordToneBetween(t2, t4, pcs, { mod12(t1), mod12(t2) });
            tpt = { t1, t2, t3 >= 0 ? t3 : v[2], t4 };
        }
        if (o.trumpets != TrumpetTechnique::LeaveOut) {
            s.trumpets = tpt;
        }

        // trombones
        std::array<int, 4> tbn { v[0] - 12, v[1] - 12, v[2] - 12, v[3] - 12 };
        if (triads && harmonised) {
            // trombone 1 in its strong high range, the others closed under it (§1.6)
            int tb1 = -1;
            double bestD = 1e9;
            for (int p = rg.leadTromboneStrongHigh; p >= rg.leadTromboneStrongLow; --p) {
                const int pc = mod12(p);
                bool tone = false;
                for (int q : v) {
                    tone = tone || mod12(q) == pc;
                }
                if (!tone || p >= tpt[3]) {
                    continue;
                }
                const double d = std::abs(p - (rg.leadTromboneStrongLow + rg.leadTromboneStrongHigh) / 2.0);
                if (d < bestD) {
                    bestD = d;
                    tb1 = p;
                }
            }
            // a low lead leaves no strong-range note under trumpet 4: the highest chord tone under it
            for (int p = std::min(tpt[3] - 1, rg.leadTromboneStrongLow - 1); tb1 < 0 && p >= rg.leadTromboneStrongLow - 12; --p) {
                for (int q : v) {
                    if (mod12(q) == mod12(p)) {
                        tb1 = p;
                    }
                }
            }
            if (tb1 >= 0) {
                const Voicing under = closedVoicing(tb1, vn.chord);
                if (under.ok) {
                    tbn = under.pitches;
                }
            }
        }
        if (o.trombones == TromboneTechnique::Unison) {
            int p = n.pitch - 12;
            while (p > rg.tromboneMax) {
                p -= 12;
            }
            tbn = { p, p, p, p };
        }
        if (o.trombones != TromboneTechnique::LeaveOut) {
            s.trombones = tbn;
        }

        // saxophones
        const int L = n.pitch + saxShift;
        std::array<int, 4> sv { v[0] + saxShift, v[1] + saxShift, v[2] + saxShift, v[3] + saxShift };
        auto bariRoot = [&](int lowestOther) {
            const int pc = writtenRootPc >= 0 ? writtenRootPc : mod12(n.pitch);
            return rootBelow(pc, lowestOther, rg.saxLow[4], rg.saxHigh[4]);
        };
        std::array<int, 5>& x = s.saxes;
        switch (o.saxes) {
        case SaxTechnique::LeaveOut:
            break;
        case SaxTechnique::DoubleTrombones:
            x = { tbn[0], tbn[1], tbn[2], tbn[3], -1 };
            // a trombone note out of a sax's reach is doubled an octave away (still the same chord tone)
            for (size_t j = 0; j < 4; ++j) {
                while (x[j] < rg.saxLow[j] && x[j] + 12 <= rg.saxHigh[j]) {
                    x[j] += 12;
                }
                while (x[j] > rg.saxHigh[j] && x[j] - 12 >= rg.saxLow[j]) {
                    x[j] -= 12;
                }
            }
            if (o.bariAvailable) {
                x[4] = bariRoot(std::min(tbn[3], x[3]));
            }
            break;
        case SaxTechnique::Unison:
            x = { L, L, L, L, bariInUnison ? L : -1 };
            break;
        case SaxTechnique::OctaveUnison4Up1Down:
            x = { L, L, L, L, L - 12 };
            break;
        case SaxTechnique::OctaveUnison2Up3Down:
            x = { L, L, L - 12, L - 12, L - 12 };
            break;
        case SaxTechnique::TwoPart: {
            int second = sv[1];
            for (int k = 1; k < 4; ++k) {
                const int below = L - sv[k];
                if (below == 3 || below == 4) {
                    second = sv[k];
                    break;
                }
            }
            if (L - second != 3 && L - second != 4) {
                for (int k = 1; k < 4; ++k) {
                    const int below = L - (sv[k] - 12 * ((L - sv[k]) < 8 ? 1 : 0));
                    if (below == 8 || below == 9) {
                        second = L - below;
                        break;
                    }
                }
            }
            const int bari = second <= rg.saxHigh[4] ? second : second - 12;
            x = { L, L, second, second, bari };
            break;
        }
        case SaxTechnique::Closed:
            x = { L, sv[1], sv[2], sv[3], L - 12 };
            break;
        case SaxTechnique::Drop2:
            x = { L, sv[2], sv[3], L - 12, sv[1] - 12 };
            break;
        case SaxTechnique::Drop2And4:
            x = { L, sv[2], L - 12, sv[1] - 12, sv[3] - 12 };
            break;
        case SaxTechnique::FourPartDrop2: {
            const int busy = o.busySax >= 0 && o.busySax < 5 ? o.busySax : 3;
            const std::array<int, 4> four { L, sv[2], sv[3], sv[1] - 12 };
            int k = 0;
            for (int j = 0; j < 5; ++j) {
                x[size_t(j)] = j == busy ? -1 : four[size_t(k++)];
            }
            break;
        }
        case SaxTechnique::Drop2BariRoots:
            x = { L, sv[2], sv[3], sv[1] - 12, -1 };
            x[4] = bariRoot(x[3]);
            break;
        }
        if (o.saxes != SaxTechnique::LeaveOut && !o.bariAvailable) {
            x[4] = -1;
        }
        // no bari: the bass trombone takes the roots (§1.3)
        if (!o.bariAvailable && o.trombones != TromboneTechnique::LeaveOut && o.saxes == SaxTechnique::DoubleTrombones) {
            s.trombones[3] = rootBelow(writtenRootPc >= 0 ? writtenRootPc : mod12(n.pitch), s.trombones[2], 34, 65);
        }
        (void)o.altosFillHighTriads;

        // flag anything a player can't reach; Proofread reports the same notes after the parts are written
        for (int j = 0; j < 4; ++j) {
            if (s.trombones[size_t(j)] >= 0 && s.trombones[size_t(j)] > rg.tromboneMax) {
                s.outOfRange |= 1u << (4 + j);
            }
        }
        for (int j = 0; j < 5; ++j) {
            const int p = s.saxes[size_t(j)];
            if (p >= 0 && (p < rg.saxLow[size_t(j)] || p > rg.saxHigh[size_t(j)])) {
                s.outOfRange |= 1u << (8 + j);
            }
        }
    }
    return out;
}

int saxRangeFaults(const std::vector<SectionNotes>& s)
{
    int n = 0;
    for (const SectionNotes& x : s) {
        for (int j = 0; j < 5; ++j) {
            n += (x.outOfRange >> (8 + j)) & 1u;
        }
    }
    return n;
}
}

std::vector<SectionNotes> orchestrate(const std::vector<MelodyNote>& melody, const std::vector<VoicedNote>& voiced,
                                      const OrchestrationOptions& o, const Ranges& rg)
{
    int maxLead = -1;
    int preferred = 0;
    std::vector<int> pitches;
    for (const MelodyNote& n : melody) {
        if (!n.rest) {
            maxLead = std::max(maxLead, n.pitch);
            pitches.push_back(n.pitch);
        }
    }
    if (!pitches.empty()) {
        // the sax lead plays the melody in alto 1's upper middle (concert C5 by default); the line keeps its shape
        std::sort(pitches.begin(), pitches.end());
        const int median = pitches[pitches.size() / 2];
        const int centre = rg.saxHigh[0] - 8;
        while (median + preferred > centre + 6) {
            preferred -= 12;
        }
        while (median + preferred < centre - 6) {
            preferred += 12;
        }
    }
    const bool triads = o.trumpets == TrumpetTechnique::TrumpetTriads
                        || (o.trumpets == TrumpetTechnique::Automatic && maxLead > rg.shoutTrumpetThreshold);

    // the octave for the sax section that keeps the most notes playable, chosen phrase by phrase (a rest ends a
    // phrase, so the section never jumps an octave in the middle of a line); the preferred octave when equal
    std::vector<SectionNotes> all(melody.size());
    size_t start = 0;
    while (start < melody.size()) {
        size_t end = start;
        while (end < melody.size() && !melody[end].rest) {
            ++end;
        }
        if (end == start) {   // a rest
            ++start;
            continue;
        }
        const std::vector<MelodyNote> m(melody.begin() + long(start), melody.begin() + long(end));
        const std::vector<VoicedNote> v(voiced.begin() + long(start), voiced.begin() + long(end));
        std::vector<SectionNotes> best = orchestrateAt(m, v, o, rg, preferred, triads);
        int bestFaults = saxRangeFaults(best);
        for (int shift : { preferred - 12, preferred + 12, preferred - 24 }) {
            if (bestFaults == 0) {
                break;
            }
            std::vector<SectionNotes> t = orchestrateAt(m, v, o, rg, shift, triads);
            const int f = saxRangeFaults(t);
            if (f < bestFaults) {
                bestFaults = f;
                best = std::move(t);
            }
        }
        std::copy(best.begin(), best.end(), all.begin() + long(start));
        start = end;
    }
    return all;
}

// ---------------------------------------------------------------------------
// Spelling
// ---------------------------------------------------------------------------

int spellChordTone(int pitch, const ChordSymbol& c)
{
    const int iv = mod12(pitch - c.rootPc);
    const bool minorThird = c.family == Family::Minor || c.family == Family::Diminished || c.family == Family::HalfDiminished;
    int delta = 0;
    switch (iv) {
    case 0: delta = 0;
        break;
    case 1: delta = -5;
        break;                      // ♭9
    case 2: delta = 2;
        break;
    case 3: delta = minorThird ? -3 : 9;
        break;                      // ♭3, or ♯9 (A♯ over G: it is an alteration, not a minor 3rd)
    case 4: delta = 4;
        break;
    case 5: delta = -1;
        break;
    case 6: delta = (c.alterations & Flat5) || c.family == Family::Diminished || c.family == Family::HalfDiminished ? -6 : 6;
        break;
    case 7: delta = 1;
        break;
    case 8: delta = (c.alterations & Sharp5) ? 8 : -4;
        break;
    case 9: delta = c.family == Family::Diminished ? -9 : 3;
        break;
    case 10: delta = -2;
        break;
    case 11: delta = 5;
        break;
    default: break;
    }
    return simplerSpelling(c.rootTpc + delta);
}

int spellChromatic(int pitch, int targetPitch, int targetTpc)
{
    if (pitch == targetPitch) {
        return targetTpc;
    }
    // a half step to the target, on the neighbouring letter, so it looks like a second (§1.10)
    return simplerSpelling(pitch < targetPitch ? targetTpc + 5 : targetTpc - 5);
}

int spellDiminished(int pitch, int leadPitch, int leadTpc)
{
    const int steps = (leadPitch - pitch) / 3;   // minor thirds down
    return simplerSpelling(leadTpc + 3 * steps);
}
}
