/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "engraving/dom/articulation.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/select.h"
#include "engraving/dom/textline.h"
#include "engraving/playback/playbackmodel.h"

#include "mpe/tests/mocks/articulationprofilesrepositorymock.h"

#include "notation/internal/jubelle/articulationpassage.h"

#include "utils/scorerw.h"

using namespace mu::engraving;
using namespace mu::engraving::jubelle;
namespace jap = mu::notation::jubelle;

//! Jubelle Phase 5A level 3: passage overrides, on MuseScore's real
//! playback model. swing_eighths.mscx: Trumpet and Alto Sax, 4 bars of
//! eighths at 120 bpm (an eighth = 250 000 µs). Every note gets a staccato;
//! the trumpet's first note in bar 2 also gets an accent.
namespace {
constexpr double EIGHTH = 250000.0;
constexpr int BAR_TICKS = 1920;

muse::mpe::ArticulationPattern pattern(int durationFactor, int peak)
{
    using namespace muse::mpe;
    ArticulationPatternSegment seg;
    seg.arrangementPattern = ArrangementPattern(durationFactor, 0);
    for (int i = 0; i <= 10; ++i) {
        seg.expressionPattern.dynamicOffsetMap.emplace(i * 10 * ONE_PERCENT, i == 5 ? peak : 4000);
        seg.pitchPattern.pitchOffsetMap.emplace(i * 10 * ONE_PERCENT, 0);
    }
    ArticulationPattern p;
    p.emplace(0, seg);
    return p;
}

class Jubelle_ArticulationPassage : public ::testing::Test
{
protected:
    void SetUp() override
    {
        score = ScoreRW::readScore(u"jubelle_data/swing_eighths.mscx");
        ASSERT_TRUE(score);
        trumpet = score->parts()[0];
        sax = score->parts()[1];
        score->startCmd(muse::TranslatableString::untranslatable("articulations"));
        for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (track_idx_t t : { track_idx_t(0), track_idx_t(4) }) {
                if (s->element(t) && s->element(t)->isChord()) {
                    add(toChord(s->element(t)), SymId::articStaccatoAbove);
                    if (t == 0 && s->tick().ticks() == BAR_TICKS) {
                        add(toChord(s->element(t)), SymId::articAccentAbove);
                    }
                }
            }
        }
        score->endCmd();

        auto profile = std::make_shared<muse::mpe::ArticulationsProfile>();
        profile->setPattern(muse::mpe::ArticulationType::Standard, pattern(10000, 5000));
        profile->setPattern(muse::mpe::ArticulationType::Staccato, pattern(5000, 5000));
        profile->setPattern(muse::mpe::ArticulationType::Accent, pattern(10000, 6000));
        repository = std::make_shared<::testing::NiceMock<muse::mpe::ArticulationProfilesRepositoryMock> >();
        ON_CALL(*repository, defaultProfile(::testing::_)).WillByDefault(::testing::Return(profile));
        model = std::make_unique<PlaybackModel>(muse::modularity::globalCtx());
        model->profilesRepository.set(repository);
        model->load(score);
    }

    void TearDown() override
    {
        model.reset();
        delete score;
    }

    void add(Chord* c, SymId id)
    {
        Articulation* a = Factory::createArticulation(c);
        a->setSymId(id);
        a->setParent(c);
        a->setTrack(c->track());
        score->undoAddElement(a);
    }

    //! Played length of each note, as % of an eighth, in order
    std::vector<int> lengths(const Part* part)
    {
        std::vector<std::pair<double, double> > notes;
        for (const auto& [ts, list] : model->resolveTrackPlaybackData(part->id(), part->instrumentId()).originEvents) {
            for (const muse::mpe::PlaybackEvent& e : list) {
                if (const auto* n = std::get_if<muse::mpe::NoteEvent>(&e)) {
                    notes.push_back({ double(n->arrangementCtx().actualTimestamp), double(n->arrangementCtx().actualDuration) });
                }
            }
        }
        std::sort(notes.begin(), notes.end());
        std::vector<int> out;
        for (const auto& n : notes) {
            out.push_back(int(std::lround(n.second * 100.0 / EIGHTH)));
        }
        // The trumpet's accented staccato (bar 2, first note): MuseScore plays
        // the average of the two articulations' lengths (accent = 100%), so it
        // is checked on its own in AccentedStaccatoIsMuseScoresAverage
        if (part == trumpet && out.size() > 8) {
            out[8] = -1;
        }
        return out;
    }

    int peakOfFirstNoteInBar2(const Part* part)
    {
        for (const auto& [ts, list] : model->resolveTrackPlaybackData(part->id(), part->instrumentId()).originEvents) {
            for (const muse::mpe::PlaybackEvent& e : list) {
                if (const auto* n = std::get_if<muse::mpe::NoteEvent>(&e)) {
                    if (std::abs(double(n->arrangementCtx().actualTimestamp) - 8 * EIGHTH) < 1.0) {
                        return n->expressionCtx().expressionCurve.maxAmplitudeLevel();
                    }
                }
            }
        }
        return -1;
    }

    jap::PassageRange bars(int from, int to, std::vector<Part*> parts)
    {
        jap::PassageRange r;
        r.startTick = (from - 1) * BAR_TICKS;
        r.endTick = to * BAR_TICKS;
        r.parts = parts;
        return r;
    }

    void setValue(const jap::PassageRange& r, ArtParam p, std::optional<int> v)
    {
        score->startCmd(muse::TranslatableString::untranslatable("passage"));
        jap::setPassageValue(score, r, p, v);
        score->endCmd();
    }

    static std::vector<int> repeat(int v, int n) { return std::vector<int>(size_t(n), v); }
    std::vector<int> concat(std::initializer_list<std::vector<int> > parts, const Part* forPart = nullptr)
    {
        std::vector<int> out;
        for (const auto& p : parts) {
            out.insert(out.end(), p.begin(), p.end());
        }
        return out;
    }
    //! Expected trumpet lengths (the accented note is marked -1, see lengths())
    std::vector<int> t(std::initializer_list<std::vector<int> > parts)
    {
        std::vector<int> out = concat(parts);
        if (out.size() > 8) {
            out[8] = -1;
        }
        return out;
    }

    MasterScore* score = nullptr;
    Part* trumpet = nullptr;
    Part* sax = nullptr;
    std::shared_ptr<::testing::NiceMock<muse::mpe::ArticulationProfilesRepositoryMock> > repository;
    std::unique_ptr<PlaybackModel> model;
};
}

TEST_F(Jubelle_ArticulationPassage, GlobalInstrumentPassageHierarchy)
{
    // Global 50%, trumpets 40%
    ArticulationPlaybackSettings s;
    s.scoreValues.set(ArtParam::StaccatoLength, 50);
    s.partValues[trumpet->id().toUint64()].set(ArtParam::StaccatoLength, 40);
    score->startCmd(muse::TranslatableString::untranslatable("settings"));
    s.write(score);
    score->endCmd();
    const int accentBefore = peakOfFirstNoteInBar2(trumpet);

    // A trumpet passage, bars 2–3: staccato 25%
    setValue(bars(2, 3, { trumpet }), ArtParam::StaccatoLength, 25);

    EXPECT_EQ(lengths(trumpet), t({ repeat(40, 8), repeat(25, 16), repeat(40, 8) }));
    EXPECT_EQ(lengths(sax), repeat(50, 32));
    // The passage only changes staccato: the accent is as before
    EXPECT_EQ(peakOfFirstNoteInBar2(trumpet), accentBefore);

    // The rows say where each value comes from
    const std::vector<jap::PassageRow> rows = jap::passageRows(score, bars(2, 3, { trumpet }));
    for (const jap::PassageRow& r : rows) {
        if (r.param == ArtParam::StaccatoLength) {
            EXPECT_EQ(r.value, 25);
            EXPECT_EQ(r.source, jap::ValueSource::Here);
            EXPECT_TRUE(r.inSelection);
        } else if (r.param == ArtParam::AccentLoudness) {
            EXPECT_EQ(r.source, jap::ValueSource::MuseScore);
            EXPECT_TRUE(r.inSelection);
        } else if (r.param == ArtParam::TenutoLength) {
            EXPECT_FALSE(r.inSelection);
        }
    }
    const std::vector<jap::PassageRow> bar4 = jap::passageRows(score, bars(4, 4, { trumpet }));
    for (const jap::PassageRow& r : bar4) {
        if (r.param == ArtParam::StaccatoLength) {
            EXPECT_EQ(r.value, 40);
            EXPECT_EQ(r.source, jap::ValueSource::Instrument);
            EXPECT_EQ(r.sourceName, u"Trumpet");
        }
    }

    // An accent value in the same passage changes only the accent
    setValue(bars(2, 3, { trumpet }), ArtParam::AccentLoudness, 30);
    EXPECT_GT(peakOfFirstNoteInBar2(trumpet), accentBefore);
    EXPECT_EQ(lengths(trumpet), t({ repeat(40, 8), repeat(25, 16), repeat(40, 8) }));

    // Clearing staccato: the passage inherits the trumpet's 40% again,
    // and keeps its accent
    setValue(bars(2, 3, { trumpet }), ArtParam::StaccatoLength, std::nullopt);
    EXPECT_EQ(lengths(trumpet), t({ repeat(40, 32) }));
    EXPECT_GT(peakOfFirstNoteInBar2(trumpet), accentBefore);

    // Clearing the last value removes the line entirely
    setValue(bars(2, 3, { trumpet }), ArtParam::AccentLoudness, std::nullopt);
    EXPECT_EQ(peakOfFirstNoteInBar2(trumpet), accentBefore);
    for (const auto& [tick, sp] : score->spanner()) {
        EXPECT_FALSE(passageLine(sp));
    }

    // Undo, step by step
    score->undoRedo(true, nullptr);   // accent back
    EXPECT_GT(peakOfFirstNoteInBar2(trumpet), accentBefore);
    score->undoRedo(true, nullptr);   // staccato back
    EXPECT_EQ(lengths(trumpet), t({ repeat(40, 8), repeat(25, 16), repeat(40, 8) }));
}

TEST_F(Jubelle_ArticulationPassage, HighlightedInstrumentsOverlapsAndEdits)
{
    // A passage highlighted over both staves: one line per instrument
    setValue(bars(2, 3, { trumpet, sax }), ArtParam::StaccatoLength, 30);
    int lines = 0;
    for (const auto& [tick, sp] : score->spanner()) {
        if (const TextLine* l = passageLine(sp)) {
            ++lines;
            EXPECT_FALSE(l->visible()) << "never printed";
        }
    }
    EXPECT_EQ(lines, 2);
    EXPECT_EQ(lengths(sax), concat({ repeat(50, 8), repeat(30, 16), repeat(50, 8) }));

    // A passage inside it on the trumpet: the later-starting one wins
    setValue(bars(3, 3, { trumpet }), ArtParam::StaccatoLength, 10);
    EXPECT_EQ(lengths(trumpet), t({ repeat(50, 8), repeat(30, 8), repeat(10, 8), repeat(50, 8) }));
    EXPECT_EQ(lengths(sax), concat({ repeat(50, 8), repeat(30, 16), repeat(50, 8) }));

    // Saved and reopened
    const muse::String saved = u"jubelle_passage_test.mscx";
    ASSERT_TRUE(ScoreRW::saveScore(score, saved));
    MasterScore* reopened = ScoreRW::readScore(saved, true);
    ASSERT_TRUE(reopened);
    int reopenedLines = 0;
    for (const auto& [tick, sp] : reopened->spanner()) {
        if (const TextLine* l = passageLine(sp)) {
            ++reopenedLines;
            EXPECT_FALSE(l->visible());
        }
    }
    EXPECT_EQ(reopenedLines, 3);
    const std::vector<PassageAt> at = passagesAt(reopened, reopened->parts()[0], 2 * BAR_TICKS + 10);
    ASSERT_EQ(at.size(), 2u);
    EXPECT_EQ(mergedPassageValues(at).get(ArtParam::StaccatoLength), 10);
    delete reopened;

    // Inserting a bar before the passage: the lines move with the music
    score->startCmd(muse::TranslatableString::untranslatable("insert"));
    score->insertMeasure(score->firstMeasure()->nextMeasure());
    score->endCmd();
    std::vector<int> starts;
    for (const auto& [tick, sp] : score->spanner()) {
        if (passageLine(sp)) {
            starts.push_back(sp->tick().ticks());
        }
    }
    std::sort(starts.begin(), starts.end());
    EXPECT_EQ(starts, (std::vector<int> { 2 * BAR_TICKS, 2 * BAR_TICKS, 3 * BAR_TICKS }));

    // The written music never changed: every chord still an eighth with one staccato
    for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        if (s->element(4) && s->element(4)->isChord()) {
            EXPECT_EQ(toChord(s->element(4))->ticks(), Fraction(1, 8));
            EXPECT_EQ(toChord(s->element(4))->articulations().size(), 1u);
        }
    }
}

TEST_F(Jubelle_ArticulationPassage, AccentedStaccatoIsMuseScoresAverage)
{
    setValue(bars(2, 3, { trumpet }), ArtParam::StaccatoLength, 20);
    double accented = 0;
    for (const auto& [ts, list] : model->resolveTrackPlaybackData(trumpet->id(), trumpet->instrumentId()).originEvents) {
        for (const muse::mpe::PlaybackEvent& e : list) {
            if (const auto* n = std::get_if<muse::mpe::NoteEvent>(&e)) {
                if (std::abs(double(n->arrangementCtx().actualTimestamp) - 8 * EIGHTH) < 1.0) {
                    accented = double(n->arrangementCtx().actualDuration);
                }
            }
        }
    }
    EXPECT_NEAR(accented, EIGHTH * (20 + 100) / 200.0, 2.0);
}

TEST(Jubelle_ArticulationPassageText, TextRoundTrip)
{
    ArticulationPlaybackValues v;
    v.set(ArtParam::StaccatoLength, 25);
    v.set(ArtParam::AccentLoudness, 15);
    v.set(ArtParam::FallCurve, -1);
    v.set(ArtParam::FallRange, 7);
    const muse::String text = passageText(v);
    EXPECT_EQ(text, u"Jubelle playback: staccato=25% accentLoudness=+15% fallRange=7 fallCurve=-1");
    EXPECT_EQ(parsePassageText(text), v);
    EXPECT_FALSE(parsePassageText(u"cresc."));
}
