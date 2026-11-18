/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */
#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <set>

#include "notation/internal/jubelle/harmony/harmonyrules.h"

using namespace mu::notation::jubelle::harmony;

namespace {
using P4 = std::array<int, 4>;
using P5 = std::array<int, 5>;

int pc(int p) { return ((p % 12) + 12) % 12; }

MelodyNote note(int pitch, int chord, double seconds = 0.6)
{
    MelodyNote n;
    n.pitch = pitch;
    n.chordIndex = chord;
    n.seconds = seconds;
    return n;
}

// the closed-voicing rules of §1.2, checked on any voicing
void expectClosedRules(const Voicing& v, const ChordSymbol& c, const std::string& where)
{
    ASSERT_TRUE(v.ok) << where << ": " << v.reason;
    const int lead = v.pitches[0];
    EXPECT_GE(lead - v.pitches[1], 3) << where << ": a minor third under the lead";
    for (int i = 1; i < 4; ++i) {
        EXPECT_LT(v.pitches[size_t(i)], v.pitches[size_t(i - 1)]) << where << ": voices go downwards";
        EXPECT_GT(v.pitches[size_t(i)], lead - 12) << where << ": within an octave";
        EXPECT_NE(pc(v.pitches[size_t(i)]), pc(lead)) << where << ": the melody isn't doubled";
        EXPECT_TRUE(isChordTone(v.pitches[size_t(i)], c)) << where << ": " << v.pitches[size_t(i)] << " is a chord tone";
    }
}
}

// ---------------------------------------------------------------- chord symbols

TEST(Jubelle_HarmonyRules, ParsesChordSymbols)
{
    ChordSymbol c = parseChordSymbol("Dm7");
    EXPECT_TRUE(c.valid);
    EXPECT_EQ(c.family, Family::Minor);
    EXPECT_EQ(c.rootPc, 2);
    EXPECT_EQ(c.rootTpc, 16);

    c = parseChordSymbol("Bb7");
    EXPECT_EQ(c.family, Family::Dominant);
    EXPECT_EQ(c.rootPc, 10);
    EXPECT_EQ(c.rootTpc, 12);

    EXPECT_EQ(parseChordSymbol("G7b9").alterations, unsigned(Flat9));
    EXPECT_EQ(parseChordSymbol("Eb7#9").alterations, unsigned(Sharp9));
    EXPECT_EQ(parseChordSymbol("C-7").family, Family::Minor);
    EXPECT_EQ(parseChordSymbol("C-7").alterations, 0u) << "the minor sign is not a flat";
    EXPECT_EQ(parseChordSymbol("C-9").alterations, 0u);
    EXPECT_EQ(parseChordSymbol("Cmaj7").family, Family::Major);
    EXPECT_EQ(parseChordSymbol("CΔ7").family, Family::Major);
    EXPECT_EQ(parseChordSymbol("C6/9").family, Family::Major);
    EXPECT_EQ(parseChordSymbol("C6/9").bassPc, -1) << "6/9 is not a slash chord";
    EXPECT_EQ(parseChordSymbol("Cm(maj7)").family, Family::Minor);
    EXPECT_EQ(parseChordSymbol("Bm7b5").family, Family::HalfDiminished);
    EXPECT_EQ(parseChordSymbol("Bø7").family, Family::HalfDiminished);
    EXPECT_EQ(parseChordSymbol("Co7").family, Family::Diminished);
    EXPECT_EQ(parseChordSymbol("Cdim7").family, Family::Diminished);
    EXPECT_EQ(parseChordSymbol("F#m7").rootTpc, 20);
    EXPECT_EQ(parseChordSymbol("C/E").bassPc, 4);
    EXPECT_TRUE(parseChordSymbol("G9sus").sus);
    EXPECT_EQ(parseChordSymbol("G9sus").family, Family::Dominant);
    EXPECT_EQ(parseChordSymbol("G7alt").alterations, unsigned(Flat9 | Sharp9 | Sharp5));
    EXPECT_FALSE(parseChordSymbol("").valid);
    EXPECT_FALSE(parseChordSymbol("N.C.").valid);
}

TEST(Jubelle_HarmonyRules, ChordsFromMuseScoreIntervals)
{
    auto mask = [](std::initializer_list<int> ivs) {
        unsigned m = 0;
        for (int i : ivs) {
            m |= 1u << i;
        }
        return m;
    };
    EXPECT_EQ(chordFromIntervals(15, mask({ 0, 4, 7, 10 })).family, Family::Dominant);
    EXPECT_EQ(chordFromIntervals(16, mask({ 0, 3, 7, 10 })).family, Family::Minor);
    EXPECT_EQ(chordFromIntervals(19, mask({ 0, 3, 6, 10 })).family, Family::HalfDiminished);
    EXPECT_EQ(chordFromIntervals(14, mask({ 0, 3, 6, 9 })).family, Family::Diminished);
    EXPECT_EQ(chordFromIntervals(14, mask({ 0, 4, 7, 11, 2 })).family, Family::Major);
    EXPECT_EQ(chordFromIntervals(15, mask({ 0, 1, 4, 7, 10 })).alterations, unsigned(Flat9));
    EXPECT_EQ(chordFromIntervals(15, mask({ 0, 3, 4, 10 })).alterations, unsigned(Sharp9));
    EXPECT_TRUE(chordFromIntervals(15, mask({ 0, 5, 7, 10 })).sus);
    EXPECT_EQ(chordFromIntervals(15, mask({ 0, 4, 7, 10 })).text, "G7");
    EXPECT_FALSE(chordFromIntervals(15, 0).valid);
}

// ---------------------------------------------------------------- closed voicing (§1.2)

TEST(Jubelle_HarmonyRules, MelodyOnTheThirdPutsTheRootSecond)
{
    const ChordSymbol c = parseChordSymbol("Cmaj7");
    const Voicing v = closedVoicing(76, c);   // E5
    expectClosedRules(v, c, "E over C");
    EXPECT_EQ(v.pitches[1], 72) << "the second voice takes the root";
    EXPECT_EQ(v.pitches, (P4 { 76, 72, 71, 67 }));
}

TEST(Jubelle_HarmonyRules, MelodyOnTheRootPutsTheSixthSecond)
{
    const ChordSymbol c = parseChordSymbol("C6");
    const Voicing v = closedVoicing(72, c);
    expectClosedRules(v, c, "C over C");
    EXPECT_EQ(pc(v.pitches[1]), 9) << "the 6th under a root melody";

    const ChordSymbol g = parseChordSymbol("G7");
    const Voicing w = closedVoicing(79, g);
    expectClosedRules(w, g, "G over G7");
    EXPECT_EQ(pc(w.pitches[1]), 4) << "the 13th under a root melody";
}

TEST(Jubelle_HarmonyRules, LessonExampleDminorUnderD)
{
    // the source: under D in a Dm7 bar, Dm11 (D–A–G–F), or momentarily Dm6 (D–B–A–F)
    const ChordSymbol c = parseChordSymbol("Dm7");
    const Voicing v = closedVoicing(74, c);
    expectClosedRules(v, c, "D over Dm7");
    EXPECT_EQ(v.pitches, (P4 { 74, 69, 67, 65 }));
}

TEST(Jubelle_HarmonyRules, RootLeftOutUnlessMelodyOnTheThird)
{
    for (const char* sym : { "Cmaj7", "C6", "Dm7", "G7", "F7", "Bbmaj7", "Ebm7", "Ab7" }) {
        const ChordSymbol c = parseChordSymbol(sym);
        for (int lead = 67; lead <= 84; ++lead) {
            if (!isChordTone(lead, c)) {
                continue;
            }
            const Voicing v = closedVoicing(lead, c);
            expectClosedRules(v, c, std::string(sym) + " lead " + std::to_string(lead));
            const bool onThird = pc(lead - c.rootPc) == (c.family == Family::Minor ? 3 : 4);
            for (int i = 1; i < 4; ++i) {
                if (!onThird) {
                    EXPECT_NE(pc(v.pitches[size_t(i)] - c.rootPc), 0) << sym << " lead " << lead << ": no root";
                }
            }
            // the 3rd is always heard, unless it would sit less than a minor third under the lead
            const int third = c.family == Family::Minor ? 3 : 4;
            const int thirdBelowLead = pc(lead - (c.rootPc + third));
            std::set<int> pcs;
            for (int p : v.pitches) {
                pcs.insert(pc(p - c.rootPc));
            }
            if (thirdBelowLead == 0 || thirdBelowLead >= 3) {
                EXPECT_TRUE(pcs.count(third)) << sym << " lead " << lead << ": the 3rd";
            }
        }
    }
}

TEST(Jubelle_HarmonyRules, SharpNineAboveTheThirdAndFlatNineAboveTheRoot)
{
    const ChordSymbol c = parseChordSymbol("G7#9");
    for (int lead = 67; lead <= 84; ++lead) {
        if (!isChordTone(lead, c)) {
            continue;
        }
        const Voicing v = closedVoicing(lead, c);
        if (!v.ok) {
            continue;
        }
        int third = -1;
        int sharp9 = -1;
        for (int p : v.pitches) {
            if (pc(p) == pc(71)) {
                third = p;
            }
            if (pc(p) == pc(70)) {
                sharp9 = p;
            }
        }
        if (third >= 0 && sharp9 >= 0) {
            EXPECT_GT(sharp9, third) << "lead " << lead;
        }
    }
    const ChordSymbol b9 = parseChordSymbol("G7b9");
    for (int lead = 67; lead <= 84; ++lead) {
        if (!isChordTone(lead, b9)) {
            continue;
        }
        const Voicing v = closedVoicing(lead, b9);
        int root = -1;
        int flat9 = -1;
        for (int p : v.pitches) {
            if (pc(p) == 7) {
                root = p;
            }
            if (pc(p) == 8) {
                flat9 = p;
            }
        }
        if (root >= 0 && flat9 >= 0) {
            EXPECT_GT(flat9, root) << "lead " << lead;
        }
    }
}

// ---------------------------------------------------------------- non-chord tones (§1.5)

TEST(Jubelle_HarmonyRules, DiminishedPassingChord)
{
    EXPECT_EQ(diminishedVoicing(72).pitches, (P4 { 72, 69, 66, 63 }));
    std::vector<ChordSymbol> chords { parseChordSymbol("C6") };
    HarmonyOptions o;
    o.nct = NctTool::Diminished;
    const auto r = voiceMelody({ note(77, 0) }, chords, o);
    EXPECT_FALSE(r[0].chordTone);
    EXPECT_EQ(r[0].tool, NctTool::Diminished);
    EXPECT_EQ(r[0].voicing.pitches, (P4 { 77, 74, 71, 68 }));
}

TEST(Jubelle_HarmonyRules, LessonExampleFOverCBecomesG7)
{
    std::vector<ChordSymbol> chords { parseChordSymbol("C6") };
    HarmonyOptions o;
    o.nct = NctTool::Diatonic;
    const auto r = voiceMelody({ note(77, 0) }, chords, o);
    EXPECT_EQ(r[0].tool, NctTool::Diatonic);
    EXPECT_EQ(r[0].chord.family, Family::Dominant);
    EXPECT_EQ(r[0].chord.rootPc, 7) << "the V7 of C";
    expectClosedRules(r[0].voicing, r[0].chord, "F as the 7th of G7");
    EXPECT_EQ(r[0].voicing.pitches, (P4 { 77, 74, 71, 69 }));

    // under a dominant, the passing chord is its ii (Dm under G7)
    const ChordSymbol ii = diatonicPassingChord(parseChordSymbol("G7"));
    EXPECT_EQ(ii.family, Family::Minor);
    EXPECT_EQ(ii.rootPc, 2);
    // under a minor chord, its V7 (E7 under Am)
    const ChordSymbol v = diatonicPassingChord(parseChordSymbol("Am7"));
    EXPECT_EQ(v.family, Family::Dominant);
    EXPECT_EQ(v.rootPc, 4);
}

TEST(Jubelle_HarmonyRules, ChromaticPlaningVoicesTheTargetFirst)
{
    // C♯ moving up to D over Dm7: voice the D, then move every part down a half step
    std::vector<ChordSymbol> chords { parseChordSymbol("Dm7") };
    HarmonyOptions o;   // Automatic chooses planing for a half-step approach
    const auto r = voiceMelody({ note(73, 0), note(74, 0) }, chords, o);
    EXPECT_EQ(r[0].tool, NctTool::ChromaticPlaning);
    EXPECT_EQ(r[1].voicing.pitches, (P4 { 74, 69, 67, 65 }));
    EXPECT_EQ(r[0].voicing.pitches, (P4 { 73, 68, 66, 64 }));
}

TEST(Jubelle_HarmonyRules, AutomaticFallsBackInTheLessonsOrder)
{
    std::vector<ChordSymbol> chords { parseChordSymbol("C6") };
    HarmonyOptions o;
    // F then A (a leap): no planing; F is the 7th of G7, so diatonic
    auto r = voiceMelody({ note(77, 0), note(81, 0) }, chords, o);
    EXPECT_EQ(r[0].tool, NctTool::Diatonic);
    // C♯ over C, then F: neither planing nor in G7 → diminished
    r = voiceMelody({ note(73, 0), note(77, 0) }, chords, o);
    EXPECT_EQ(r[0].tool, NctTool::Diminished);
    // a requested tool that can't apply says so
    o.nct = NctTool::ChromaticPlaning;
    r = voiceMelody({ note(77, 0), note(81, 0) }, chords, o);
    EXPECT_FALSE(r[0].note.empty());
    EXPECT_TRUE(r[0].voicing.ok);
}

// ---------------------------------------------------------------- alterations (§1.7) and the slide (§1.8)

TEST(Jubelle_HarmonyRules, AlterationsFollowTheMelody)
{
    const ChordSymbol g7 = parseChordSymbol("G7");
    // melody on the 3rd (B): no ♯9 (a half step); the author's ♭9♯5 fits
    EXPECT_EQ(allowedAlterations(71, g7), unsigned(Flat9 | Sharp5));
    // melody on the 9th (A): no altered ninths
    EXPECT_EQ(allowedAlterations(69, g7) & (Flat9 | Sharp9), 0u);
    // melody on the 5th (D): no altered fifths
    EXPECT_EQ(allowedAlterations(74, g7) & (Flat5 | Sharp5), 0u);
    // melody on the 13th (E): no ♯5 rubbing against it
    EXPECT_EQ(allowedAlterations(76, g7) & Sharp5, 0u);
    // not a dominant: nothing
    EXPECT_EQ(allowedAlterations(72, parseChordSymbol("Cmaj7")), 0u);
    EXPECT_EQ(allowedAlterations(72, parseChordSymbol("G9sus")), 0u);
}

TEST(Jubelle_HarmonyRules, AlterationsOnlyOnAResolvingVAndOnlyWhenAsked)
{
    std::vector<ChordSymbol> chords { parseChordSymbol("Dm7"), parseChordSymbol("G7"), parseChordSymbol("Cmaj7") };
    std::vector<MelodyNote> m { note(77, 0), note(71, 1), note(72, 2) };
    HarmonyOptions o;
    auto copy = chords;
    voiceMelody(m, copy, o);
    EXPECT_EQ(copy[1].alterations, 0u) << "off by default";
    o.addAlterations = true;
    copy = chords;
    const auto r = voiceMelody(m, copy, o);
    EXPECT_EQ(copy[1].alterations, unsigned(Flat9 | Sharp5));
    EXPECT_EQ(copy[1].text, "G7b9#5");
    expectClosedRules(r[1].voicing, copy[1], "B over G7b9#5");
    // a dominant that doesn't resolve is left alone
    std::vector<ChordSymbol> noRes { parseChordSymbol("G7"), parseChordSymbol("F6") };
    voiceMelody({ note(71, 0), note(72, 1) }, noRes, o);
    EXPECT_EQ(noRes[0].alterations, 0u);
}

TEST(Jubelle_HarmonyRules, HalfStepSlideDmDbC)
{
    std::vector<ChordSymbol> chords { parseChordSymbol("Dm7"), parseChordSymbol("G7"), parseChordSymbol("Cmaj7") };
    HarmonyOptions o;
    o.halfStepSlide = true;
    voiceMelody({ note(77, 0), note(77, 1), note(76, 2) }, chords, o);
    EXPECT_EQ(chords[1].rootPc, 1);
    EXPECT_EQ(chords[1].text, "Db7") << "the chord symbol says what the horns play";
    EXPECT_EQ(chords[1].family, Family::Dominant);
}

// ---------------------------------------------------------------- orchestration (§1.3, §1.6, §1.9)

TEST(Jubelle_HarmonyRules, FullBandClosed)
{
    std::vector<ChordSymbol> chords { parseChordSymbol("Cmaj7") };
    const std::vector<MelodyNote> m { note(76, 0) };
    const auto voiced = voiceMelody(m, chords, {});
    const auto s = orchestrate(m, voiced, {});
    EXPECT_EQ(s[0].trumpets, (P4 { 76, 72, 71, 67 }));
    EXPECT_EQ(s[0].trombones, (P4 { 64, 60, 59, 55 }));
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(s[0].saxes[size_t(i)], s[0].trombones[size_t(i)]) << "altos and tenors on the trombone notes";
    }
    EXPECT_EQ(pc(s[0].saxes[4]), 0) << "the bari on the root";
    EXPECT_LT(s[0].saxes[4], s[0].saxes[3]);
    EXPECT_EQ(s[0].outOfRange, 0u);

    // no baritone: the bass trombone takes the roots
    OrchestrationOptions o;
    o.bariAvailable = false;
    const auto t = orchestrate(m, voiced, o);
    EXPECT_EQ(t[0].saxes[4], -1);
    EXPECT_EQ(pc(t[0].trombones[3]), 0);
    EXPECT_LT(t[0].trombones[3], t[0].trombones[2]);
}

TEST(Jubelle_HarmonyRules, ShoutChorusTrumpetTriads)
{
    std::vector<ChordSymbol> chords { parseChordSymbol("C6") };
    const std::vector<MelodyNote> m { note(84, 0) };   // C6, above the A5 threshold
    const auto voiced = voiceMelody(m, chords, {});
    OrchestrationOptions o;
    o.trumpets = TrumpetTechnique::Automatic;
    const auto s = orchestrate(m, voiced, o);
    EXPECT_EQ(s[0].trumpets[0], 84);
    EXPECT_EQ(s[0].trumpets[3], 72) << "trumpets 1 and 4 in octaves";
    EXPECT_GE(s[0].trumpets[0] - s[0].trumpets[1], 3);
    EXPECT_LT(s[0].trumpets[2], s[0].trumpets[1]);
    EXPECT_GT(s[0].trumpets[2], s[0].trumpets[3]);
    const Ranges rg;
    EXPECT_GE(s[0].trombones[0], rg.leadTromboneStrongLow) << "trombone 1 in its strong range";
    EXPECT_LE(s[0].trombones[0], rg.leadTromboneStrongHigh);
    EXPECT_LT(s[0].trombones[0], s[0].trumpets[3]);
    for (int p : s[0].trombones) {
        EXPECT_TRUE(isChordTone(p, chords[0]));
    }
    // below the threshold, Automatic stays closed
    const std::vector<MelodyNote> low { note(76, 0) };
    std::vector<ChordSymbol> c2 { parseChordSymbol("Cmaj7") };
    const auto s2 = orchestrate(low, voiceMelody(low, c2, {}), o);
    EXPECT_EQ(s2[0].trumpets, (P4 { 76, 72, 71, 67 }));
}

TEST(Jubelle_HarmonyRules, TheNineSaxVoicings)
{
    std::vector<ChordSymbol> chords { parseChordSymbol("Cmaj7") };
    const std::vector<MelodyNote> m { note(76, 0) };
    const auto voiced = voiceMelody(m, chords, {});
    auto sax = [&](SaxTechnique t) {
        OrchestrationOptions o;
        o.trumpets = TrumpetTechnique::LeaveOut;
        o.trombones = TromboneTechnique::LeaveOut;
        o.saxes = t;
        return orchestrate(m, voiced, o)[0].saxes;
    };
    EXPECT_EQ(sax(SaxTechnique::Unison), (P5 { 76, 76, 76, 76, -1 })) << "the bari leaves a line too high for it";
    EXPECT_EQ(sax(SaxTechnique::OctaveUnison4Up1Down), (P5 { 76, 76, 76, 76, 64 }));
    {
        // a unison line above the tenors' reach goes down an octave as a whole
        std::vector<ChordSymbol> c2 { parseChordSymbol("Cmaj7") };
        const std::vector<MelodyNote> hi { note(79, 0) };
        OrchestrationOptions o;
        o.saxes = SaxTechnique::Unison;
        const auto s = orchestrate(hi, voiceMelody(hi, c2, {}), o);
        EXPECT_EQ(s[0].saxes, (P5 { 67, 67, 67, 67, 67 }));
        EXPECT_EQ(s[0].outOfRange, 0u);
    }
    EXPECT_EQ(sax(SaxTechnique::OctaveUnison2Up3Down), (P5 { 76, 76, 64, 64, 64 }));
    EXPECT_EQ(sax(SaxTechnique::TwoPart), (P5 { 76, 76, 72, 72, 60 }));
    EXPECT_EQ(sax(SaxTechnique::Closed), (P5 { 76, 72, 71, 67, 64 }));
    EXPECT_EQ(sax(SaxTechnique::Drop2), (P5 { 76, 71, 67, 64, 60 }));
    EXPECT_EQ(sax(SaxTechnique::Drop2And4), (P5 { 76, 71, 64, 60, 55 }));
    EXPECT_EQ(sax(SaxTechnique::FourPartDrop2), (P5 { 76, 71, 67, -1, 60 })) << "tenor 2 busy elsewhere";
    const P5 roots = sax(SaxTechnique::Drop2BariRoots);
    EXPECT_EQ((P4 { roots[0], roots[1], roots[2], roots[3] }), (P4 { 76, 71, 67, 60 }));
    EXPECT_EQ(pc(roots[4]), 0);
    EXPECT_LT(roots[4], roots[3]);
}

TEST(Jubelle_HarmonyRules, FastLinesWarnAgainstSpreadVoicings)
{
    std::vector<MelodyNote> fast;
    std::vector<MelodyNote> slow;
    for (int i = 0; i < 8; ++i) {
        fast.push_back(note(72 + i % 3, 0, 0.15));
        slow.push_back(note(72 + i % 3, 0, 0.8));
    }
    EXPECT_TRUE(techniqueUnsuitable(SaxTechnique::Drop2, fast).has_value());
    EXPECT_TRUE(techniqueUnsuitable(SaxTechnique::Drop2And4, fast).has_value());
    EXPECT_FALSE(techniqueUnsuitable(SaxTechnique::Closed, fast).has_value());
    EXPECT_FALSE(techniqueUnsuitable(SaxTechnique::Unison, fast).has_value());
    EXPECT_FALSE(techniqueUnsuitable(SaxTechnique::Drop2, slow).has_value());
}

// ---------------------------------------------------------------- spelling (§1.10)

TEST(Jubelle_HarmonyRules, Spelling)
{
    EXPECT_EQ(spellChordTone(70, parseChordSymbol("G7#9")), 24) << "A♯, the ♯9 of G";
    EXPECT_EQ(spellChordTone(70, parseChordSymbol("Gm7")), 12) << "B♭, the minor 3rd of G";
    EXPECT_EQ(spellChordTone(64, parseChordSymbol("Eb7b9")), 18) << "E, not F♭";
    EXPECT_EQ(spellChordTone(66, parseChordSymbol("C7b5")), 8) << "G♭";
    EXPECT_EQ(spellChordTone(68, parseChordSymbol("C7#5")), 22) << "G♯";
    EXPECT_EQ(spellChordTone(65, parseChordSymbol("Dm7")), 13) << "F";
    // a rising half step looks like a second; a descending one takes the flat
    EXPECT_EQ(spellChromatic(73, 74, 16), 21) << "C♯ up to D";
    EXPECT_EQ(spellChromatic(73, 72, 14), 9) << "D♭ down to C";
    // minor thirds look like thirds
    EXPECT_EQ(spellDiminished(69, 72, 14), 17);
    EXPECT_EQ(spellDiminished(66, 72, 14), 20);
    // no B♯, C♭, E♯, F♭
    EXPECT_EQ(simplerSpelling(26), 14);
    EXPECT_EQ(simplerSpelling(7), 19);
    EXPECT_EQ(simplerSpelling(25), 13);
    EXPECT_EQ(simplerSpelling(6), 18);
    for (int tpc = -1; tpc <= 33; ++tpc) {
        EXPECT_EQ(tpcToPc(simplerSpelling(tpc)), tpcToPc(tpc));
    }
}

// ---------------------------------------------------------------- a realistic chorus

namespace {
struct Chorus {
    std::vector<ChordSymbol> chords;
    std::vector<MelodyNote> melody;
};

//! Eight bars of a medium-swing tune in B♭ (concert): ii–V–I–VI, ii–V–I, with passing tones
//! and approach notes, as a lead trumpet line.
Chorus bbTune(double seconds)
{
    Chorus c;
    for (const char* s : { "Cm7", "F7", "Bbmaj7", "G7", "Cm7", "F7", "Bb6", "Bb6" }) {
        c.chords.push_back(parseChordSymbol(s));
    }
    const int bars[8][4] = {
        { 75, 74, 72, 70 },   // Eb D C Bb over Cm7
        { 69, 70, 72, 75 },   // A Bb C Eb over F7
        { 74, 72, 69, 70 },   // D C A Bb over Bbmaj7
        { 71, 74, 77, 76 },   // B D F E over G7
        { 75, 74, 72, 70 },
        { 72, 69, 67, 65 },   // C A G F over F7
        { 74, 73, 74, 77 },   // D C# D F over Bb6
        { 82, 81, 79, 77 },   // Bb A G F
    };
    for (int b = 0; b < 8; ++b) {
        for (int k = 0; k < 4; ++k) {
            c.melody.push_back(note(bars[b][k], b, seconds));
        }
    }
    return c;
}
}

TEST(Jubelle_HarmonyRules, RealisticChorusFullBand)
{
    Chorus c = bbTune(0.5);
    const auto voiced = voiceMelody(c.melody, c.chords, {});
    ASSERT_EQ(voiced.size(), c.melody.size());
    const auto s = orchestrate(c.melody, voiced, {});
    const Ranges rg;
    for (size_t i = 0; i < c.melody.size(); ++i) {
        const VoicedNote& v = voiced[i];
        ASSERT_TRUE(v.voicing.ok) << "note " << i;
        EXPECT_EQ(v.voicing.pitches[0], c.melody[i].pitch) << "the melody stays on top";
        if (v.chordTone || v.tool == NctTool::Diatonic) {
            expectClosedRules(v.voicing, v.chord, "note " + std::to_string(i));
        }
        for (int k = 1; k < 4; ++k) {
            EXPECT_GT(v.voicing.pitches[size_t(k)], c.melody[i].pitch - 12) << "note " << i;
        }
        EXPECT_EQ(s[i].trumpets[0], c.melody[i].pitch);
        for (int k = 0; k < 4; ++k) {
            EXPECT_EQ(s[i].trombones[size_t(k)], s[i].trumpets[size_t(k)] - 12);
            EXPECT_LE(s[i].trombones[size_t(k)], rg.tromboneMax);
        }
        EXPECT_EQ(s[i].outOfRange, 0u) << "note " << i;
        EXPECT_EQ(pc(s[i].saxes[4]), c.chords[size_t(c.melody[i].chordIndex)].rootPc) << "bari roots, note " << i;
    }
    // the C♯ in bar 7 approaches D by half step: planing
    EXPECT_EQ(voiced[25].tool, NctTool::ChromaticPlaning);
    // the deterministic promise: the same input gives the same parts
    auto again = c.chords;
    const auto voiced2 = voiceMelody(c.melody, again, {});
    const auto s2 = orchestrate(c.melody, voiced2, {});
    for (size_t i = 0; i < s.size(); ++i) {
        EXPECT_EQ(s[i].trumpets, s2[i].trumpets);
        EXPECT_EQ(s[i].trombones, s2[i].trombones);
        EXPECT_EQ(s[i].saxes, s2[i].saxes);
    }
}

TEST(Jubelle_HarmonyRules, RealisticChorusEverySaxVoicingStaysInRange)
{
    // the tune in the saxes' register (a fourth lower: concert G4–D5 … F5), phrases separated by rests
    Chorus c = bbTune(0.6);
    for (MelodyNote& n : c.melody) {
        n.pitch -= 5;
    }
    for (ChordSymbol& ch : c.chords) {
        ch.rootPc = pc(ch.rootPc - 5);
        ch.rootTpc -= 1;
    }
    for (size_t i = 7; i < c.melody.size(); i += 8) {
        c.melody[i].rest = true;   // a breath every two bars
    }
    const auto voiced = voiceMelody(c.melody, c.chords, {});
    for (SaxTechnique t : { SaxTechnique::Unison, SaxTechnique::OctaveUnison4Up1Down, SaxTechnique::OctaveUnison2Up3Down,
                            SaxTechnique::TwoPart, SaxTechnique::Closed, SaxTechnique::Drop2, SaxTechnique::Drop2And4,
                            SaxTechnique::FourPartDrop2, SaxTechnique::Drop2BariRoots, SaxTechnique::DoubleTrombones }) {
        OrchestrationOptions o;
        o.saxes = t;
        const auto s = orchestrate(c.melody, voiced, o);
        const Ranges rg;
        for (size_t i = 0; i < s.size(); ++i) {
            if (c.melody[i].rest) {
                EXPECT_EQ(s[i].saxes, (P5 { -1, -1, -1, -1, -1 }));
                continue;
            }
            EXPECT_EQ(s[i].outOfRange & 0x1F00u, 0u) << "technique " << int(t) << " note " << i;
            for (size_t j = 0; j < 5; ++j) {
                const int p = s[i].saxes[j];
                if (p >= 0) {
                    EXPECT_GE(p, rg.saxLow[j]) << "technique " << int(t) << " note " << i << " sax " << j;
                    EXPECT_LE(p, rg.saxHigh[j]) << "technique " << int(t) << " note " << i << " sax " << j;
                }
            }
            // the sax lead keeps the melody's shape (an octave transposition at most)
            if (t != SaxTechnique::DoubleTrombones) {
                EXPECT_EQ(pc(s[i].saxes[0]), pc(c.melody[i].pitch));
            }
        }
    }
}

TEST(Jubelle_HarmonyRules, OutOfRangeNotesAreFlagged)
{
    // a line spanning more than two octaves can't be played in unison by altos and tenors at any octave:
    // the notes that don't fit are flagged (for the dialog and Proofread), never silently moved
    Chorus c;
    c.chords = { parseChordSymbol("C6") };
    for (int p : { 84, 79, 76, 72, 67, 64, 60, 55 }) {
        c.melody.push_back(note(p, 0));
    }
    const auto voiced = voiceMelody(c.melody, c.chords, {});
    OrchestrationOptions o;
    o.saxes = SaxTechnique::Unison;
    const auto s = orchestrate(c.melody, voiced, o);
    const Ranges rg;
    int flagged = 0;
    for (const SectionNotes& x : s) {
        for (size_t j = 0; j < 5; ++j) {
            const bool out = x.saxes[j] >= 0 && (x.saxes[j] < rg.saxLow[j] || x.saxes[j] > rg.saxHigh[j]);
            EXPECT_EQ(out, bool((x.outOfRange >> (8 + j)) & 1u));
            flagged += out ? 1 : 0;
        }
    }
    EXPECT_GT(flagged, 0);
}

TEST(Jubelle_HarmonyRules, ShoutChorusRealistic)
{
    Chorus c = bbTune(0.4);
    for (MelodyNote& n : c.melody) {
        n.pitch += 7;   // the same tune a fifth higher: the lead goes over A5
    }
    for (ChordSymbol& ch : c.chords) {
        ch.rootPc = pc(ch.rootPc + 7);
        ch.rootTpc += 1;
    }
    const auto voiced = voiceMelody(c.melody, c.chords, {});
    OrchestrationOptions o;
    o.trumpets = TrumpetTechnique::Automatic;
    const auto s = orchestrate(c.melody, voiced, o);
    const Ranges rg;
    for (size_t i = 0; i < s.size(); ++i) {
        EXPECT_EQ(s[i].trumpets[3], c.melody[i].pitch - 12) << "triads: trumpet 4 doubles the lead, note " << i;
        EXPECT_LE(s[i].trombones[0], rg.leadTromboneStrongHigh) << "note " << i;
        EXPECT_LT(s[i].trombones[0], s[i].trumpets[3]) << "note " << i;
        EXPECT_EQ(s[i].outOfRange & 0xF0u, 0u) << "note " << i;
    }
}

TEST(Jubelle_HarmonyRules, LargePassageIsFast)
{
    // 2,000 notes (about 250 bars of eighths) voiced and orchestrated
    Chorus base = bbTune(0.3);
    std::vector<ChordSymbol> chords;
    std::vector<MelodyNote> melody;
    for (int r = 0; r < 63; ++r) {
        for (const ChordSymbol& ch : base.chords) {
            chords.push_back(ch);
        }
        for (MelodyNote n : base.melody) {
            n.chordIndex += r * 8;
            melody.push_back(n);
        }
    }
    const auto t0 = std::chrono::steady_clock::now();
    const auto voiced = voiceMelody(melody, chords, {});
    const auto s = orchestrate(melody, voiced, {});
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    EXPECT_EQ(s.size(), melody.size());
    EXPECT_LT(ms, 500.0) << "took " << ms << " ms";
    std::printf("[harmony] %zu notes voiced and orchestrated in %.1f ms\n", melody.size(), ms);
}
