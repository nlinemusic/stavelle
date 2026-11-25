// SPDX-License-Identifier: GPL-3.0-only
// S2: model-based score writing (decision D-E27). Scores built and edited in the model are saved as files
// generated from the model alone, closed, opened again and compared: the music, the notation the model holds,
// part layouts and the 11 Jubelle style settings. A second save of the reopened score is byte-for-byte the same.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "edit/history.h"
#include "edit/scorebuilder.h"
#include "edit/structure.h"
#include "io/details.h"
#include "io/mscxreader.h"
#include "io/mscxwriter.h"
#include "io/scorefile.h"
#include "io/xml.h"
#include "model/invariants.h"

using namespace je;
using namespace je::edit;
using je::model::Kind;
using je::model::Text;
namespace F = je::model::fields;
namespace fs = std::filesystem;

namespace {
std::string problems(const model::Snapshot& s)
{
    std::string out;
    for (const std::string& p : model::checkInvariants(s)) {
        out += p + "\n";
    }
    return out;
}

fs::path tempFile(const std::string& name)
{
    const fs::path dir = fs::temp_directory_path() / "je_writer_tests";
    fs::create_directories(dir);
    return dir / name;
}

std::vector<const model::Object*> all(const model::Snapshot& s, Kind kind)
{
    std::vector<const model::Object*> out;
    s.forEachObject([&](const model::Object& o) {
        if (o.kind() == kind) {
            out.push_back(&o);
        }
    });
    return out;
}

std::vector<std::string> texts(const model::Snapshot& s, Kind kind, const std::string& subkind)
{
    std::vector<std::string> out;
    for (const model::Object* o : all(s, kind)) {
        if (o->text(F::SUBKIND) == subkind) {
            out.push_back(o->text(F::TEXT));
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

//! A score for an ensemble, built in the model as editing would: three players (one transposing), two voices,
//! nested tuplets, grace notes, ties, articulations, lyrics, dynamics, a hairpin and slur, chord symbols, tempo,
//! key, repeats with a volta, a jump, a rehearsal mark, an instrument change and a part layout
struct Ensemble {
    model::Model model;
    IdGenerator ids { 1 };
    ScoreBuilder b { model, ids };
    History history;
    ScoreBuilder::PlayerIds flute, horn, cello;
    Id voice2, tiedFrom, firstFluteNote;

    Id chord(Transaction& tx, Id voice, Fraction t, Fraction base, int dots, std::vector<int> pitches, Id inTuplet = Id(),
             Fraction factor = Fraction(1, 1))
    {
        Fraction d = base, add = base;
        for (int i = 0; i < dots; ++i) {
            add = add * Fraction(1, 2);
            d += add;
        }
        std::vector<std::pair<Field, model::Value> > f { { F::TIME, t }, { F::DURATION, d * factor }, { F::BASE_DURATION, base } };
        if (dots) {
            f.push_back({ F::DOTS, int64_t(dots) });
        }
        if (inTuplet.isValid()) {
            f.push_back({ F::TUPLET, inTuplet });
        }
        const Id c = create(tx, ids, voice, { Kind::Chord, f }, Placement::ByTime);
        for (int p : pitches) {
            create(tx, ids, c, { Kind::Note, { { F::PITCH, int64_t(p) } } });
        }
        return c;
    }

    Id rest(Transaction& tx, Id voice, Fraction t, Fraction base)
    {
        return create(tx, ids, voice, { Kind::Rest, { { F::TIME, t }, { F::DURATION, base }, { F::BASE_DURATION, base } } },
                      Placement::ByTime);
    }

    void build()
    {
        Transaction tx(model, "Build");
        b.newScore(tx, "Save and reopen");
        b.addMeasures(tx, 4);
        flute = b.addPlayer(tx, "Flute", "flute", "woodwinds", 1);
        horn = b.addPlayer(tx, "Horn in F", "horn", "brass", 1, -7);
        cello = b.addPlayer(tx, "Violoncello", "violoncello", "strings", 1);
        voice2 = create(tx, ids, flute.staff, { Kind::Voice, { { F::FLOW, b.flow() }, { F::NUMBER, int64_t(1) } } });

        // flute bar 1: dotted quarter, eighth, a triplet with a nested triplet, a grace note before the last quarter
        const Id c1 = chord(tx, flute.voice, Fraction(0, 1), Fraction(1, 4), 1, { 72 });
        firstFluteNote = b.notesOf(c1).front();
        chord(tx, flute.voice, Fraction(3, 8), Fraction(1, 8), 0, { 74 });
        const Id tuplet = create(tx, ids, flute.voice, { Kind::Tuplet, { { F::TIME, Fraction(1, 2) }, { F::DURATION, Fraction(1, 4) },
                                                             { F::BASE_DURATION, Fraction(1, 8) }, { F::RATIO_NUM, int64_t(3) },
                                                             { F::RATIO_DEN, int64_t(2) } } }, Placement::ByTime);
        chord(tx, flute.voice, Fraction(1, 2), Fraction(1, 8), 0, { 76 }, tuplet, Fraction(2, 3));
        chord(tx, flute.voice, Fraction(7, 12), Fraction(1, 8), 0, { 77 }, tuplet, Fraction(2, 3));
        const Id inner = create(tx, ids, flute.voice, { Kind::Tuplet, { { F::TIME, Fraction(2, 3) }, { F::DURATION, Fraction(1, 12) },
                                                            { F::BASE_DURATION, Fraction(1, 16) }, { F::RATIO_NUM, int64_t(3) },
                                                            { F::RATIO_DEN, int64_t(2) }, { F::TUPLET, tuplet } } },
                                Placement::ByTime);
        for (int i = 0; i < 3; ++i) {
            chord(tx, flute.voice, Fraction(2, 3) + Fraction(i, 36), Fraction(1, 16), 0, { 79 + i }, inner, Fraction(4, 9));
        }
        const Id grace = create(tx, ids, flute.voice, { Kind::Chord, { { F::TIME, Fraction(3, 4) }, { F::DURATION, Fraction(1, 8) },
                                                            { F::BASE_DURATION, Fraction(1, 8) }, { F::GRACE, true },
                                                            { F::GRACE_TYPE, Text("acciaccatura") } } }, Placement::Append);
        create(tx, ids, grace, { Kind::Note, { { F::PITCH, int64_t(83) } } });
        const Id last = chord(tx, flute.voice, Fraction(3, 4), Fraction(1, 4), 0, { 84 });
        create(tx, ids, last, { Kind::Attachment, { { F::SUBKIND, Text("staccato") } } });

        // flute voice 2, bar 1: two half notes
        chord(tx, voice2, Fraction(0, 1), Fraction(1, 2), 0, { 60 });
        chord(tx, voice2, Fraction(1, 2), Fraction(1, 2), 0, { 62 });

        // flute bar 2: tied halves with an accent, a fermata and lyrics; a slur over them
        const Id h1 = chord(tx, flute.voice, Fraction(1, 1), Fraction(1, 2), 0, { 72 });
        const Id h2 = chord(tx, flute.voice, Fraction(3, 2), Fraction(1, 2), 0, { 72 });
        tiedFrom = b.notesOf(h1).front();
        tx.set(tiedFrom, F::TIE_TO, b.notesOf(h2).front());
        create(tx, ids, h1, { Kind::Attachment, { { F::SUBKIND, Text("accent") } } });
        create(tx, ids, h2, { Kind::Attachment, { { F::SUBKIND, Text("fermata") } } });
        create(tx, ids, h1, { Kind::Attachment, { { F::SUBKIND, Text("lyric") }, { F::SYLLABIC, Text("begin") }, { F::TEXT, Text("Hal") } } });
        create(tx, ids, h2, { Kind::Attachment, { { F::SUBKIND, Text("lyric") }, { F::SYLLABIC, Text("end") }, { F::TEXT, Text("le") } } });
        create(tx, ids, model.score(), { Kind::Spanner, { { F::SUBKIND, Text("slur") }, { F::ANCHOR, h1 }, { F::END_ANCHOR, h2 },
                                             { F::TIME, Fraction(1, 1) }, { F::DURATION, Fraction(1, 1) } } });
        // flute bars 3–4: quarters, the last bar a whole rest
        for (int q = 0; q < 4; ++q) {
            chord(tx, flute.voice, Fraction(2, 1) + Fraction(q, 4), Fraction(1, 4), 0, { 69 + q });
        }
        rest(tx, flute.voice, Fraction(3, 1), Fraction(1, 1));

        // horn (in F, sounding a fifth lower) and cello: whole notes and quarters
        for (int bar = 0; bar < 4; ++bar) {
            chord(tx, horn.voice, Fraction(bar, 1), Fraction(1, 1), 0, { 65 + bar });
            for (int q = 0; q < 4; ++q) {
                chord(tx, cello.voice, Fraction(bar, 1) + Fraction(q, 4), Fraction(1, 4), 0, { 48 + q, 55 + q });
            }
        }

        auto item = [&](std::vector<std::pair<Field, model::Value> > f) {
            return create(tx, ids, b.flow(), { Kind::FlowItem, f }, Placement::ByTime);
        };
        item({ { F::SUBKIND, Text("keySig") }, { F::TIME, Fraction(0, 1) }, { F::INT_VALUE, int64_t(-1) } });
        item({ { F::SUBKIND, Text("tempo") }, { F::TIME, Fraction(0, 1) }, { F::REAL_VALUE, 2.0 }, { F::TEXT, Text("Allegro") } });
        item({ { F::SUBKIND, Text("dynamic") }, { F::ANCHOR, horn.staff }, { F::TIME, Fraction(0, 1) }, { F::TEXT, Text("mf") } });
        item({ { F::SUBKIND, Text("dynamic") }, { F::ANCHOR, cello.staff }, { F::TIME, Fraction(2, 1) }, { F::TEXT, Text("p") } });
        item({ { F::SUBKIND, Text("harmony") }, { F::ANCHOR, flute.staff }, { F::TIME, Fraction(0, 1) }, { F::TEXT, Text("Fmaj7") } });
        item({ { F::SUBKIND, Text("staffText") }, { F::ANCHOR, cello.staff }, { F::TIME, Fraction(1, 1) }, { F::TEXT, Text("pizz.") } });
        item({ { F::SUBKIND, Text("rehearsalMark") }, { F::TIME, Fraction(2, 1) }, { F::TEXT, Text("A") } });
        item({ { F::SUBKIND, Text("instrumentChange") }, { F::ANCHOR, flute.staff }, { F::TIME, Fraction(2, 1) },
               { F::INSTRUMENT, Text("piccolo") }, { F::NAME, Text("Piccolo") }, { F::TRANSPOSE_CHROMATIC, int64_t(12) } });
        item({ { F::SUBKIND, Text("jump") }, { F::TIME, Fraction(4, 1) }, { F::TEXT, Text("D.C. al Fine") } });
        create(tx, ids, model.score(), { Kind::Spanner, { { F::SUBKIND, Text("hairpin") }, { F::ANCHOR, horn.staff },
                                             { F::TIME, Fraction(0, 1) }, { F::DURATION, Fraction(2, 1) }, { F::INT_VALUE, int64_t(0) } } });

        // bars 3–4 repeated, a first ending over bar 4
        std::vector<Id> bars;
        for (Id m : model.find(b.flow())->children().ids()) {
            if (model.find(m)->kind() == Kind::Measure) {
                bars.push_back(m);
            }
        }
        tx.set(bars[2], F::REPEAT_START, true);
        tx.set(bars[3], F::REPEAT_END, int64_t(2));
        create(tx, ids, model.score(), { Kind::Spanner, { { F::SUBKIND, Text("volta") }, { F::ANCHOR, bars[3] },
                                             { F::TIME, Fraction(3, 1) }, { F::DURATION, Fraction(1, 1) },
                                             { F::INT_VALUE, int64_t(1) }, { F::TEXT, Text("1.") } } });
        // a part layout for the horn
        create(tx, ids, model.score(), { Kind::Layout, { { F::NAME, Text("Horn in F") }, { F::PLAYERS, model::IdList({ horn.player }) } } });
        history.push(tx.commit());
    }
};

struct Reopened {
    model::Model model;
    IdGenerator ids { 1 };
};

void saveAndOpen(const model::Snapshot& s, const fs::path& path, Reopened& out, io::WriteReport* report = nullptr)
{
    std::string error;
    ASSERT_TRUE(io::saveScore(s, path.string(), error, report)) << error;
    ASSERT_TRUE(io::openScore(path.string(), out.model, out.ids, error)) << error;
}

std::string bytesOf(const fs::path& p)
{
    std::string b;
    io::zip::readFile(p.string(), b);
    return b;
}
}

TEST(ModelWriter, SaveAndReopenKeepsTheMusic)
{
    Ensemble e;
    e.build();
    ASSERT_EQ(problems(e.model.snapshot()), "");
    Reopened r;
    saveAndOpen(e.model.snapshot(), tempFile("ensemble.mscz"), r);
    const model::Snapshot s = r.model.snapshot();
    EXPECT_EQ(problems(s), "");
    // every event: staff, voice, time, duration, pitches, grace
    EXPECT_EQ(io::musicListing(s), io::musicListing(e.model.snapshot()));

    EXPECT_EQ(all(s, Kind::Player).size(), 3u);
    EXPECT_EQ(all(s, Kind::Tuplet).size(), 2u);
    EXPECT_EQ(all(s, Kind::Measure).size(), 4u);
    int ties = 0;
    for (const model::Object* n : all(s, Kind::Note)) {
        ties += n->has(F::TIE_TO);
    }
    EXPECT_EQ(ties, 1);
    EXPECT_EQ(texts(s, Kind::Attachment, "lyric"), (std::vector<std::string> { "Hal", "le" }));
    std::vector<std::string> articulations;
    for (const model::Object* a : all(s, Kind::Attachment)) {
        if (a->text(F::TAG) == "Articulation") {
            articulations.push_back(a->text(F::SUBKIND));
        }
    }
    std::sort(articulations.begin(), articulations.end());
    EXPECT_EQ(articulations, (std::vector<std::string> { "articAccentAbove", "articStaccatoAbove" }));
    EXPECT_EQ(texts(s, Kind::FlowItem, "dynamic"), (std::vector<std::string> { "mf", "p" }));
    EXPECT_EQ(texts(s, Kind::FlowItem, "fermata"), (std::vector<std::string> { "fermataAbove" }));
    EXPECT_EQ(texts(s, Kind::FlowItem, "rehearsalMark"), (std::vector<std::string> { "A" }));
    EXPECT_EQ(texts(s, Kind::FlowItem, "staffText"), (std::vector<std::string> { "pizz." }));
    EXPECT_EQ(texts(s, Kind::FlowItem, "jump"), (std::vector<std::string> { "D.C. al Fine" }));
    EXPECT_EQ(texts(s, Kind::FlowItem, "harmony"), (std::vector<std::string> { "maj7" }));   // root F kept apart
    EXPECT_EQ(texts(s, Kind::FlowItem, "keySig").size(), 3u);                              // on every staff
    std::vector<std::string> spanners;
    for (const model::Object* sp : all(s, Kind::Spanner)) {
        spanners.push_back(sp->text(F::SUBKIND) + " " + sp->fractionOr(F::TIME, Fraction()).toString() + " "
                           + sp->fractionOr(F::DURATION, Fraction()).toString());
    }
    std::sort(spanners.begin(), spanners.end());
    EXPECT_EQ(spanners, (std::vector<std::string> { "hairPin 0/1 2/1", "slur 1/1 1/1", "volta 3/1 1/1" }));
    for (const model::Object* it : all(s, Kind::FlowItem)) {
        if (it->text(F::SUBKIND) == "tempo") {
            EXPECT_EQ(std::get<double>(it->get(F::REAL_VALUE)), 2.0);
        }
        if (it->text(F::SUBKIND) == "instrumentChange") {
            EXPECT_EQ(it->text(F::INSTRUMENT), "piccolo");
            EXPECT_EQ(it->intOr(F::TRANSPOSE_CHROMATIC, 0), 12);
        }
    }
    for (const model::Object* p : all(s, Kind::Player)) {
        if (p->text(F::NAME) == "Horn in F") {
            EXPECT_EQ(p->intOr(F::TRANSPOSE_CHROMATIC, 0), -7);
            EXPECT_EQ(p->intOr(F::TRANSPOSE_DIATONIC, 0), -4);
        }
    }
    std::vector<const model::Object*> measures = all(s, Kind::Measure);
    std::sort(measures.begin(), measures.end(), [](auto a, auto b) { return a->fractionOr(F::TIME, Fraction()) < b->fractionOr(F::TIME, Fraction()); });
    EXPECT_TRUE(measures[2]->has(F::REPEAT_START));
    EXPECT_EQ(measures[3]->intOr(F::REPEAT_END, 0), 2);
    // the part layout, with its own score in the file, linked to the main score
    const auto layouts = all(s, Kind::Layout);
    ASSERT_EQ(layouts.size(), 1u);
    EXPECT_EQ(layouts[0]->text(F::NAME), "Horn in F");
    const model::IdList shown = std::get<model::IdList>(layouts[0]->get(F::PLAYERS));
    ASSERT_EQ(shown.size(), 1u);
    EXPECT_EQ(s.find(shown[0])->text(F::NAME), "Horn in F");
    io::ScoreFile file;
    std::string error;
    ASSERT_TRUE(io::load(tempFile("ensemble.mscz").string(), file, error)) << error;
    ASSERT_EQ(file.excerpts.size(), 1u);
    const std::string part = io::xml::write(file.excerpts.begin()->second);
    EXPECT_NE(part.find("<linkedTo>"), std::string::npos);
    EXPECT_EQ(part.find("Flute"), std::string::npos);   // only the horn
}

TEST(ModelWriter, SecondSaveIsTheSame)
{
    Ensemble e;
    e.build();
    Reopened r1, r2;
    saveAndOpen(e.model.snapshot(), tempFile("first.mscz"), r1);
    io::WriteReport report;
    saveAndOpen(r1.model.snapshot(), tempFile("second.mscz"), r2, &report);
    EXPECT_EQ(report.detailsDropped, 0);
    EXPECT_EQ(io::modelDump(r2.model.snapshot()), io::modelDump(r1.model.snapshot()));
    std::string error;
    ASSERT_TRUE(io::saveScore(r2.model.snapshot(), tempFile("third.mscz").string(), error)) << error;
    EXPECT_EQ(bytesOf(tempFile("third.mscz")), bytesOf(tempFile("second.mscz")));
}

TEST(ModelWriter, EditsAfterReopeningAreSavedAndStaleDetailsDropped)
{
    // a real score from the repository, with appearance details the model doesn't hold
    const fs::path source = fs::path(JE_REPO_DIR) / "vtest" / "scores" / "accidental-18.mscz";
    ASSERT_TRUE(fs::exists(source));
    Reopened r;
    std::string error;
    ASSERT_TRUE(io::openScore(source.string(), r.model, r.ids, error)) << error;
    io::WriteReport unchanged;
    const std::string before = io::saveBytes(io::generateScoreFile(r.model.snapshot(), &unchanged));
    EXPECT_EQ(unchanged.detailsDropped, 0);

    // edit: move the first note with details a tone up, and delete the last chord of the first staff
    const model::Snapshot beforeEdit = r.model.snapshot();   // keeps the objects found below alive
    const model::Object* edited = nullptr;
    for (const model::Object* n : all(beforeEdit, Kind::Note)) {
        if (n->has(F::DETAILS) && !n->has(F::TIE_TO)) {
            edited = n;
            break;
        }
    }
    ASSERT_NE(edited, nullptr);
    const Id note = edited->id();
    const int64_t pitch = edited->intOr(F::PITCH, 60);
    const int64_t tpc = edited->intOr(F::TPC, 14);
    History history;
    {
        Transaction tx(r.model, "Repitch");
        tx.set(note, F::PITCH, int64_t(pitch + 2));
        tx.set(note, F::TPC, int64_t(tpc + 2));
        history.push(tx.commit());
    }
    io::WriteReport report;
    Reopened again;
    saveAndOpen(r.model.snapshot(), tempFile("edited.mscz"), again, &report);
    EXPECT_EQ(report.detailsDropped, 1);   // only the edited note's
    EXPECT_EQ(problems(again.model.snapshot()), "");
    EXPECT_EQ(io::musicListing(again.model.snapshot()), io::musicListing(r.model.snapshot()));

    // undone, the score saves exactly as before the edit
    history.undo(r.model);
    io::WriteReport undone;
    EXPECT_EQ(io::saveBytes(io::generateScoreFile(r.model.snapshot(), &undone)), before);
    EXPECT_EQ(undone.detailsDropped, 0);
}

TEST(ModelWriter, RemovedAndAddedMusicIsSaved)
{
    Ensemble e;
    e.build();
    // replace the cello's last quarter with a rest, and add a note to the horn's first chord
    const model::Object* lastCello = nullptr;
    for (Id c : e.model.find(e.cello.voice)->children().ids()) {
        lastCello = e.model.find(c);
    }
    ASSERT_NE(lastCello, nullptr);
    const Fraction t = lastCello->fractionOr(F::TIME, Fraction());
    const Id hornFirst = e.model.find(e.horn.voice)->children().ids().front();
    {
        Transaction tx(e.model, "Edit");
        removeTree(tx, lastCello->id());
        e.rest(tx, e.cello.voice, t, Fraction(1, 4));
        create(tx, e.ids, hornFirst, { Kind::Note, { { F::PITCH, int64_t(72) } } });
        e.history.push(tx.commit());
    }
    ASSERT_EQ(problems(e.model.snapshot()), "");
    Reopened r;
    saveAndOpen(e.model.snapshot(), tempFile("edited_ensemble.mscz"), r);
    EXPECT_EQ(problems(r.model.snapshot()), "");
    EXPECT_EQ(io::musicListing(r.model.snapshot()), io::musicListing(e.model.snapshot()));
}

TEST(ModelWriter, JubelleStyleSettingsSurviveSavingAndReopening)
{
    Ensemble e;
    e.build();
    // all 11 with values other than their defaults
    const std::vector<std::pair<std::string, std::string> > values {
        { "jubelleCourtesyAccidentals", "1" }, { "jubelleCourtesyAccidentalsAllOctaves", "1" },
        { "jubelleCourtesyAccidentalsBars", "3" }, { "jubelleCourtesyAccidentalsOtherStaves", "1" },
        { "jubelleCourtesyAccidentalsParentheses", "1" }, { "jubelleMagneticLayout", "1" }, { "jubelleMagneticRows", "0" },
        { "jubelleSidewaysNudge", "0" }, { "jubelleNudgeLimit", "0.75" }, { "jubelleCollisionAvoidance", "1" },
        { "jubelleEngravingRules", "store-test" },
    };
    ASSERT_EQ(values.size(), io::jubelleStyleNames().size());
    {
        Transaction tx(e.model, "Style");
        tx.set(e.model.score(), F::STYLE, Text(io::packPairs(values)));
        e.history.push(tx.commit());
    }
    const fs::path path = tempFile("style.mscz");
    Reopened r;
    saveAndOpen(e.model.snapshot(), path, r);
    const auto back = io::unpackPairs(r.model.find(r.model.score())->text(F::STYLE));
    for (const auto& v : values) {
        EXPECT_NE(std::find(back.begin(), back.end(), v), back.end()) << v.first;
    }
    // the engine store holds them too, for when MuseScore saves the score and drops them
    io::ScoreFile f;
    std::string error;
    ASSERT_TRUE(io::load(path.string(), f, error)) << error;
    for (const auto& v : values) {
        EXPECT_NE(io::metaTag(f, "jubelleEngine").find(v.first), std::string::npos) << v.first;
    }
    // MuseScore 4.7.7 drops unknown style values on saving (docs/13); reopening restores them from the store
    for (io::xml::Node* style : { io::styleNode(f), f.score()->child("Style") }) {
        auto& kids = style->children;
        kids.erase(std::remove_if(kids.begin(), kids.end(), [](const io::xml::Node& n) { return n.name.rfind("jubelle", 0) == 0; }),
                   kids.end());
    }
    const fs::path dropped = tempFile("style_dropped.mscz");
    ASSERT_TRUE(io::save(dropped.string(), f));
    Reopened r2;
    ASSERT_TRUE(io::openScore(dropped.string(), r2.model, r2.ids, error)) << error;
    const auto restored = io::unpackPairs(r2.model.find(r2.model.score())->text(F::STYLE));
    for (const auto& v : values) {
        EXPECT_NE(std::find(restored.begin(), restored.end(), v), restored.end()) << v.first;
    }
}

TEST(ModelWriter, ScoreWithoutPartsHasNoIdentitiesAdded)
{
    // identities are only given when part layouts need them to link
    Ensemble e;
    e.build();
    {
        Transaction tx(e.model, "No parts");
        const model::Snapshot before = e.model.snapshot();
        for (const model::Object* l : all(before, Kind::Layout)) {
            removeTree(tx, l->id());
        }
        e.history.push(tx.commit());
    }
    io::ScoreFile f = io::generateScoreFile(e.model.snapshot());
    EXPECT_EQ(io::xml::write(f.main).find("<eid>"), std::string::npos);
    EXPECT_TRUE(f.excerpts.empty());
}

namespace {
// ties to another voice, to a grace note and within a unison; a slur kept inside its chords (MuseScore's places)
const char* LINKS = R"(<?xml version="1.0" encoding="UTF-8"?>
<museScore version="4.70">
  <Score>
    <Division>480</Division>
    <Part id="1">
      <Staff id="1">
        <StaffType group="pitched">
          <name>stdNormal</name>
          </StaffType>
        </Staff>
      <trackName>Piano</trackName>
      <Instrument id="piano">
        <longName>Piano</longName>
        </Instrument>
      </Part>
    <Staff id="1">
      <Measure>
        <voice>
          <Chord>
            <durationType>eighth</durationType>
            <acciaccatura/>
            <Note>
              <Spanner type="Tie">
                <Tie>
                  </Tie>
                <next>
                  <location>
                    <grace>1</grace>
                    </location>
                  </next>
                </Spanner>
              <pitch>59</pitch>
              </Note>
            </Chord>
          <Chord>
            <durationType>eighth</durationType>
            <acciaccatura/>
            <Note>
              <Spanner type="Tie">
                <prev>
                  <location>
                    <grace>0</grace>
                    </location>
                  </prev>
                </Spanner>
              <pitch>59</pitch>
              </Note>
            </Chord>
          <Chord>
            <durationType>half</durationType>
            <Spanner type="Slur">
              <Slur>
                </Slur>
              <next>
                <location>
                  <fractions>1/2</fractions>
                  </location>
                </next>
              </Spanner>
            <Note>
              <Spanner type="Tie">
                <Tie>
                  </Tie>
                <next>
                  <location>
                    <fractions>1/2</fractions>
                    </location>
                  </next>
                </Spanner>
              <pitch>72</pitch>
              </Note>
            <Note>
              <Spanner type="Tie">
                <Tie>
                  </Tie>
                <next>
                  <location>
                    <fractions>1/2</fractions>
                    </location>
                  </next>
                </Spanner>
              <pitch>72</pitch>
              </Note>
            <Note>
              <Spanner type="Tie">
                <Tie>
                  </Tie>
                <next>
                  <location>
                    <voices>1</voices>
                    <fractions>1/2</fractions>
                    </location>
                  </next>
                </Spanner>
              <pitch>76</pitch>
              </Note>
            </Chord>
          <Chord>
            <durationType>half</durationType>
            <Spanner type="Slur">
              <prev>
                <location>
                  <fractions>-1/2</fractions>
                  </location>
                </prev>
              </Spanner>
            <Note>
              <pitch>72</pitch>
              </Note>
            <Note>
              <pitch>72</pitch>
              </Note>
            </Chord>
          </voice>
        <voice>
          <Rest>
            <durationType>half</durationType>
            </Rest>
          <Chord>
            <durationType>half</durationType>
            <Note>
              <pitch>76</pitch>
              </Note>
            </Chord>
          </voice>
        </Measure>
      </Staff>
    </Score>
  </museScore>
)";
}

TEST(ModelWriter, TiesAndSlursKeepTheirPlaces)
{
    io::ScoreFile f;
    std::string error;
    ASSERT_TRUE(io::loadBytes(LINKS, true, f, error)) << error;
    model::Model m;
    IdGenerator ids(1);
    io::ReadReport r;
    ASSERT_TRUE(io::readScore(f, m, ids, r, error)) << error;
    EXPECT_EQ(r.ties, 4);   // the grace-to-grace tie, both unison notes, and the one to voice 2
    EXPECT_TRUE(r.warnings.empty()) << r.warnings.front();
    std::set<Id> targets;
    for (const model::Object* n : all(m.snapshot(), Kind::Note)) {
        if (n->has(F::TIE_TO)) {
            targets.insert(n->idOr(F::TIE_TO));
        }
    }
    EXPECT_EQ(targets.size(), 4u);   // each tie its own end
    const std::string out = io::xml::write(io::generateScoreFile(m.snapshot()).main);
    // the slur inside its chords, the locations naming the grace note and the voice
    const size_t chord = out.find("<durationType>half</durationType>");
    ASSERT_NE(chord, std::string::npos);
    EXPECT_LT(out.find("<Spanner type=\"Slur\">", chord), out.find("<Note>", chord));
    EXPECT_NE(out.find("<grace>1</grace>"), std::string::npos);
    EXPECT_NE(out.find("<grace>0</grace>"), std::string::npos);
    EXPECT_NE(out.find("<voices>1</voices>"), std::string::npos);
    EXPECT_NE(out.find("<voices>-1</voices>"), std::string::npos);
    // read again: the same ties and slur (the file gained spellings and a time signature, which it had none of);
    // from then on, saving and reading again changes nothing
    auto reread = [&error](const std::string& xml, model::Model& into) {
        io::ScoreFile again;
        IdGenerator g(1);
        io::ReadReport rr;
        return io::loadBytes(xml, true, again, error) && io::readScore(again, into, g, rr, error) && rr.ties == 4;
    };
    model::Model m2, m3;
    ASSERT_TRUE(reread(out, m2)) << error;
    EXPECT_EQ(io::musicListing(m2.snapshot()), io::musicListing(m.snapshot()));
    ASSERT_TRUE(reread(io::xml::write(io::generateScoreFile(m2.snapshot()).main), m3)) << error;
    EXPECT_EQ(io::modelDump(m3.snapshot()), io::modelDump(m2.snapshot()));
}

TEST(ModelWriter, LinesFromNoteToNoteStayWholeWhenTheirNotesAreEdited)
{
    // glissandi between notes (a guitar part with a linked tablature): modelled as lines between two notes
    const fs::path source = fs::path(JE_REPO_DIR) / "src" / "engraving" / "tests" / "midi" / "midirenderer_data" / "simple_glissando_legato.mscx";
    ASSERT_TRUE(fs::exists(source));
    Reopened r;
    std::string error;
    ASSERT_TRUE(io::openScore(source.string(), r.model, r.ids, error)) << error;
    auto glissandi = [](const model::Snapshot& s) {
        std::vector<const model::Object*> out;
        for (const model::Object* o : all(s, Kind::Spanner)) {
            if (o->text(F::TAG) == "Glissando") {
                out.push_back(o);
            }
        }
        return out;
    };
    const model::Snapshot read = r.model.snapshot();
    ASSERT_EQ(glissandi(read).size(), 2u);   // one on each staff
    for (const model::Object* g : glissandi(read)) {
        EXPECT_EQ(read.find(g->idOr(F::ANCHOR))->kind(), Kind::Note);
        EXPECT_EQ(read.find(g->idOr(F::END_ANCHOR))->kind(), Kind::Note);
        EXPECT_FALSE(read.find(g->idOr(F::ANCHOR))->has(F::LINE_DETAILS));
    }
    // a new pitch for the end note: the line is still there, written in both notes, and read back the same
    const Id end = glissandi(read).front()->idOr(F::END_ANCHOR);
    {
        Transaction tx(r.model, "Repitch");
        tx.set(end, F::PITCH, int64_t(read.find(end)->intOr(F::PITCH, 60) + 1));
        tx.commit();
    }
    const std::string xml = io::xml::write(io::generateScoreFile(r.model.snapshot()).main);
    size_t starts = 0, ends = 0;
    for (size_t p = xml.find("<Spanner type=\"Glissando\">"); p != std::string::npos; p = xml.find("<Spanner type=\"Glissando\">", p + 1)) {
        (xml.compare(xml.find('<', p + 1), 11, "<Glissando>") == 0 ? starts : ends)++;
    }
    EXPECT_EQ(starts, 2u);
    EXPECT_EQ(ends, 2u);
    io::ScoreFile again;
    ASSERT_TRUE(io::loadBytes(xml, true, again, error)) << error;
    model::Model m2;
    IdGenerator ids2 { 1 };
    io::ReadReport rr;
    ASSERT_TRUE(io::readScore(again, m2, ids2, rr, error)) << error;
    EXPECT_EQ(glissandi(m2.snapshot()).size(), 2u);
    // the start note removed: its line goes with it, nothing is left half-written
    Transaction tx(r.model, "Delete");
    removeTree(tx, glissandi(r.model.snapshot()).front()->id());
    tx.commit();
    const std::string xml2 = io::xml::write(io::generateScoreFile(r.model.snapshot()).main);
    size_t total = 0;
    for (size_t p = xml2.find("<Spanner type=\"Glissando\">"); p != std::string::npos; p = xml2.find("<Spanner type=\"Glissando\">", p + 1)) {
        ++total;
    }
    EXPECT_EQ(total, 2u);
    EXPECT_TRUE(model::checkInvariants(r.model.snapshot()).empty());
}
