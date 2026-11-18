/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <filesystem>
#include <memory>
#include <set>

#include "engraving/compat/mscxcompat.h"
#include "engraving/compat/scoreaccess.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/key.h"
#include "engraving/dom/tempotext.h"
#include "engraving/editing/transpose.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/pitchspelling.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/select.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/tie.h"
#include "engraving/infrastructure/localfileinfoprovider.h"

#include "notation/internal/jubelle/harmony/harmonyrunner.h"
#include "notation/internal/jubelle/proofread.h"

#include "templatebuilder.h"

using namespace mu::engraving;
namespace hr = mu::notation::jubelle::harmony;

namespace {
const std::string ROOT = std::string(MU_ROOT_DIR) + "/";
constexpr int Q = Constants::DIVISION;   // a quarter note

// Big Band template staves
constexpr staff_idx_t ALTO1 = 0, BARI = 4, TPT1 = 5, TPT2 = 6, TBN1 = 9, TBN2 = 10, BTBN = 12, PIANO = 13, GUITAR = 14;

std::unique_ptr<MasterScore> bigBand()
{
    jubelle_templates::loadInstrumentData();
    std::unique_ptr<MasterScore> s(jubelle_templates::readTemplate(ROOT + "share/templates/05-Jazz/02-Big_Band/02-Big_Band.mscx"));
    if (s) {
        s->doLayout();
    }
    return s;
}

MasterScore* loadChart(const std::string& path)
{
    MasterScore* s = compat::ScoreAccess::createMasterScoreWithBaseStyle(nullptr);
    s->setFileInfoProvider(std::make_shared<LocalFileInfoProvider>(muse::io::path_t(path)));
    if (!compat::loadMsczOrMscx(s, muse::io::path_t(path), false)) {
        delete s;
        return nullptr;
    }
    return s;
}

//! writes notes (pitch, quarters) one after another on a staff from bar 1, and chord symbols on the piano
void writeLine(Score* s, staff_idx_t staff, const std::vector<std::pair<int, int>>& notes)
{
    Fraction tick(0, 1);
    for (auto [pitch, quarters] : notes) {
        Segment* seg = s->tick2segment(tick, true, SegmentType::ChordRest);
        if (pitch < 0) {
            s->setNoteRest(seg, staff * VOICES, NoteVal(), Fraction(quarters, 4));
        } else {
            s->setNoteRest(seg, staff * VOICES, NoteVal(pitch), Fraction(quarters, 4));
        }
        tick += Fraction(quarters, 4);
    }
}

void addChord(Score* s, staff_idx_t staff, int quarter, const char* text)
{
    Segment* seg = s->tick2segment(Fraction::fromTicks(quarter * Q), true, SegmentType::ChordRest);
    Harmony* h = Factory::createHarmony(seg);
    h->setTrack(staff * VOICES);
    h->setParent(seg);
    h->setHarmonyType(HarmonyType::STANDARD);
    h->setHarmony(muse::String::fromUtf8(text));
    s->undoAddElement(h);
}

void selectBars(Score* s, staff_idx_t staff, int firstBar, int lastBar)
{
    Measure* m1 = s->firstMeasure();
    for (int i = 1; i < firstBar; ++i) {
        m1 = m1->nextMeasure();
    }
    Measure* m2 = m1;
    for (int i = firstBar; i < lastBar; ++i) {
        m2 = m2->nextMeasure();
    }
    s->deselectAll();
    s->select(m1, SelectType::RANGE, staff);
    s->select(m2, SelectType::RANGE, staff);
}

struct Played {
    Fraction tick;
    int pitch;
    int tpc1;
    int tpc2;
    bool tiedBack;
};

std::vector<Played> notesOn(Score* s, staff_idx_t staff)
{
    std::vector<Played> out;
    for (Segment* seg = s->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        EngravingItem* e = seg->element(staff * VOICES);
        if (e && e->isChord()) {
            for (Note* n : toChord(e)->notes()) {
                out.push_back({ seg->tick(), n->pitch(), n->tpc1(), n->tpc2(), n->tieBack() != nullptr });
            }
        }
    }
    return out;
}

std::vector<std::string> chordTexts(Score* s, staff_idx_t staff)
{
    std::vector<std::string> out;
    for (Segment* seg = s->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (EngravingItem* a : seg->annotations()) {
            if (a->isHarmony() && a->staffIdx() == staff) {
                out.push_back(toHarmony(a)->harmonyName().toStdString());
            }
        }
    }
    return out;
}

bool niceWritten(int tpc)
{
    return tpc >= 8 && tpc <= 24;   // no F♭, C♭, E♯, B♯ and no double accidentals
}

int pc(int p) { return ((p % 12) + 12) % 12; }

//! Bars 1–4 of a medium-swing tune in B♭ on Trumpet 1: | Cm7 | F7 | B♭maj7 | G7 |, quarter notes
void tuneInBb(Score* s)
{
    writeLine(s, TPT1, { { 75, 1 }, { 74, 1 }, { 72, 1 }, { 70, 1 },
                   { 69, 1 }, { 70, 1 }, { 72, 1 }, { 75, 1 },
                   { 74, 1 }, { 72, 1 }, { 69, 1 }, { 70, 1 },
                   { 71, 1 }, { 74, 1 }, { 77, 1 }, { 76, 1 } });
    addChord(s, PIANO, 0, "Cm7");
    addChord(s, PIANO, 4, "F7");
    addChord(s, PIANO, 8, "Bbmaj7");
    addChord(s, PIANO, 12, "G7");
    addChord(s, GUITAR, 0, "Cm7");
    addChord(s, GUITAR, 4, "F7");
    addChord(s, GUITAR, 8, "Bbmaj7");
    addChord(s, GUITAR, 12, "G7");
}
}

TEST(Jubelle_HarmonyRunner, FindsTheBigBandSections)
{
    auto s = bigBand();
    ASSERT_TRUE(s);
    const hr::SectionStaves st = hr::findSections(s.get());
    EXPECT_EQ(st.saxes, (std::array<staff_idx_t, 5> { 0, 1, 2, 3, 4 }));
    EXPECT_EQ(st.trumpets, (std::array<staff_idx_t, 4> { 5, 6, 7, 8 }));
    EXPECT_EQ(st.trombones, (std::array<staff_idx_t, 4> { 9, 10, 11, 12 })) << "the bass trombone is trombone 4";
    EXPECT_EQ(st.chordSymbols, PIANO) << "chord symbols go on the piano staff";
}

TEST(Jubelle_HarmonyRunner, FullBandOnTheBigBandTemplate)
{
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("test"));
    tuneInBb(s.get());
    s->endCmd();
    const std::vector<Played> melodyBefore = notesOn(s.get(), TPT1);
    selectBars(s.get(), TPT1, 1, 4);

    hr::RunOptions o;
    const hr::Scope sc = hr::scope(s.get(), o);
    ASSERT_TRUE(sc.ok) << sc.problem;
    EXPECT_EQ(sc.melodyNotes, 16);
    EXPECT_EQ(sc.chordSymbols, 4);
    EXPECT_EQ(sc.firstBar, 1);
    EXPECT_EQ(sc.lastBar, 4);
    EXPECT_EQ(sc.notesWithoutChord, 0);

    s->startCmd(muse::TranslatableString::untranslatable("Harmonise melody"));
    const hr::Report r = hr::run(s.get(), o);
    s->endCmd();
    ASSERT_TRUE(r.ok) << r.problem;
    EXPECT_EQ(r.stavesWritten, 12) << "every horn but the lead";
    EXPECT_EQ(r.outOfRange, 0);

    // the melody is untouched
    const std::vector<Played> melodyAfter = notesOn(s.get(), TPT1);
    ASSERT_EQ(melodyAfter.size(), melodyBefore.size());
    for (size_t i = 0; i < melodyAfter.size(); ++i) {
        EXPECT_EQ(melodyAfter[i].pitch, melodyBefore[i].pitch);
    }

    std::map<staff_idx_t, std::vector<Played>> parts;
    for (staff_idx_t st = 0; st <= BTBN; ++st) {
        parts[st] = notesOn(s.get(), st);
        EXPECT_EQ(parts[st].size(), 16u) << "staff " << st << " has the melody's rhythm";
    }
    const char* chordsByBar[] = { "Cm7", "F7", "Bbmaj7", "G7" };
    for (size_t i = 0; i < 16; ++i) {
        const int lead = parts[TPT1][i].pitch;
        const hr::ChordSymbol chord = hr::parseChordSymbol(chordsByBar[i / 4]);
        // trumpets: a closed voicing under the lead, at least a minor third below it, within an octave
        EXPECT_GE(lead - parts[TPT2][i].pitch, 3) << "note " << i;
        for (staff_idx_t t = TPT2; t <= 8; ++t) {
            EXPECT_LT(parts[t][i].pitch, parts[t - 1][i].pitch) << "note " << i;
            EXPECT_GT(parts[t][i].pitch, lead - 12) << "note " << i;
        }
        // trombones an octave below the trumpets; altos and tenors on the trombone notes
        for (int k = 0; k < 4; ++k) {
            EXPECT_EQ(parts[TBN1 + k][i].pitch, parts[TPT1 + k][i].pitch - 12) << "note " << i;
            EXPECT_EQ(pc(parts[ALTO1 + k][i].pitch), pc(parts[TBN1 + k][i].pitch)) << "note " << i;
        }
        // the baritone on the written root
        EXPECT_EQ(pc(parts[BARI][i].pitch), chord.rootPc) << "note " << i;
        // spelling: what each player reads has no B♯, C♭, E♯, F♭ or double accidentals
        for (staff_idx_t st = 0; st <= BTBN; ++st) {
            EXPECT_TRUE(niceWritten(parts[st][i].tpc2)) << "staff " << st << " note " << i << " tpc " << parts[st][i].tpc2;
            EXPECT_EQ(hr::tpcToPc(parts[st][i].tpc1), pc(parts[st][i].pitch));
        }
        // every pitch within the player's professional range
        for (staff_idx_t st = 0; st <= BTBN; ++st) {
            const Instrument* ins = s->staff(st)->part()->instrument(Fraction(0, 1));
            EXPECT_GE(parts[st][i].pitch, ins->minPitchP()) << "staff " << st << " note " << i;
            EXPECT_LE(parts[st][i].pitch, ins->maxPitchP()) << "staff " << st << " note " << i;
        }
    }
    // the C minor 7 over E♭ (bar 1, beat 1): E♭–C–B♭–G per lesson 1 (3rd in the lead, root second)
    EXPECT_EQ(parts[TPT2][0].pitch, 72);
    // a transposing part reads in its own key: alto 1 written a major sixth above concert
    EXPECT_NE(parts[ALTO1][0].tpc1, parts[ALTO1][0].tpc2);

    // the chord symbols are unchanged (no alterations were asked for)
    EXPECT_EQ(chordTexts(s.get(), PIANO), (std::vector<std::string> { "Cm7", "F7", "Bbmaj7", "G7" }));

    // one Undo removes everything
    s->undoRedo(true, nullptr);
    for (staff_idx_t st = 0; st <= BTBN; ++st) {
        if (st != TPT1) {
            EXPECT_TRUE(notesOn(s.get(), st).empty()) << "staff " << st;
        }
    }
    EXPECT_EQ(notesOn(s.get(), TPT1).size(), melodyBefore.size());
    s->undoRedo(false, nullptr);
    EXPECT_EQ(notesOn(s.get(), ALTO1).size(), 16u);
}

TEST(Jubelle_HarmonyRunner, KeepsAStaffThatAlreadyHasNotes)
{
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("test"));
    tuneInBb(s.get());
    writeLine(s.get(), TBN2, { { 50, 4 } });   // something of yours in Trombone 2, bar 1
    s->endCmd();
    selectBars(s.get(), TPT1, 1, 4);
    hr::RunOptions o;
    o.existing = hr::ExistingNotes::KeepExisting;
    s->startCmd(muse::TranslatableString::untranslatable("Harmonise melody"));
    const hr::Report r = hr::run(s.get(), o);
    s->endCmd();
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.stavesKept.size(), 1u);
    const std::vector<Played> tb2 = notesOn(s.get(), TBN2);
    ASSERT_EQ(tb2.size(), 1u);
    EXPECT_EQ(tb2[0].pitch, 50);
    EXPECT_EQ(notesOn(s.get(), TBN1).size(), 16u);
}

TEST(Jubelle_HarmonyRunner, SectionsCanBeLeftOutAndTechniquesChosen)
{
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("test"));
    tuneInBb(s.get());
    s->endCmd();
    selectBars(s.get(), TPT1, 1, 4);
    hr::RunOptions o;
    o.orchestration.trombones = hr::TromboneTechnique::LeaveOut;
    o.orchestration.saxes = hr::SaxTechnique::OctaveUnison4Up1Down;
    s->startCmd(muse::TranslatableString::untranslatable("Harmonise melody"));
    const hr::Report r = hr::run(s.get(), o);
    s->endCmd();
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(notesOn(s.get(), TBN1).empty()) << "trombones left out";
    const auto a1 = notesOn(s.get(), ALTO1);
    const auto bari = notesOn(s.get(), BARI);
    const auto lead = notesOn(s.get(), TPT1);
    ASSERT_EQ(a1.size(), 16u);
    ASSERT_EQ(bari.size(), 16u);
    for (size_t i = 0; i < 16; ++i) {
        EXPECT_EQ(pc(a1[i].pitch), pc(lead[i].pitch)) << "altos and tenors in unison on the melody";
        EXPECT_EQ(bari[i].pitch, a1[i].pitch - 12) << "the baritone an octave below";
        EXPECT_EQ(notesOn(s.get(), 3)[i].pitch, a1[i].pitch);
    }
}

TEST(Jubelle_HarmonyRunner, AlterationsUpdateTheChordSymbols)
{
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("test"));
    // | Dm7 F | G7 B | Cmaj7 C |  (concert)
    writeLine(s.get(), TPT1, { { 77, 4 }, { 71, 4 }, { 72, 4 } });
    addChord(s.get(), PIANO, 0, "Dm7");
    addChord(s.get(), PIANO, 4, "G7");
    addChord(s.get(), PIANO, 8, "Cmaj7");
    s->endCmd();
    selectBars(s.get(), TPT1, 1, 3);
    hr::RunOptions o;
    o.harmony.addAlterations = true;
    s->startCmd(muse::TranslatableString::untranslatable("Harmonise melody"));
    const hr::Report r = hr::run(s.get(), o);
    s->endCmd();
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.chordSymbolsChanged, 1);
    EXPECT_EQ(chordTexts(s.get(), PIANO), (std::vector<std::string> { "Dm7", "G7b9#5", "Cmaj7" }))
        << "the rhythm section's symbol says what the horns play";
    // the horns play the ♭9 (A♭) and ♯5 (D♯) in bar 2
    std::set<int> bar2;
    for (staff_idx_t st = 0; st <= BTBN; ++st) {
        for (const Played& p : notesOn(s.get(), st)) {
            if (p.tick == Fraction(1, 1)) {
                bar2.insert(pc(p.pitch));
            }
        }
    }
    EXPECT_TRUE(bar2.count(8)) << "♭9";
    EXPECT_TRUE(bar2.count(3)) << "♯5";
    s->undoRedo(true, nullptr);
    EXPECT_EQ(chordTexts(s.get(), PIANO), (std::vector<std::string> { "Dm7", "G7", "Cmaj7" }));
}

TEST(Jubelle_HarmonyRunner, HalfStepSlideOnTheRhythmSection)
{
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("test"));
    writeLine(s.get(), TPT1, { { 77, 4 }, { 77, 4 }, { 76, 4 } });
    addChord(s.get(), PIANO, 0, "Dm7");
    addChord(s.get(), PIANO, 4, "G7");
    addChord(s.get(), PIANO, 8, "Cmaj7");
    s->endCmd();
    selectBars(s.get(), TPT1, 1, 3);
    hr::RunOptions o;
    o.harmony.halfStepSlide = true;
    s->startCmd(muse::TranslatableString::untranslatable("Harmonise melody"));
    ASSERT_TRUE(hr::run(s.get(), o).ok);
    s->endCmd();
    EXPECT_EQ(chordTexts(s.get(), PIANO), (std::vector<std::string> { "Dm7", "Db7", "Cmaj7" }));
    // and the bari plays D♭ under it
    const auto bari = notesOn(s.get(), BARI);
    ASSERT_EQ(bari.size(), 3u);
    EXPECT_EQ(pc(bari[1].pitch), 1);
}

TEST(Jubelle_HarmonyRunner, TiesAndRestsFollowTheMelody)
{
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("test"));
    // a dotted half tied over the bar line, then a rest
    writeLine(s.get(), TPT1, { { 76, 2 }, { 74, 2 }, { 72, 2 }, { -1, 2 } });
    addChord(s.get(), PIANO, 0, "C6");
    // tie the D (beat 3, bar 1) into the C? no: tie the D to a D in bar 2
    s->endCmd();
    s->startCmd(muse::TranslatableString::untranslatable("tie"));
    {
        Segment* seg = s->tick2segment(Fraction(2, 4), true, SegmentType::ChordRest);
        Chord* d = toChord(seg->element(TPT1 * VOICES));
        Segment* seg2 = s->tick2segment(Fraction(4, 4), true, SegmentType::ChordRest);
        Chord* c = toChord(seg2->element(TPT1 * VOICES));
        s->undoChangePitch(c->upNote(), 74, 16, 18);
        Tie* tie = Factory::createTie(s->dummy());
        tie->setStartNote(d->upNote());
        tie->setEndNote(c->upNote());
        tie->setTrack(d->track());
        s->undoAddElement(tie);
    }
    s->endCmd();
    selectBars(s.get(), TPT1, 1, 2);
    hr::RunOptions o;
    s->startCmd(muse::TranslatableString::untranslatable("Harmonise melody"));
    const hr::Report r = hr::run(s.get(), o);
    s->endCmd();
    ASSERT_TRUE(r.ok);
    for (staff_idx_t st : { ALTO1, TPT2, TBN1, BARI }) {
        const auto n = notesOn(s.get(), st);
        ASSERT_EQ(n.size(), 3u) << "staff " << st << ": the rest stays a rest";
        EXPECT_TRUE(n[2].tiedBack) << "staff " << st;
        EXPECT_EQ(n[2].pitch, n[1].pitch) << "staff " << st << ": a tie holds one pitch";
    }
}

TEST(Jubelle_HarmonyRunner, ShoutChorusOnTheTemplate)
{
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("test"));
    writeLine(s.get(), TPT1, { { 84, 1 }, { 82, 1 }, { 79, 1 }, { 77, 1 }, { 82, 4 } });
    addChord(s.get(), PIANO, 0, "Bb6");
    addChord(s.get(), PIANO, 4, "Eb7");
    s->endCmd();
    selectBars(s.get(), TPT1, 1, 2);
    hr::RunOptions o;
    o.orchestration.trumpets = hr::TrumpetTechnique::Automatic;
    s->startCmd(muse::TranslatableString::untranslatable("Harmonise melody"));
    const hr::Report r = hr::run(s.get(), o);
    s->endCmd();
    ASSERT_TRUE(r.ok);
    const auto t1 = notesOn(s.get(), TPT1);
    const auto t4 = notesOn(s.get(), 8);
    const auto tb1 = notesOn(s.get(), TBN1);
    for (size_t i = 0; i < t1.size(); ++i) {
        EXPECT_EQ(t4[i].pitch, t1[i].pitch - 12) << "trumpet triads: 1 and 4 in octaves";
        EXPECT_LE(tb1[i].pitch, 69) << "trombone 1 at or below the A on top of the bass staff";
        EXPECT_LT(tb1[i].pitch, t4[i].pitch);
    }
    EXPECT_EQ(r.outOfRange, 0);
}

TEST(Jubelle_HarmonyRunner, WithoutChordSymbolsTheRhythmSectionIsRead)
{
    // no chord symbols, but the piano plays C E G B♭ (C7): the Chord Identifier names it
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("test"));
    writeLine(s.get(), TPT1, { { 76, 2 }, { 79, 2 } });
    Segment* seg = s->firstMeasure()->first(SegmentType::ChordRest);
    const int voicing[4] = { 48, 52, 55, 58 };
    for (voice_idx_t v = 0; v < VOICES; ++v) {
        s->setNoteRest(seg, PIANO * VOICES + v, NoteVal(voicing[v]), Fraction(1, 1));
    }
    s->endCmd();
    selectBars(s.get(), TPT1, 1, 1);
    const hr::Scope sc = hr::scope(s.get(), {});
    ASSERT_TRUE(sc.ok) << sc.problem;
    EXPECT_EQ(sc.chordSymbols, 0);
    EXPECT_EQ(sc.identifiedChords, 1);
    EXPECT_EQ(sc.notesWithoutChord, 0);
    s->startCmd(muse::TranslatableString::untranslatable("Harmonise melody"));
    const hr::Report r = hr::run(s.get(), {});
    s->endCmd();
    ASSERT_TRUE(r.ok);
    const hr::ChordSymbol c7 = hr::parseChordSymbol("C7");
    for (staff_idx_t st : { TPT2, staff_idx_t(7), staff_idx_t(8), BARI }) {
        for (const Played& p : notesOn(s.get(), st)) {
            EXPECT_TRUE(hr::isChordTone(p.pitch, c7)) << "staff " << st << " pitch " << p.pitch;
        }
    }
    EXPECT_TRUE(chordTexts(s.get(), PIANO).empty()) << "no chord symbols are invented";
    EXPECT_EQ(r.chordSymbolsWritten + r.chordSymbolsChanged, 0);
}

TEST(Jubelle_HarmonyRunner, NoSelectionExplainsWhatToDo)
{
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->deselectAll();
    const hr::Scope sc = hr::scope(s.get(), {});
    EXPECT_FALSE(sc.ok);
    EXPECT_FALSE(sc.problem.empty());
    s->startCmd(muse::TranslatableString::untranslatable("Harmonise melody"));
    EXPECT_FALSE(hr::run(s.get(), {}).ok);
    s->endCmd();
}

TEST(Jubelle_HarmonyRunner, TheBigBandTestChart)
{
    // the realistic 40-bar test chart: its lead trumpet line over its own 55 chord changes, full band
    std::unique_ptr<MasterScore> s(loadChart(ROOT + "tools/jubelle_templates/testscores/Big_Band_test_music.mscz"));
    ASSERT_TRUE(s);
    s->doLayout();
    std::map<staff_idx_t, size_t> before;
    for (staff_idx_t st = 0; st < s->nstaves(); ++st) {
        before[st] = notesOn(s.get(), st).size();
    }
    const auto lead = notesOn(s.get(), TPT1);
    selectBars(s.get(), TPT1, 1, s->nmeasures());
    hr::RunOptions o;
    const hr::Scope sc = hr::scope(s.get(), o);
    ASSERT_TRUE(sc.ok) << sc.problem;
    EXPECT_GE(sc.chordSymbols, 40);

    const auto t0 = std::chrono::steady_clock::now();
    s->startCmd(muse::TranslatableString::untranslatable("Harmonise melody"));
    const hr::Report r = hr::run(s.get(), o);
    s->endCmd();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ASSERT_TRUE(r.ok) << r.problem;
    std::printf("[harmony] test chart: %d notes in %d staves, %d non-chord tones (%d planed, %d diatonic, %d diminished), "
                "%d out of range, %.0f ms including layout\n",
                r.notesWritten, r.stavesWritten, r.nonChordTones, r.planed, r.diatonic, r.diminished, r.outOfRange, ms);
    EXPECT_EQ(r.stavesWritten, 12);
    EXPECT_LT(ms, 5000.0);
    // the melody is unchanged
    const auto leadAfter = notesOn(s.get(), TPT1);
    ASSERT_EQ(leadAfter.size(), lead.size());
    for (size_t i = 0; i < lead.size(); ++i) {
        EXPECT_EQ(leadAfter[i].pitch, lead[i].pitch);
    }
    // whatever the report says is out of range, really is; everything else is within range
    int outOfRange = 0;
    int unusual = 0;
    for (staff_idx_t st = 0; st <= BTBN; ++st) {
        const Instrument* ins = s->staff(st)->part()->instrument(Fraction(0, 1));
        for (const Played& p : notesOn(s.get(), st)) {
            if (st != TPT1 && !p.tiedBack) {
                outOfRange += (p.pitch < ins->minPitchP() || p.pitch > ins->maxPitchP()) ? 1 : 0;
            }
            // never a double accidental; B♯, C♭, E♯ or F♭ only where a step would otherwise look wrong
            EXPECT_TRUE(p.tpc2 >= 6 && p.tpc2 <= 26) << "staff " << st << " tpc " << p.tpc2;
            unusual += niceWritten(p.tpc2) ? 0 : 1;
        }
    }
    std::printf("[harmony] B♯/C♭/E♯/F♭ written: %d\n", unusual);
    EXPECT_LE(unusual, 3);
    EXPECT_EQ(outOfRange, r.outOfRange);
    // chord-tone accuracy: under a melody note that belongs to its chord, every horn plays a tone of that chord
    std::map<Fraction, hr::ChordSymbol> chordAt;
    for (Segment* seg = s->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (EngravingItem* a : seg->annotations()) {
            if (a->isHarmony() && a->staffIdx() == PIANO) {
                chordAt[seg->tick()] = hr::parseChordSymbol(toHarmony(a)->harmonyName().toStdString());
            }
        }
    }
    auto chordFor = [&chordAt](const Fraction& t) {
        hr::ChordSymbol c;
        for (auto& [tick, ch] : chordAt) {
            if (tick <= t) {
                c = ch;
            }
        }
        return c;
    };
    std::map<Fraction, std::vector<int>> hornsAt;
    for (staff_idx_t st = 0; st <= BTBN; ++st) {
        for (const Played& p : notesOn(s.get(), st)) {
            hornsAt[p.tick].push_back(p.pitch);
        }
    }
    int checked = 0;
    for (const Played& m : leadAfter) {
        const hr::ChordSymbol c = chordFor(m.tick);
        if (m.tiedBack || !c.valid || !hr::isChordTone(m.pitch, c)) {
            continue;
        }
        ++checked;
        for (int p : hornsAt[m.tick]) {
            EXPECT_TRUE(hr::isChordTone(p, c)) << "bar tick " << m.tick.ticks() << " pitch " << p << " over " << c.text << " lead " << m.pitch;
        }
        // and the closed voicing keeps a minor third under the lead
        const auto t2 = notesOn(s.get(), TPT2);
        for (const Played& q : t2) {
            if (q.tick == m.tick) {
                EXPECT_GE(m.pitch - q.pitch, 3);
            }
        }
    }
    EXPECT_GT(checked, 50);
    // Proofread finds no spelling that looks wrong in the new parts (lesson 8), and no range problems
    {
        mu::notation::jubelle::ProofreadOptions po;
        po.categories = { mu::notation::jubelle::CheckCategory::Spelling, mu::notation::jubelle::CheckCategory::Range };
        int looks = 0;
        int range = 0;
        for (const auto& f : mu::notation::jubelle::proofread(s.get(), po)) {
            if (f.staffIdx > BTBN || f.staffIdx == TPT1) {
                continue;
            }
            looks += f.message.contains(u"looks like") ? 1 : 0;
            range += f.category == mu::notation::jubelle::CheckCategory::Range && f.severity == mu::notation::jubelle::Severity::Error ? 1 : 0;
        }
        std::printf("[harmony] Proofread on the new parts: %d spelling (%d unavoidable, reported), %d range\n", looks,
                    r.awkwardSpellings, range);
        EXPECT_LE(looks, r.awkwardSpellings);
        EXPECT_LE(looks, 3);
        EXPECT_EQ(range, r.outOfRange);
    }
    // the rhythm section is untouched
    for (staff_idx_t st = PIANO; st < s->nstaves(); ++st) {
        EXPECT_EQ(notesOn(s.get(), st).size(), before[st]) << "staff " << st;
    }
    // undo puts the chart back exactly as it was
    s->undoRedo(true, nullptr);
    for (staff_idx_t st = 0; st < s->nstaves(); ++st) {
        EXPECT_EQ(notesOn(s.get(), st).size(), before[st]) << "staff " << st;
    }
}


TEST(Jubelle_HarmonyRunner, DiatonicPassingChordInTheScore)
{
    // over C6: E | F A — F leaps to A, so it is re-harmonised as the 7th of G7 (lesson 3)
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("test"));
    writeLine(s.get(), TPT1, { { 76, 2 }, { 77, 1 }, { 81, 1 } });
    addChord(s.get(), PIANO, 0, "C6");
    s->endCmd();
    selectBars(s.get(), TPT1, 1, 1);
    s->startCmd(muse::TranslatableString::untranslatable("Harmonise melody"));
    const hr::Report r = hr::run(s.get(), {});
    s->endCmd();
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.diatonic, 1);
    const hr::ChordSymbol g7 = hr::parseChordSymbol("G7");
    for (staff_idx_t st = TPT2; st <= 8; ++st) {
        const auto n = notesOn(s.get(), st);
        ASSERT_EQ(n.size(), 3u);
        EXPECT_TRUE(hr::isChordTone(n[1].pitch, g7)) << "staff " << st;
    }
    // the baritone stays on the written root under the passing chord
    EXPECT_EQ(pc(notesOn(s.get(), BARI)[1].pitch), 0);
}

TEST(Jubelle_HarmonyRunner, NonChordTonesAreSpelledAsTheyMove)
{
    // over C6: E F | E D C♯ D — the F falls a half step into E and the C♯ rises into D: both plane (lesson 3)
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("test"));
    writeLine(s.get(), TPT1, { { 76, 2 }, { 77, 2 }, { 76, 1 }, { 74, 1 }, { 73, 1 }, { 74, 1 } });
    addChord(s.get(), PIANO, 0, "C6");
    {
        // the melody's C♯ as you would write it, rising into D
        Chord* cs = toChord(s->tick2segment(Fraction(6, 4), true, SegmentType::ChordRest)->element(TPT1 * VOICES));
        s->undoChangePitch(cs->upNote(), 73, 21, 23);
    }
    s->endCmd();
    selectBars(s.get(), TPT1, 1, 2);
    hr::RunOptions o;
    s->startCmd(muse::TranslatableString::untranslatable("Harmonise melody"));
    const hr::Report r = hr::run(s.get(), o);
    s->endCmd();
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.planed, 2);
    EXPECT_EQ(r.nonChordTones, 2);
    auto letter = [](int tpc) {
        static const int byFifth[7] = { 3, 0, 4, 1, 5, 2, 6 };   // F C G D A E B → C=0 … B=6
        return byFifth[((tpc + 1) % 7 + 7) % 7];
    };
    for (staff_idx_t st : { TPT2, staff_idx_t(7), staff_idx_t(8), TBN1, ALTO1, staff_idx_t(2) }) {
        const auto n = notesOn(s.get(), st);
        ASSERT_EQ(n.size(), 6u) << "staff " << st;
        // the planed chord (5th note) moves up a half step into the 6th: each voice looks like a second
        EXPECT_EQ(n[5].pitch - n[4].pitch, 1) << "staff " << st;
        // a rising half step reads as a second or a chromatic step (C to C♯), never as a third
        EXPECT_LE(((letter(n[5].tpc2) - letter(n[4].tpc2)) % 7 + 7) % 7, 1) << "staff " << st;
        // the F (2nd note) falls into the E: each voice a half step above its target, spelled as a second
        EXPECT_EQ(n[1].pitch - n[2].pitch, 1) << "staff " << st;
        EXPECT_LE(((letter(n[1].tpc2) - letter(n[2].tpc2)) % 7 + 7) % 7, 1) << "staff " << st << ": never a third";
    }
}

TEST(Jubelle_HarmonyRunner, LargeArrangementStaysResponsive)
{
    // 192 bars of eighth notes (1,536 melody notes) over a chord per bar, full band
    auto s = bigBand();
    ASSERT_TRUE(s);
    constexpr int BARS = 192;
    s->startCmd(muse::TranslatableString::untranslatable("test"));
    s->appendMeasures(BARS - s->nmeasures());
    s->endCmd();
    ASSERT_EQ(s->nmeasures(), BARS);
    s->startCmd(muse::TranslatableString::untranslatable("test"));
    const char* prog[8] = { "Cm7", "F7", "Bbmaj7", "G7", "Cm7", "F7", "Bb6", "Bb6" };
    const int line[8] = { 75, 74, 72, 70, 69, 70, 72, 74 };
    Fraction tick(0, 1);
    for (int bar = 0; bar < BARS; ++bar) {
        Segment* first = s->tick2segment(tick, true, SegmentType::ChordRest);
        Harmony* h = Factory::createHarmony(first);
        h->setTrack(PIANO * VOICES);
        h->setParent(first);
        h->setHarmony(muse::String::fromUtf8(prog[bar % 8]));
        s->undoAddElement(h);
        for (int k = 0; k < 8; ++k) {
            Segment* seg = s->tick2segment(tick, true, SegmentType::ChordRest);
            s->setNoteRest(seg, TPT1 * VOICES, NoteVal(line[(k + bar) % 8]), Fraction(1, 8));
            tick += Fraction(1, 8);
        }
    }
    s->endCmd();
    selectBars(s.get(), TPT1, 1, BARS);
    hr::RunOptions o;
    const auto t0 = std::chrono::steady_clock::now();
    s->startCmd(muse::TranslatableString::untranslatable("Harmonise melody"));
    const hr::Report r = hr::run(s.get(), o);
    const double runMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    s->endCmd();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.notesWritten, 12 * 8 * BARS);
    std::printf("[harmony] %d bars, %d notes written: %.0f ms to write, %.0f ms including layout\n", BARS, r.notesWritten, runMs, ms);
    EXPECT_LT(ms, 20000.0);
    const auto u0 = std::chrono::steady_clock::now();
    s->undoRedo(true, nullptr);
    std::printf("[harmony] undo: %.0f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - u0).count());
    EXPECT_TRUE(notesOn(s.get(), ALTO1).empty());
}

// ---------------------------------------------------------------------------------------------------------------
// A complete 32-bar chart: four sections in two keys and three tempos, each harmonised with a different technique,
// then saved, reopened, undone and redone.
// ---------------------------------------------------------------------------------------------------------------
namespace {
struct Snap {
    staff_idx_t staff;
    int tick;
    voice_idx_t voice;
    int pitch;
    int tpc1;
    int tpc2;
    bool tieFor;
    bool operator==(const Snap& o) const
    {
        return staff == o.staff && tick == o.tick && voice == o.voice && pitch == o.pitch && tpc1 == o.tpc1 && tpc2 == o.tpc2
               && tieFor == o.tieFor;
    }
};

std::vector<Snap> snapshot(Score* s)
{
    std::vector<Snap> out;
    for (Segment* seg = s->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (staff_idx_t st = 0; st < s->nstaves(); ++st) {
            for (voice_idx_t v = 0; v < VOICES; ++v) {
                EngravingItem* e = seg->element(st * VOICES + v);
                if (e && e->isChord()) {
                    for (Note* n : toChord(e)->notes()) {
                        out.push_back({ st, seg->tick().ticks(), v, n->pitch(), n->tpc1(), n->tpc2(), n->tieFor() != nullptr });
                    }
                }
            }
        }
    }
    return out;
}

std::vector<std::string> allChordTexts(Score* s)
{
    std::vector<std::string> out;
    for (Segment* seg = s->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (EngravingItem* a : seg->annotations()) {
            if (a->isHarmony()) {
                out.push_back(std::to_string(seg->tick().ticks()) + ":" + std::to_string(a->staffIdx()) + ":"
                              + toHarmony(a)->harmonyName().toStdString());
            }
        }
    }
    std::sort(out.begin(), out.end());   // the order of annotations on one beat has no meaning
    return out;
}

struct ChartBar {
    std::vector<std::pair<int, int>> notes;   // (concert pitch or -1 for a rest, eighths)
    std::vector<std::pair<int, const char*>> chords;   // (eighth within the bar, symbol)
};

void addTempo(Score* s, int bar, double bpm)
{
    Segment* seg = s->tick2segment(Fraction(bar, 1), true, SegmentType::ChordRest);
    TempoText* t = Factory::createTempoText(seg);
    t->setParent(seg);
    t->setTrack(0);
    t->setXmlText(muse::String::fromStdString("♩ = " + std::to_string(int(bpm))));
    t->setTempo(BeatsPerSecond(bpm / 60.0));
    s->undoAddElement(t);
}

void addKey(Score* s, int bar, Key k)
{
    KeySigEvent ke;
    ke.setConcertKey(k);
    for (Staff* st : s->staves()) {
        if (!st->isDrumStaff(Fraction(0, 1))) {
            s->undoChangeKeySig(st, Fraction(bar, 1), ke);
        }
    }
}

std::unique_ptr<MasterScore> fullChart()
{
    auto s = bigBand();
    if (!s) {
        return s;
    }
    s->startCmd(muse::TranslatableString::untranslatable("chart"));
    if (s->nmeasures() < 32) {
        s->appendMeasures(32 - s->nmeasures());
    }
    s->endCmd();
    // A (bars 1–8): B♭, medium-up swing, full band. Quarter notes (2 eighths), a chromatic approach and rests.
    // B (9–16): E♭, ballad, saxophone soli in Drop 2.
    // C (17–24): B♭, shout chorus up to concert D6.
    // D (25–32): B♭, a fast eighth-note line for the saxes.
    const std::vector<ChartBar> bars {
        { { { 74, 2 }, { 77, 2 }, { 81, 2 }, { 74, 2 } }, { { 0, "Bbmaj7" } } },
        { { { 71, 2 }, { 74, 2 }, { 77, 2 }, { -1, 2 } }, { { 0, "G7" } } },
        { { { 75, 2 }, { 74, 2 }, { 72, 2 }, { 70, 2 } }, { { 0, "Cm7" } } },
        { { { 69, 2 }, { 72, 2 }, { 75, 2 }, { 73, 2 } }, { { 0, "F7" } } },
        { { { 74, 2 }, { 77, 2 }, { 81, 2 }, { 77, 2 } }, { { 0, "Dm7" } } },
        { { { 79, 2 }, { 77, 2 }, { 74, 2 }, { 71, 2 } }, { { 0, "G7" } } },
        { { { 72, 2 }, { 75, 2 }, { 69, 2 }, { 72, 2 } }, { { 0, "Cm7" }, { 4, "F7" } } },
        { { { 74, 4 }, { -1, 4 } }, { { 0, "Bb6" } } },

        { { { 67, 2 }, { 70, 2 }, { 74, 2 }, { 70, 2 } }, { { 0, "Ebmaj7" } } },
        { { { 67, 2 }, { 75, 2 }, { 72, 2 }, { 70, 2 } }, { { 0, "Cm7" } } },
        { { { 68, 2 }, { 72, 2 }, { 75, 2 }, { 72, 2 } }, { { 0, "Fm7" } } },
        { { { 74, 2 }, { 70, 2 }, { 68, 2 }, { 65, 2 } }, { { 0, "Bb7" } } },
        { { { 67, 4 }, { 70, 4 } }, { { 0, "Ebmaj7" } } },
        { { { 72, 2 }, { 75, 2 }, { 66, 2 }, { 72, 2 } }, { { 0, "Ab7" } } },
        { { { 68, 2 }, { 72, 2 }, { 74, 2 }, { 68, 2 } }, { { 0, "Fm7" }, { 4, "Bb7" } } },
        { { { 67, 8 } }, { { 0, "Eb6" } } },

        { { { 86, 2 }, { 82, 2 }, { 77, 2 }, { 79, 2 } }, { { 0, "Bb6" } } },
        { { { 79, 2 }, { 82, 2 }, { 86, 4 } }, { { 0, "Bb6" } } },
        { { { 85, 2 }, { 82, 2 }, { 79, 2 }, { 75, 2 } }, { { 0, "Eb7" } } },
        { { { 79, 2 }, { 82, 2 }, { 85, 4 } }, { { 0, "Eb7" } } },
        { { { 86, 8 } }, { { 0, "Bb6" } } },
        { { { 83, 2 }, { 86, 2 }, { 77, 2 }, { 83, 2 } }, { { 0, "G7" } } },
        { { { 84, 2 }, { 79, 2 }, { 81, 2 }, { 84, 2 } }, { { 0, "Cm7" }, { 4, "F7" } } },
        { { { 82, 8 } }, { { 0, "Bb6" } } },
    };
    std::vector<ChartBar> all = bars;
    const ChartBar fast[4] = {
        { { { 70, 1 }, { 72, 1 }, { 74, 1 }, { 77, 1 }, { 79, 1 }, { 77, 1 }, { 74, 1 }, { 72, 1 } }, { { 0, "Bb6" } } },
        { { { 71, 1 }, { 74, 1 }, { 77, 1 }, { 79, 1 }, { 77, 1 }, { 74, 1 }, { 71, 1 }, { 67, 1 } }, { { 0, "G7" } } },
        { { { 72, 1 }, { 75, 1 }, { 79, 1 }, { 75, 1 }, { 72, 1 }, { 70, 1 }, { 67, 1 }, { 70, 1 } }, { { 0, "Cm7" } } },
        { { { 69, 1 }, { 72, 1 }, { 75, 1 }, { 77, 1 }, { 75, 1 }, { 72, 1 }, { 69, 1 }, { -1, 1 } }, { { 0, "F7" } } },
    };
    for (int k = 0; k < 7; ++k) {
        all.push_back(fast[k % 4]);
    }
    all.push_back({ { { 70, 8 } }, { { 0, "Bb6" } } });

    s->startCmd(muse::TranslatableString::untranslatable("chart"));
    addKey(s.get(), 0, Key::B_B);
    addKey(s.get(), 8, Key::E_B);
    addKey(s.get(), 16, Key::B_B);
    addTempo(s.get(), 0, 160);
    addTempo(s.get(), 8, 100);
    addTempo(s.get(), 16, 160);
    addTempo(s.get(), 24, 240);
    s->endCmd();

    s->startCmd(muse::TranslatableString::untranslatable("chart"));
    for (size_t b = 0; b < all.size(); ++b) {
        Fraction tick(int(b), 1);
        for (auto [pitch, eighths] : all[b].notes) {
            Segment* seg = s->tick2segment(tick, true, SegmentType::ChordRest);
            if (pitch < 0) {
                s->setNoteRest(seg, TPT1 * VOICES, NoteVal(), Fraction(eighths, 8));
            } else {
                // spelled in the key, as you would write it (G♭ in bar 14, C♯ approaching D in bar 4)
                NoteVal nv(pitch);
                const Key key = b >= 8 && b < 16 ? Key::E_B : Key::B_B;
                nv.tpc1 = pitch2tpc(pitch, key, Prefer::NEAREST);
                if (b == 3 && pitch == 73) {
                    nv.tpc1 = int(Tpc::TPC_C_S);
                }
                if (b == 13 && pitch == 66) {
                    nv.tpc1 = int(Tpc::TPC_G_B);
                }
                nv.tpc2 = nv.tpc1;
                s->setNoteRest(seg, TPT1 * VOICES, nv, Fraction(eighths, 8));
            }
            tick += Fraction(eighths, 8);
        }
        for (auto [at, sym] : all[b].chords) {
            addChord(s.get(), PIANO, int(b) * 4 + at / 2, sym);
            addChord(s.get(), GUITAR, int(b) * 4 + at / 2, sym);
        }
    }
    // the rhythm section has something of its own: a bass line on the roots
    s->endCmd();
    s->doLayout();
    return s;
}

//! the concert and written spellings of a note agree with its staff's transposition
void expectTransposition(Score* s, const Snap& n)
{
    const Staff* staff = s->staff(n.staff);
    Interval v = staff->transpose(Fraction::fromTicks(n.tick));
    if (v.isZero()) {
        EXPECT_EQ(n.tpc1, n.tpc2) << "staff " << n.staff << " tick " << n.tick;
        return;
    }
    v.flip();
    EXPECT_EQ(Transpose::transposeTpc(n.tpc1, v, true), n.tpc2) << "staff " << n.staff << " tick " << n.tick;
    EXPECT_EQ(mu::notation::jubelle::harmony::tpcToPc(n.tpc1), pc(n.pitch)) << "staff " << n.staff << " tick " << n.tick;
}

hr::Report harmonise(Score* s, int firstBar, int lastBar, const hr::RunOptions& o)
{
    selectBars(s, TPT1, firstBar, lastBar);
    s->startCmd(muse::TranslatableString::untranslatable("Harmonise melody"));
    hr::Report r = hr::run(s, o);
    s->endCmd();
    return r;
}

std::vector<Snap> inBars(const std::vector<Snap>& all, staff_idx_t staff, int firstBar, int lastBar)
{
    std::vector<Snap> out;
    for (const Snap& n : all) {
        const int bar = n.tick / (4 * Q) + 1;
        if (n.staff == staff && n.voice == 0 && bar >= firstBar && bar <= lastBar) {
            out.push_back(n);
        }
    }
    return out;
}
}

TEST(Jubelle_HarmonyRunner, CompleteChartInTwoKeysAndThreeTempos)
{
    auto s = fullChart();
    ASSERT_TRUE(s);
    const std::vector<Snap> original = snapshot(s.get());
    const std::vector<std::string> originalChords = allChordTexts(s.get());

    // --- A: full band, closed (the proposal's defaults, apart from alterations kept off so the symbols stay put)
    hr::RunOptions a;
    a.harmony.addAlterations = false;
    EXPECT_FALSE(a.harmony.halfStepSlide) << "the half-step slide is off unless you ask";
    const hr::Report ra = harmonise(s.get(), 1, 8, a);
    ASSERT_TRUE(ra.ok) << ra.problem;
    EXPECT_EQ(ra.stavesWritten, 12);
    EXPECT_GE(ra.planed, 1) << "the C♯ into D in bar 4–5";
    EXPECT_EQ(ra.outOfRange, 0);

    // --- B: saxophone soli in Drop 2 at the ballad tempo; no warning, because the notes are long
    hr::RunOptions b;
    b.orchestration.trumpets = hr::TrumpetTechnique::LeaveOut;
    b.orchestration.trombones = hr::TromboneTechnique::LeaveOut;
    b.orchestration.saxes = hr::SaxTechnique::Drop2;
    b.harmony.addAlterations = false;
    selectBars(s.get(), TPT1, 9, 16);
    EXPECT_FALSE(hr::scope(s.get(), b).saxWarning.has_value()) << "ballad quarters are 0.6 s";
    const hr::Report rb = harmonise(s.get(), 9, 16, b);
    ASSERT_TRUE(rb.ok);
    EXPECT_EQ(rb.stavesWritten, 5);
    EXPECT_EQ(rb.outOfRange, 0);

    // --- C: the shout chorus, automatic trumpets (triads above A5), alterations as the proposal sets them
    hr::RunOptions c;
    c.orchestration.trumpets = hr::TrumpetTechnique::Automatic;
    c.harmony.addAlterations = true;
    const hr::Report rc = harmonise(s.get(), 17, 24, c);
    ASSERT_TRUE(rc.ok);
    EXPECT_EQ(rc.outOfRange, 0);

    // --- D: a fast line: the dialog warns against Drop 2, octave unison is chosen instead
    hr::RunOptions d;
    d.orchestration.trumpets = hr::TrumpetTechnique::LeaveOut;
    d.orchestration.trombones = hr::TromboneTechnique::LeaveOut;
    d.orchestration.saxes = hr::SaxTechnique::Drop2;
    selectBars(s.get(), TPT1, 25, 32);
    const hr::Scope sd = hr::scope(s.get(), d);
    EXPECT_TRUE(sd.saxWarning.has_value()) << "eighths at 240 are 0.125 s";
    EXPECT_GT(sd.fastNotes, 40);
    d.orchestration.saxes = hr::SaxTechnique::OctaveUnison2Up3Down;
    EXPECT_FALSE(hr::scope(s.get(), d).saxWarning.has_value());
    const hr::Report rd = harmonise(s.get(), 25, 32, d);
    ASSERT_TRUE(rd.ok);
    EXPECT_EQ(rd.outOfRange, 0);

    const std::vector<Snap> done = snapshot(s.get());

    // the melody and the rhythm section are exactly as they were
    for (staff_idx_t st : { TPT1, PIANO, GUITAR, staff_idx_t(15), staff_idx_t(16) }) {
        std::vector<Snap> before;
        std::vector<Snap> after;
        for (const Snap& n : original) {
            if (n.staff == st) {
                before.push_back(n);
            }
        }
        for (const Snap& n : done) {
            if (n.staff == st) {
                after.push_back(n);
            }
        }
        EXPECT_EQ(after, before) << "staff " << st;
    }

    // every note written: concert and written spelling agree with the instrument; within the professional range
    for (const Snap& n : done) {
        if (n.staff > BTBN || n.staff == TPT1) {
            continue;
        }
        expectTransposition(s.get(), n);
        const Instrument* ins = s->staff(n.staff)->part()->instrument(Fraction::fromTicks(n.tick));
        EXPECT_GE(n.pitch, ins->minPitchP()) << "staff " << n.staff << " tick " << n.tick;
        EXPECT_LE(n.pitch, ins->maxPitchP()) << "staff " << n.staff << " tick " << n.tick;
        EXPECT_TRUE(n.tpc2 >= 6 && n.tpc2 <= 26) << "no double accidentals";
    }

    // A: trumpets closed under the lead, trombones an octave below, saxes on the trombone notes, bari on roots
    {
        const auto t1 = inBars(done, TPT1, 1, 8);
        for (size_t i = 0; i < t1.size(); ++i) {
            const auto t2 = inBars(done, TPT2, 1, 8);
            ASSERT_EQ(t2.size(), t1.size());
            EXPECT_GE(t1[i].pitch - t2[i].pitch, 3);
            EXPECT_GT(inBars(done, 8, 1, 8)[i].pitch, t1[i].pitch - 12);
            for (int k = 0; k < 4; ++k) {
                EXPECT_EQ(inBars(done, TBN1 + k, 1, 8)[i].pitch, inBars(done, TPT1 + k, 1, 8)[i].pitch - 12);
            }
        }
        EXPECT_EQ(inBars(done, ALTO1, 1, 8).size(), t1.size()) << "rests stay rests";
    }
    // B: only the saxes play; Drop 2 puts tenor 2 an octave under the lead; spelled with flats in E♭
    {
        EXPECT_TRUE(inBars(done, TPT2, 9, 16).empty());
        EXPECT_TRUE(inBars(done, TBN1, 9, 16).empty());
        const auto lead = inBars(done, TPT1, 9, 16);
        const auto a1 = inBars(done, ALTO1, 9, 16);
        const auto t2 = inBars(done, 3, 9, 16);
        ASSERT_EQ(a1.size(), lead.size());
        for (size_t i = 0; i < lead.size(); ++i) {
            EXPECT_EQ(pc(a1[i].pitch), pc(lead[i].pitch));
            EXPECT_EQ(t2[i].pitch, a1[i].pitch - 12) << "Drop 2: tenor 2 doubles the lead an octave below";
        }
        for (staff_idx_t st = 0; st <= BARI; ++st) {
            for (const Snap& n : inBars(done, st, 9, 16)) {
                EXPECT_LT(n.tpc1, 20) << "no sharps at concert pitch in E♭ (staff " << st << ")";
            }
        }
    }
    // C: above A5, trumpets 1 and 4 in octaves and trombone 1 in its strong range
    {
        const auto lead = inBars(done, TPT1, 17, 24);
        const auto t4 = inBars(done, 8, 17, 24);
        const auto tb1 = inBars(done, TBN1, 17, 24);
        ASSERT_EQ(t4.size(), lead.size());
        for (size_t i = 0; i < lead.size(); ++i) {
            EXPECT_EQ(t4[i].pitch, lead[i].pitch - 12);
            EXPECT_LE(tb1[i].pitch, 69);
            EXPECT_LT(tb1[i].pitch, t4[i].pitch);
        }
    }
    // D: octave unison, 2 up and 3 down
    {
        const auto lead = inBars(done, TPT1, 25, 32);
        const auto a1 = inBars(done, ALTO1, 25, 32);
        const auto a2 = inBars(done, 1, 25, 32);
        const auto ten = inBars(done, 2, 25, 32);
        const auto bari = inBars(done, BARI, 25, 32);
        ASSERT_EQ(a1.size(), lead.size());
        for (size_t i = 0; i < lead.size(); ++i) {
            EXPECT_EQ(pc(a1[i].pitch), pc(lead[i].pitch));
            EXPECT_EQ(a2[i].pitch, a1[i].pitch);
            EXPECT_EQ(ten[i].pitch, a1[i].pitch - 12);
            EXPECT_EQ(bari[i].pitch, a1[i].pitch - 12);
        }
    }

    // Proofread: no spelling that looks wrong and no range errors in the new parts
    {
        mu::notation::jubelle::ProofreadOptions po;
        po.categories = { mu::notation::jubelle::CheckCategory::Spelling, mu::notation::jubelle::CheckCategory::Range };
        int looks = 0;
        int range = 0;
        for (const auto& f : mu::notation::jubelle::proofread(s.get(), po)) {
            if (f.staffIdx > BTBN || f.staffIdx == TPT1) {
                continue;
            }
            looks += f.message.contains(u"looks like") ? 1 : 0;
            range += f.category == mu::notation::jubelle::CheckCategory::Range && f.severity == mu::notation::jubelle::Severity::Error ? 1 : 0;
        }
        // only where no spelling of the line can make every step look right, and each of those is reported
        EXPECT_LE(looks, ra.awkwardSpellings + rb.awkwardSpellings + rc.awkwardSpellings + rd.awkwardSpellings);
        EXPECT_LE(looks, 4);
        std::printf("[harmony] full chart: %d steps that look wrong however they are spelled\n", looks);
        EXPECT_EQ(range, 0);
    }

    // save, close and reopen: every note, spelling, tie and chord symbol comes back
    const std::string path = std::string(std::filesystem::temp_directory_path().string()) + "/jubelle_harmonised_chart.mscz";
    const std::vector<std::string> doneChords = allChordTexts(s.get());
    ASSERT_TRUE(jubelle_templates::writeMscz(s.get(), path));
    {
        std::unique_ptr<MasterScore> back(loadChart(path));
        ASSERT_TRUE(back);
        back->doLayout();
        EXPECT_EQ(snapshot(back.get()), done);
        EXPECT_EQ(allChordTexts(back.get()), doneChords);
    }
    std::filesystem::remove(path);

    // four Undos put the chart back exactly; four Redos bring every section back
    for (int k = 0; k < 4; ++k) {
        s->undoRedo(true, nullptr);
    }
    EXPECT_EQ(snapshot(s.get()), original);
    EXPECT_EQ(allChordTexts(s.get()), originalChords);
    for (int k = 0; k < 4; ++k) {
        s->undoRedo(false, nullptr);
    }
    EXPECT_EQ(snapshot(s.get()), done);
    EXPECT_EQ(allChordTexts(s.get()), doneChords);
}

TEST(Jubelle_HarmonyRunner, UnreadableChordsAreExplainedNotGuessed)
{
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("test"));
    writeLine(s.get(), TPT1, { { 76, 2 }, { 77, 2 }, { 76, 2 }, { 79, 2 } });
    addChord(s.get(), PIANO, 0, "C6");
    addChord(s.get(), PIANO, 4, "N.C.");
    s->endCmd();
    selectBars(s.get(), TPT1, 1, 2);
    const hr::Scope sc = hr::scope(s.get(), {});
    ASSERT_TRUE(sc.ok);
    bool explained = false;
    for (const std::string& n : sc.notices) {
        explained = explained || n.find("unison") != std::string::npos;
    }
    EXPECT_TRUE(explained) << "the dialog says what happens under N.C.";
    const hr::Report r = harmonise(s.get(), 1, 2, {});
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.unisonNotes, 2);
    EXPECT_EQ(r.unisonBars, (std::vector<int> { 2 }));
    // under N.C. every horn plays the melody's pitch class: nothing invented
    for (staff_idx_t st = 0; st <= BTBN; ++st) {
        const auto n = notesOn(s.get(), st);
        ASSERT_EQ(n.size(), 4u) << "staff " << st;
        EXPECT_EQ(pc(n[2].pitch), 4) << "staff " << st;
        EXPECT_EQ(pc(n[3].pitch), 7) << "staff " << st;
    }
}

TEST(Jubelle_HarmonyRunner, TheDialogExplainsChoicesThisScoreCantUse)
{
    // a small band: two trumpets, no baritone
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("test"));
    tuneInBb(s.get());
    s->endCmd();
    selectBars(s.get(), TPT1, 1, 4);
    hr::RunOptions o;
    o.useStaves = true;
    o.staves = hr::findSections(s.get());
    o.staves.trumpets = { TPT1, TPT2, muse::nidx, muse::nidx };
    o.staves.saxes[4] = muse::nidx;
    o.orchestration.saxes = hr::SaxTechnique::Drop2BariRoots;
    const hr::Scope sc = hr::scope(s.get(), o);
    std::string all;
    for (const std::string& n : sc.notices) {
        all += n + "\n";
    }
    EXPECT_NE(all.find("has the melody and keeps it"), std::string::npos) << all;
    EXPECT_NE(all.find("2 trumpet(s)"), std::string::npos) << all;
    EXPECT_NE(all.find("no baritone"), std::string::npos) << all;
    const hr::Report r = harmonise(s.get(), 1, 4, o);
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(notesOn(s.get(), 7).empty()) << "trumpet 3 isn't one of this band's players";
    EXPECT_TRUE(notesOn(s.get(), BARI).empty());
}
