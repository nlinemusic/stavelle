/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine: tests for the play order (perform::RepeatList). The cases follow MuseScore's repeat tests
 * (src/engraving/tests/repeat_tests.cpp) and Jubelle's "Play this repeat" tests (jubelle_repeats_tests.cpp); every
 * one of MuseScore's 72 repeat test files and every corpus score is also compared with MuseScore's own unwinding
 * (tools/jubelle_checks/je_repeats_reference.py, in CI).
 */
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "edit/scorebuilder.h"
#include "edit/structure.h"
#include "model/fields.h"
#include "model/model.h"
#include "perform/repeatlist.h"

using namespace je;
using namespace je::edit;
using je::model::Kind;
namespace F = je::model::fields;

namespace {
struct Doc {
    model::Model model;
    IdGenerator ids { 1 };
    ScoreBuilder b { model, ids };
    ScoreBuilder::PlayerIds flute;
    std::vector<Id> measures;
    int64_t seq = 0;

    explicit Doc(int count)
    {
        Transaction tx(model, "New score");
        b.newScore(tx, "Repeats");
        b.addMeasures(tx, count, 4, 4);
        flute = b.addPlayer(tx, "Flute", "flute", "woodwinds", 1);
        tx.commit();
        for (Id c : model.find(b.flow())->children()) {
            if (model.find(c)->kind() == Kind::Measure) {
                measures.push_back(c);
            }
        }
    }
    template<typename Fn>
    void edit(Fn&& fn)
    {
        Transaction tx(model, "edit");
        fn(tx);
        tx.commit();
    }
    void startRepeat(int m)
    {
        edit([&](Transaction& tx) { tx.set(measures[size_t(m - 1)], F::REPEAT_START, true); });
    }
    void endRepeat(int m, int count = 2)
    {
        edit([&](Transaction& tx) { tx.set(measures[size_t(m - 1)], F::REPEAT_END, int64_t(count)); });
    }
    void sectionBreak(int m)
    {
        edit([&](Transaction& tx) { tx.set(measures[size_t(m - 1)], F::BREAK, model::Text(std::string("section"))); });
    }
    //! Jubelle's "Play this repeat" switched off on the end repeat of measure m
    void dontPlayRepeat(int m)
    {
        edit([&](Transaction& tx) {
            create(tx, ids, b.flow(), { Kind::FlowItem, { { F::SUBKIND, model::Text(std::string("measureDetail")) },
                                                          { F::ANCHOR, flute.staff }, { F::TIME, Fraction(m - 1, 1) },
                                                          { F::SEQ, ++seq },
                                                          { F::DETAILS, model::Text(std::string(
                                                                            "<x>\n<jubellePlayRepeat>0</jubellePlayRepeat>\n</x>\n")) } } },
                   Placement::ByTime);
        });
    }
    //! a volta over measures first … last (1-based), endings "1" or "1, 2"; closed: with a hook at the end
    void volta(int first, int last, const std::string& endings, bool closed = true)
    {
        edit([&](Transaction& tx) {
            create(tx, ids, model.score(), { Kind::Spanner, { { F::SUBKIND, model::Text(std::string("volta")) },
                                                              { F::ANCHOR, flute.staff }, { F::TIME, Fraction(first - 1, 1) },
                                                              { F::DURATION, Fraction(last - first + 1, 1) },
                                                              { F::TEXT, model::Text(endings) }, { F::SEQ, ++seq },
                                                              { F::DETAILS, model::Text(std::string(closed ? "<x>\n<endHookType>1</endHookType>\n</x>\n"
                                                                                                           : "<x>\n</x>\n")) } } });
        });
    }
    void flowItem(int m, const std::string& subkind, const std::string& details)
    {
        edit([&](Transaction& tx) {
            create(tx, ids, b.flow(), { Kind::FlowItem, { { F::SUBKIND, model::Text(subkind) }, { F::ANCHOR, flute.staff },
                                                          { F::HOME_STAFF, flute.staff }, { F::TIME, Fraction(m - 1, 1) },
                                                          { F::SEQ, ++seq }, { F::DETAILS, model::Text("<x>\n" + details + "</x>\n") } } },
                   Placement::ByTime);
        });
    }
    void marker(int m, const std::string& label, const std::string& type)
    {
        flowItem(m, "marker", "<label>" + label + "</label>\n" + (type.empty() ? "" : "<markerType>" + type + "</markerType>\n"));
    }
    void jump(int m, const std::string& to, const std::string& until, const std::string& continueAt)
    {
        flowItem(m, "jump", "<jumpTo>" + to + "</jumpTo>\n<playUntil>" + until + "</playUntil>\n<continueAt>" + continueAt
                 + "</continueAt>\n");
    }
    //! the measures played, 1-based, as MuseScore's tests write them
    std::string played(bool expand = true) const
    {
        std::string out;
        const perform::RepeatList list = perform::RepeatList::build(model.snapshot(), Id(), expand);
        for (const perform::RepeatSegment& rs : list.segments()) {
            for (size_t m : rs.measures) {
                out += (out.empty() ? "" : ";") + std::to_string(m + 1);
            }
        }
        return out;
    }
};
}

TEST(RepeatList, WithoutRepeatsEveryMeasureIsPlayedOnce)
{
    Doc d(4);
    EXPECT_EQ(d.played(), "1;2;3;4");
}

TEST(RepeatList, ARepeatIsPlayedItsNumberOfTimes)
{
    Doc d(6);
    d.startRepeat(2);
    d.endRepeat(3);
    EXPECT_EQ(d.played(), "1;2;3;2;3;4;5;6");   // MuseScore's repeat01
    Doc t(4);
    t.endRepeat(2, 3);
    EXPECT_EQ(t.played(), "1;2;1;2;1;2;3;4");   // from the start, three times
    EXPECT_EQ(t.played(false), "1;2;3;4");      // repeats off: MuseScore's flatten
}

TEST(RepeatList, EndingsAreTakenOnTheirPass)
{
    Doc d(6);
    d.startRepeat(2);
    d.volta(3, 3, "1");
    d.endRepeat(3);
    d.volta(4, 4, "2", false);
    EXPECT_EQ(d.played(), "1;2;3;2;4;5;6");   // repeat06
    const perform::RepeatList rl = perform::RepeatList::build(d.model.snapshot());
    // where lyrics may run on: the end of the first ending goes back to measure 2, the measure before it on to 4
    EXPECT_EQ(rl.followingMeasures(2), std::vector<size_t>({ 1 }));
    EXPECT_EQ(rl.precedingMeasures(3), std::vector<size_t>({ 1 }));
    EXPECT_TRUE(rl.adjacent(2, 1));
    EXPECT_TRUE(rl.adjacent(1, 3));
    EXPECT_FALSE(rl.adjacent(2, 3));
    EXPECT_TRUE(rl.adjacent(4, 5));
}

TEST(RepeatList, DaCapoAlFineAndDalSegnoAlCoda)
{
    Doc dc(4);
    dc.marker(2, "fine", "");   // a marker without a type is a Fine
    dc.jump(4, "start", "fine", "");
    EXPECT_EQ(dc.played(), "1;2;3;4;1;2");

    Doc ds(6);
    ds.marker(2, "segno", "segno");
    ds.marker(3, "coda", "coda");      // To Coda
    ds.marker(5, "codab", "codab");    // the coda
    ds.jump(4, "segno", "coda", "codab");
    EXPECT_EQ(ds.played(), "1;2;3;4;2;3;5;6");
}

TEST(RepeatList, ASectionBreakStartsTheRepeatsAgain)
{
    Doc d(4);
    d.sectionBreak(2);
    d.endRepeat(3);
    EXPECT_EQ(d.played(), "1;2;3;3;4");
}

TEST(RepeatList, ARepeatSwitchedOffIsPlayedOnceOnItsFinalPass)
{
    Doc d(6);
    d.startRepeat(2);
    d.endRepeat(3);
    d.dontPlayRepeat(3);
    EXPECT_EQ(d.played(), "1;2;3;4;5;6");   // Jubelle_Repeats.PlayThisRepeatOff

    Doc e(6);
    e.startRepeat(2);
    e.volta(3, 3, "1");
    e.endRepeat(3);
    e.volta(4, 4, "2", false);
    e.dontPlayRepeat(3);
    EXPECT_EQ(e.played(), "1;2;4;5;6");   // straight into the last ending
}
