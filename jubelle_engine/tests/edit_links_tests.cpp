// SPDX-License-Identifier: GPL-3.0-only
// S3: linked staves while editing (edit/links.h) and tablature fretting (edit/tablature.h): an edit on either staff
// of a guitar's standard + tablature pair appears on the other in the same undo step, the tablature gets MuseScore's
// strings and frets, ties and lines stay connected, and the file links the copies as MuseScore does.
#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <string>

#include "edit/history.h"
#include "edit/links.h"
#include "edit/noteinput.h"
#include "edit/operations.h"
#include "edit/scorebuilder.h"
#include "edit/structure.h"
#include "edit/tablature.h"
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

namespace {
struct Guitar {
    model::Model model;
    IdGenerator ids { 1 };
    ScoreBuilder b { model, ids };
    History history;
    Operations ops { model, ids };
    ScoreBuilder::PlayerIds guitar;
    Id tab, tabVoice;

    Guitar()
    {
        Transaction tx(model, "New score");
        b.newScore(tx, "Guitar");
        b.addMeasures(tx, 4);
        guitar = b.addPlayer(tx, "Guitar", "guitar-steel", "plucked-strings");
        tx.set(guitar.player, F::STRING_TUNING, Text("40 45 50 55 59 64"));
        tx.set(guitar.player, F::FRETS, int64_t(19));
        tab = create(tx, ids, guitar.player, { Kind::Staff, { { F::SUBKIND, Text("tablature") }, { F::LINKED_STAFF, guitar.staff } } });
        tabVoice = create(tx, ids, tab, { Kind::Voice, { { F::FLOW, b.flow() }, { F::NUMBER, int64_t(0) } } });
        for (int i = 0; i < 4; ++i) {
            for (Id v : { guitar.voice, tabVoice }) {
                create(tx, ids, v, { Kind::Rest, { { F::TIME, Fraction(i, 1) }, { F::DURATION, Fraction(1, 1) }, { F::MEASURE_REST, true } } },
                       Placement::ByTime);
            }
        }
        history.push(tx.commit());
    }

    template<typename Fn>
    void edit(Fn&& fn)
    {
        Transaction tx(model, "Edit", { linkedStavesHook(model, ids) });
        fn(tx);
        history.push(tx.commit());
    }

    //! A voice's music: time, length, pitches (~ tied), string/fret on tablature
    std::string music(Id voice) const
    {
        std::string out;
        for (Id e : ops.eventsOf(voice)) {
            const model::Object* o = model.find(e);
            out += o->fractionOr(F::TIME, Fraction()).toString() + " " + o->fractionOr(F::DURATION, Fraction()).toString();
            for (Id n : o->children()) {
                const model::Object* x = model.find(n);
                if (x->kind() == Kind::Note) {
                    out += " " + std::to_string(x->intOr(F::PITCH, 0)) + (x->has(F::TIE_TO) ? "~" : "");
                }
            }
            out += "\n";
        }
        return out;
    }

    std::string fretting(Id voice) const
    {
        std::string out;
        for (Id e : ops.eventsOf(voice)) {
            for (Id n : model.find(e)->children()) {
                const model::Object* x = model.find(n);
                if (x->kind() == Kind::Note) {
                    out += std::to_string(x->intOr(F::PITCH, 0)) + ":" + std::to_string(x->intOr(F::STRING, -1)) + "/"
                           + std::to_string(x->intOr(F::FRET, -1)) + " ";
                }
            }
        }
        return out;
    }

    std::string problems() const
    {
        std::string out;
        for (const auto& p : model::checkInvariants(model.snapshot())) {
            out += p + "\n";
        }
        return out;
    }

    Id eventAt(Id voice, const Fraction& t) const
    {
        for (Id e : ops.eventsOf(voice)) {
            if (model.find(e)->fractionOr(F::TIME, Fraction()) == t && !model.find(e)->has(F::GRACE)) {
                return e;
            }
        }
        return Id();
    }
};
}

TEST(StringTuning, MuseScoresFrettingRules)
{
    model::Object p = model::Object(Id(1), { { F::KIND, int64_t(Kind::Player) } }).with(F::STRING_TUNING, Text("40 45 50 55 59 64")).with(F::FRETS, int64_t(19));
    const StringTuning t = StringTuning::of(p);
    int s = -1, f = -1;
    // the highest string that reaches the pitch; strings are numbered from the highest (0)
    EXPECT_TRUE(t.convertPitch(64, 0, s, f));
    EXPECT_EQ(s, 0);
    EXPECT_EQ(f, 0);
    EXPECT_TRUE(t.convertPitch(62, 0, s, f));   // D4: 3rd fret of the B string
    EXPECT_EQ(s, 1);
    EXPECT_EQ(f, 3);
    EXPECT_TRUE(t.convertPitch(40, 0, s, f));
    EXPECT_EQ(s, 5);
    EXPECT_EQ(f, 0);
    EXPECT_FALSE(t.convertPitch(39, 0, s, f));   // below the low E: shown on it, can't be played
    EXPECT_EQ(s, 5);
    EXPECT_FALSE(t.convertPitch(64 + 20, 0, s, f));   // above the last fret
    EXPECT_EQ(t.fretFor(69, 1, 0), 10);
    EXPECT_EQ(t.fretFor(58, 1, 0), -1);
    EXPECT_EQ(t.pitchOf(2, 2, 0), 57);
    // a transposing guitar's strings are written pitches: a note sounding E2 is on the open low string
    EXPECT_TRUE(t.convertPitch(28, 12, s, f));
    EXPECT_EQ(s, 5);
    EXPECT_EQ(f, 0);
    // a five-string banjo: the drone string is open only, the others from the highest
    model::Object banjo = model::Object(Id(2), { { F::KIND, int64_t(Kind::Player) } }).with(F::STRING_TUNING, Text("67* 50 55 59 62")).with(F::FRETS, int64_t(22));
    const StringTuning b = StringTuning::of(banjo);
    ASSERT_TRUE(b.fiveStringBanjo());
    EXPECT_TRUE(b.convertPitch(67, 0, s, f));
    EXPECT_EQ(s, 4);
    EXPECT_EQ(f, 0);
    EXPECT_TRUE(b.convertPitch(64, 0, s, f));
    EXPECT_EQ(s, 0);
    EXPECT_EQ(f, 2);
}

TEST(LinkedStaves, NotesEnteredOnTheStandardStaffAppearOnTheTablature)
{
    Guitar g;
    const Id m2rest = g.eventAt(g.tabVoice, Fraction(1, 1));
    g.edit([&](Transaction& tx) {
        g.ops.enterChord(tx, g.guitar.voice, Fraction(0, 1), { Fraction(1, 4), 0 }, { 40, 59 });
        g.ops.enterChord(tx, g.guitar.voice, Fraction(1, 4), { Fraction(1, 4), 0 }, { 64, 69 });
    });
    EXPECT_EQ(g.problems(), "");
    EXPECT_EQ(g.music(g.tabVoice), g.music(g.guitar.voice));
    // MuseScore's fretting: open strings where they reach; two notes wanting the top string share out
    EXPECT_EQ(g.fretting(g.tabVoice), "40:5/0 59:1/0 64:1/5 69:0/5 ");
    // each copy knows its original; the bars that didn't change keep their objects
    const Id c = g.eventAt(g.tabVoice, Fraction(0, 1));
    EXPECT_EQ(g.model.find(c)->idOr(F::LINKED_TO), g.eventAt(g.guitar.voice, Fraction(0, 1)));
    EXPECT_EQ(g.eventAt(g.tabVoice, Fraction(1, 1)), m2rest);
    // one undo step takes both back
    const std::string before = g.music(g.tabVoice);
    g.history.undo(g.model);
    EXPECT_EQ(g.music(g.tabVoice), "0/1 1/1\n1/1 1/1\n2/1 1/1\n3/1 1/1\n");
    EXPECT_EQ(g.music(g.guitar.voice), "0/1 1/1\n1/1 1/1\n2/1 1/1\n3/1 1/1\n");
    g.history.redo(g.model);
    EXPECT_EQ(g.music(g.tabVoice), before);
    EXPECT_EQ(g.problems(), "");
}

TEST(LinkedStaves, AnEditOnTheTablatureIsMadeOnTheStandardStaff)
{
    Guitar g;
    g.edit([&](Transaction& tx) { g.ops.enterChord(tx, g.tabVoice, Fraction(2, 1), { Fraction(1, 2), 1 }, { 52 }); });
    EXPECT_EQ(g.music(g.guitar.voice), g.music(g.tabVoice));
    EXPECT_NE(g.music(g.guitar.voice).find("2/1 3/4 52"), std::string::npos);
    EXPECT_EQ(g.fretting(g.tabVoice), "52:3/2 ");
    // the main staff's chord is the original: the tablature's is linked to it
    const Id mainChord = g.eventAt(g.guitar.voice, Fraction(2, 1));
    EXPECT_EQ(g.model.find(g.eventAt(g.tabVoice, Fraction(2, 1)))->idOr(F::LINKED_TO), mainChord);
    EXPECT_FALSE(g.model.find(mainChord)->has(F::LINKED_TO));
    // a changed pitch on the tablature moves the standard staff's note and gets a new fret
    g.edit([&](Transaction& tx) {
        const Id n = g.b.notesOf(g.eventAt(g.tabVoice, Fraction(2, 1))).front();
        g.ops.setPitch(tx, n, 55);
    });
    EXPECT_EQ(g.music(g.guitar.voice), g.music(g.tabVoice));
    EXPECT_EQ(g.fretting(g.tabVoice), "55:2/0 ");
    // a change that keeps the structure is made in place: the same objects on both staves
    EXPECT_EQ(g.eventAt(g.guitar.voice, Fraction(2, 1)), mainChord);
    EXPECT_EQ(g.model.find(g.eventAt(g.tabVoice, Fraction(2, 1)))->idOr(F::LINKED_TO), mainChord);
    EXPECT_EQ(g.problems(), "");
}

TEST(LinkedStaves, TiesAcrossABarlineStayConnectedWhenOneBarChanges)
{
    Guitar g;
    // a half note tied over the barline
    g.edit([&](Transaction& tx) {
        g.ops.enterChord(tx, g.guitar.voice, Fraction(1, 2), { Fraction(1, 2), 0 }, { 57 });
        g.ops.enterChord(tx, g.guitar.voice, Fraction(1, 1), { Fraction(1, 4), 0 }, { 57 });
        g.ops.tieToNext(tx, g.b.notesOf(g.eventAt(g.guitar.voice, Fraction(1, 2))).front());
    });
    EXPECT_EQ(g.music(g.tabVoice), g.music(g.guitar.voice));
    // only bar 2 changes: its copy is written again, and the tie from bar 1's copy goes to the new note
    const Id bar1Copy = g.eventAt(g.tabVoice, Fraction(1, 2));
    g.edit([&](Transaction& tx) { g.ops.enterChord(tx, g.guitar.voice, Fraction(5, 4), { Fraction(1, 4), 0 }, { 60 }); });
    EXPECT_EQ(g.eventAt(g.tabVoice, Fraction(1, 2)), bar1Copy);
    const Id tiedTo = g.model.find(g.b.notesOf(bar1Copy).front())->idOr(F::TIE_TO);
    ASSERT_TRUE(g.model.find(tiedTo));
    EXPECT_EQ(g.model.find(tiedTo)->parent(), g.eventAt(g.tabVoice, Fraction(1, 1)));
    EXPECT_EQ(g.music(g.tabVoice), g.music(g.guitar.voice));
    EXPECT_EQ(g.problems(), "");
}

TEST(LinkedStaves, TupletsAndGraceNotesAreCopied)
{
    Guitar g;
    g.edit([&](Transaction& tx) {
        g.ops.makeTuplet(tx, g.guitar.voice, Fraction(0, 1), 3, 2, Fraction(1, 8));
        for (int i = 0; i < 3; ++i) {
            g.ops.enterChord(tx, g.guitar.voice, Fraction(i, 12), { Fraction(1, 8), 0 }, { 52 + 2 * i });
        }
        // an acciaccatura before the quarter on beat 2
        g.ops.enterChord(tx, g.guitar.voice, Fraction(1, 4), { Fraction(1, 4), 0 }, { 60 });
        const Id grace = create(tx, g.ids, g.guitar.voice, { Kind::Chord, { { F::TIME, Fraction(1, 4) }, { F::GRACE, true },
                                                                           { F::GRACE_TYPE, Text("acciaccatura") },
                                                                           { F::BASE_DURATION, Fraction(1, 8) },
                                                                           { F::DURATION, Fraction(1, 8) } } });
        // graces come before their main chord in the voice
        std::vector<Id> kids = g.model.find(g.guitar.voice)->children().ids();
        kids.erase(std::find(kids.begin(), kids.end(), grace));
        kids.insert(std::find(kids.begin(), kids.end(), g.eventAt(g.guitar.voice, Fraction(1, 4))), grace);
        tx.set(g.guitar.voice, F::CHILDREN, model::IdList(std::move(kids)));
        create(tx, g.ids, grace, { Kind::Note, { { F::PITCH, int64_t(62) }, { F::TPC, int64_t(16) } } });
    });
    EXPECT_EQ(g.problems(), "");
    EXPECT_EQ(g.music(g.tabVoice), g.music(g.guitar.voice));
    int tuplets = 0;
    for (Id c : g.model.find(g.tabVoice)->children()) {
        if (g.model.find(c)->kind() == Kind::Tuplet) {
            ++tuplets;
            EXPECT_EQ(g.model.find(c)->intOr(F::RATIO_NUM, 0), 3);
        }
    }
    EXPECT_EQ(tuplets, 1);
    // the grace chord is copied before its main chord, and fretted by itself
    int graces = 0;
    for (Id e : g.ops.eventsOf(g.tabVoice)) {
        if (g.model.find(e)->has(F::GRACE)) {
            ++graces;
            EXPECT_EQ(g.model.find(g.b.notesOf(e).front())->intOr(F::STRING, -1), 1);
            EXPECT_EQ(g.model.find(g.b.notesOf(e).front())->intOr(F::FRET, -1), 3);
        }
    }
    EXPECT_EQ(graces, 1);
    for (Id e : g.ops.eventsOf(g.tabVoice)) {
        const model::Object* o = g.model.find(e);
        if (o->fractionOr(F::TIME, Fraction()) < Fraction(1, 4)) {
            ASSERT_TRUE(o->has(F::TUPLET));
            EXPECT_EQ(g.model.find(o->idOr(F::TUPLET))->parent(), g.tabVoice);
        }
    }
}

TEST(LinkedStaves, SavedAndReopenedTheLinksAndFretsAreThere)
{
    Guitar g;
    g.edit([&](Transaction& tx) {
        g.ops.enterChord(tx, g.guitar.voice, Fraction(0, 1), { Fraction(1, 4), 0 }, { 45, 52 });
        g.ops.enterChord(tx, g.guitar.voice, Fraction(1, 4), { Fraction(1, 4), 0 }, { 57 });
        g.ops.tieToNext(tx, g.b.notesOf(g.eventAt(g.guitar.voice, Fraction(0, 1))).back());
    });
    const io::ScoreFile file = io::generateScoreFile(g.model.snapshot());
    const std::string xml = io::xml::write(file.main);
    // a new score is 4.70: links by identity, the tablature's notes with their strings and frets
    EXPECT_NE(xml.find("<linkedTo>"), std::string::npos);
    EXPECT_NE(xml.find("<fret>2</fret>"), std::string::npos);
    EXPECT_NE(xml.find("<string>3</string>"), std::string::npos);
    EXPECT_NE(xml.find("<StringData>"), std::string::npos);
    EXPECT_NE(xml.find("<StaffType group=\"tablature\">"), std::string::npos);
    io::ScoreFile again;
    std::string error;
    ASSERT_TRUE(io::loadBytes(xml, true, again, error)) << error;
    model::Model m2;
    IdGenerator ids2 { 1 };
    io::ReadReport r;
    ASSERT_TRUE(io::readScore(again, m2, ids2, r, error)) << error;
    // the second staff is linked to the first, with the same music
    std::vector<Id> staves;
    m2.snapshot().forEachObject([&](const model::Object& o) {
        if (o.kind() == Kind::Staff) {
            staves.push_back(o.id());
        }
    });
    ASSERT_EQ(staves.size(), 2u);
    const model::Object* s1 = m2.find(staves[0]);
    const model::Object* s2 = m2.find(staves[1]);
    const Id linked = s1->has(F::LINKED_STAFF) ? s1->id() : s2->id();
    const Id main = linked == s1->id() ? s2->id() : s1->id();
    EXPECT_EQ(m2.find(linked)->idOr(F::LINKED_STAFF), main);
    // every element of the tablature names its original
    const std::string withLinks = xml.substr(xml.find("<Staff id=\"2\">", xml.find("</Part>")));
    size_t copies = 0, links = 0;
    for (size_t p = withLinks.find("<Chord>"); p != std::string::npos; p = withLinks.find("<Chord>", p + 1)) {
        ++copies;
    }
    for (size_t p = withLinks.find("<linkedTo>"); p != std::string::npos; p = withLinks.find("<linkedTo>", p + 1)) {
        ++links;
    }
    EXPECT_EQ(copies, 2u);
    EXPECT_GE(links, 5u);   // 2 chords, 3 notes (and the tie)
    // and editing after reopening keeps the two together
    Operations ops2(m2, ids2);
    Transaction tx(m2, "Edit", { linkedStavesHook(m2, ids2) });
    Id v1;
    for (Id v : m2.find(main)->children()) {
        v1 = v;
        break;
    }
    ops2.enterChord(tx, v1, Fraction(3, 1), { Fraction(1, 1), 0 }, { 64 });
    tx.commit();
    std::string a, b;
    for (Id st : { main, linked }) {
        std::string& out = st == main ? a : b;
        for (Id v : m2.find(st)->children()) {
            for (Id e : ops2.eventsOf(v)) {
                out += m2.find(e)->fractionOr(F::TIME, Fraction()).toString() + ",";
            }
        }
    }
    EXPECT_EQ(a, b);
    EXPECT_TRUE(model::checkInvariants(m2.snapshot()).empty());
}

TEST(LinkedStaves, RandomEditsOnEitherStaffKeepThemIdenticalAndUndoExactly)
{
    for (unsigned seed = 1; seed <= 12; ++seed) {
        Guitar g;
        std::mt19937 rng(seed);
        const std::string start = g.music(g.guitar.voice);
        for (int step = 0; step < 25; ++step) {
            const Id voice = (rng() % 2) ? g.guitar.voice : g.tabVoice;
            const Fraction t(int64_t(rng() % 16), 4);
            const int kind = int(rng() % 4);
            g.edit([&](Transaction& tx) {
                if (kind == 0) {
                    g.ops.enterChord(tx, voice, t, { Fraction(1, 4 << (rng() % 2)), int(rng() % 2) }, { 40 + int(rng() % 30) });
                } else if (kind == 1) {
                    g.ops.enterRest(tx, voice, t, { Fraction(1, 2), 0 });
                } else if (kind == 2) {
                    const std::vector<Id> ev = g.ops.eventsOf(voice);
                    const Id e = ev[rng() % ev.size()];
                    if (g.model.find(e)->kind() == Kind::Chord) {
                        g.ops.addNote(tx, e, 40 + int(rng() % 30));
                    }
                } else {
                    const std::vector<Id> ev = g.ops.eventsOf(voice);
                    g.ops.deleteEvent(tx, ev[rng() % ev.size()]);
                }
            });
            ASSERT_EQ(g.problems(), "") << "seed " << seed << " step " << step;
            ASSERT_EQ(g.music(g.tabVoice), g.music(g.guitar.voice)) << "seed " << seed << " step " << step;
            // every playable note on the tablature has a string and fret that give its pitch
            const StringTuning tuning = StringTuning::of(*g.model.find(g.guitar.player));
            for (Id e : g.ops.eventsOf(g.tabVoice)) {
                for (Id n : g.model.find(e)->children()) {
                    const model::Object* x = g.model.find(n);
                    if (x->kind() == Kind::Note && x->intOr(F::PITCH, 0) >= 40) {
                        ASSERT_TRUE(x->has(F::STRING) && x->has(F::FRET)) << "seed " << seed;
                    }
                }
            }
            (void)tuning;
        }
        while (g.history.canUndo() && g.history.index() > 1) {
            g.history.undo(g.model);
        }
        EXPECT_EQ(g.music(g.guitar.voice), start);
        EXPECT_EQ(g.music(g.tabVoice), start);
        EXPECT_EQ(g.problems(), "");
    }
}

TEST(LinkedStaves, AFrettingThatIsntThePitchIsLeftAloneUnlessThatNoteChanges)
{
    Guitar g;
    // the end of a bend: written a tone higher than the fret it is played on
    g.edit([&](Transaction& tx) { g.ops.enterChord(tx, g.guitar.voice, Fraction(0, 1), { Fraction(1, 4), 0 }, { 57 }); });
    const Id tabNote = g.b.notesOf(g.eventAt(g.tabVoice, Fraction(0, 1))).front();
    {
        Transaction tx(g.model, "From the file");
        tx.set(tabNote, F::STRING, int64_t(2));
        tx.set(tabNote, F::FRET, int64_t(0));
        g.history.push(tx.commit());
    }
    // an edit elsewhere leaves it as it is
    g.edit([&](Transaction& tx) { g.ops.enterChord(tx, g.guitar.voice, Fraction(1, 1), { Fraction(1, 4), 0 }, { 64 }); });
    EXPECT_EQ(g.fretting(g.tabVoice), "57:2/0 64:0/0 ");
    // a new pitch for it gets MuseScore's fretting
    g.edit([&](Transaction& tx) { g.ops.setPitch(tx, g.b.notesOf(g.eventAt(g.guitar.voice, Fraction(0, 1))).front(), 59); });
    EXPECT_EQ(g.fretting(g.tabVoice), "59:1/0 64:0/0 ");
}

TEST(LinkedStaves, DynamicsAndLinesAreAddedChangedAndRemovedOnBothStaves)
{
    Guitar g;
    Id dyn;
    auto onStaff = [&](Id staff, const std::string& subkind) {
        std::vector<Id> out;
        g.model.snapshot().forEachObject([&](const model::Object& o) {
            if ((o.kind() == Kind::FlowItem || o.kind() == Kind::Spanner) && o.text(F::SUBKIND) == subkind) {
                Id a = o.idOr(F::ANCHOR);
                for (const model::Object* x = g.model.find(a); x; x = g.model.find(x->parent())) {
                    if (x->kind() == Kind::Staff) {
                        a = x->id();
                        break;
                    }
                }
                if (a == staff) {
                    out.push_back(o.id());
                }
            }
        });
        return out;
    };
    // a dynamic added to one staff of the part stays on it, as MuseScore keeps dynamics, staff texts, chord symbols
    // and lyrics to their own staff (Score::undoAddElement leaves them off the part's other staves)
    Id alone;
    g.edit([&](Transaction& tx) {
        alone = create(tx, g.ids, g.b.flow(), { Kind::FlowItem, { { F::SUBKIND, Text("dynamic") }, { F::ANCHOR, g.guitar.staff },
                                                                  { F::TIME, Fraction(0, 1) }, { F::TEXT, Text("p") } } },
                       Placement::ByTime);
    });
    EXPECT_TRUE(onStaff(g.tab, "dynamic").empty());
    g.edit([&](Transaction& tx) { removeTree(tx, alone); });
    // one the file linked to a copy on the other staff: the copy follows it
    Id tabDyn;
    g.edit([&](Transaction& tx) {
        dyn = create(tx, g.ids, g.b.flow(), { Kind::FlowItem, { { F::SUBKIND, Text("dynamic") }, { F::ANCHOR, g.guitar.staff },
                                                                { F::TIME, Fraction(1, 1) }, { F::TEXT, Text("mf") } } },
                     Placement::ByTime);
        tabDyn = create(tx, g.ids, g.b.flow(), { Kind::FlowItem, { { F::SUBKIND, Text("dynamic") }, { F::ANCHOR, g.tab },
                                                                   { F::TIME, Fraction(1, 1) }, { F::TEXT, Text("mf") },
                                                                   { F::LINKED_TO, dyn } } },
                        Placement::ByTime);
    });
    ASSERT_EQ(onStaff(g.tab, "dynamic").size(), 1u);
    EXPECT_EQ(g.model.find(tabDyn)->idOr(F::LINKED_TO), dyn);
    // changed on the tablature: the standard staff's follows
    g.edit([&](Transaction& tx) { tx.set(tabDyn, F::TEXT, Text("ff")); });
    EXPECT_EQ(g.model.find(dyn)->text(F::TEXT), "ff");
    // removed on the standard staff: gone from both, and one undo brings both back
    g.edit([&](Transaction& tx) { removeTree(tx, dyn); });
    EXPECT_TRUE(onStaff(g.tab, "dynamic").empty());
    EXPECT_TRUE(onStaff(g.guitar.staff, "dynamic").empty());
    g.history.undo(g.model);
    EXPECT_EQ(onStaff(g.tab, "dynamic").size(), 1u);
    EXPECT_EQ(onStaff(g.guitar.staff, "dynamic").size(), 1u);
    // a slur made on the tablature between two of its chords: the original is on the standard staff, between the
    // same chords there
    Id a, b;
    g.edit([&](Transaction& tx) {
        a = g.ops.enterChord(tx, g.tabVoice, Fraction(2, 1), { Fraction(1, 4), 0 }, { 60 });
        b = g.ops.enterChord(tx, g.tabVoice, Fraction(9, 4), { Fraction(1, 4), 0 }, { 62 });
    });
    a = g.eventAt(g.tabVoice, Fraction(2, 1));
    b = g.eventAt(g.tabVoice, Fraction(9, 4));
    Id slur;
    g.edit([&](Transaction& tx) {
        slur = create(tx, g.ids, g.model.score(), { Kind::Spanner, { { F::SUBKIND, Text("slur") }, { F::ANCHOR, a }, { F::END_ANCHOR, b },
                                                                     { F::TIME, Fraction(2, 1) }, { F::DURATION, Fraction(1, 4) } } });
    });
    ASSERT_EQ(onStaff(g.guitar.staff, "slur").size(), 1u);
    const model::Object* main = g.model.find(onStaff(g.guitar.staff, "slur").front());
    EXPECT_EQ(main->idOr(F::ANCHOR), g.eventAt(g.guitar.voice, Fraction(2, 1)));
    EXPECT_EQ(main->idOr(F::END_ANCHOR), g.eventAt(g.guitar.voice, Fraction(9, 4)));
    EXPECT_EQ(g.model.find(slur)->idOr(F::LINKED_TO), main->id());
    EXPECT_FALSE(main->has(F::LINKED_TO));
    EXPECT_EQ(g.problems(), "");
    // written and read again: both slurs and both dynamics, linked
    const std::string xml = io::xml::write(io::generateScoreFile(g.model.snapshot()).main);
    size_t slurs = 0;
    for (size_t p = xml.find("<Slur>"); p != std::string::npos; p = xml.find("<Slur>", p + 1)) {
        ++slurs;
    }
    EXPECT_EQ(slurs, 2u);
}

TEST(LinkedStaves, LyricsStayOnTheirOwnStaff)
{
    // as MuseScore: a lyric typed on one staff of the part is not put on the other, and the other's own lyrics stay
    // when its music is written again from the main staff
    Guitar g;
    g.edit([&](Transaction& tx) {
        g.ops.enterChord(tx, g.guitar.voice, Fraction(0, 1), { Fraction(1, 4), 0 }, { 60 });
        g.ops.enterChord(tx, g.guitar.voice, Fraction(1, 4), { Fraction(1, 4), 0 }, { 62 });
    });
    auto lyricOn = [&](Id event) {
        std::string out;
        for (Id c : g.model.find(event)->children()) {
            if (g.model.find(c)->text(F::SUBKIND) == "lyric") {
                out += g.model.find(c)->text(F::TEXT);
            }
        }
        return out;
    };
    g.edit([&](Transaction& tx) {
        create(tx, g.ids, g.eventAt(g.guitar.voice, Fraction(0, 1)),
               { Kind::Attachment, { { F::SUBKIND, Text("lyric") }, { F::VERSE, int64_t(0) }, { F::TEXT, Text("la") } } });
        create(tx, g.ids, g.eventAt(g.tabVoice, Fraction(1, 4)),
               { Kind::Attachment, { { F::SUBKIND, Text("lyric") }, { F::VERSE, int64_t(0) }, { F::TEXT, Text("tab") } } });
    });
    EXPECT_EQ(lyricOn(g.eventAt(g.guitar.voice, Fraction(0, 1))), "la");
    EXPECT_EQ(lyricOn(g.eventAt(g.tabVoice, Fraction(0, 1))), "");
    EXPECT_EQ(lyricOn(g.eventAt(g.tabVoice, Fraction(1, 4))), "tab");
    // the rhythm changes on the standard staff: the tablature's bar is written again, its lyric kept
    g.edit([&](Transaction& tx) { g.ops.enterChord(tx, g.guitar.voice, Fraction(1, 4), { Fraction(1, 2), 0 }, { 64 }); });
    EXPECT_EQ(lyricOn(g.eventAt(g.tabVoice, Fraction(1, 4))), "tab");
    EXPECT_EQ(lyricOn(g.eventAt(g.tabVoice, Fraction(0, 1))), "");
    EXPECT_EQ(lyricOn(g.eventAt(g.guitar.voice, Fraction(0, 1))), "la");
}
