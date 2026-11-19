// SPDX-License-Identifier: GPL-3.0-only
#include <gtest/gtest.h>

#include <atomic>
#include <map>
#include <mutex>
#include <stdexcept>
#include <random>
#include <thread>
#include <vector>

#include "edit/history.h"
#include "edit/scorebuilder.h"
#include "edit/structure.h"
#include "model/invariants.h"
#include "model/model.h"

using namespace je;
using namespace je::edit;
using je::model::Kind;
namespace F = je::model::fields;

namespace {
std::string problems(const model::Snapshot& s)
{
    std::string out;
    for (const std::string& p : model::checkInvariants(s)) {
        out += p + "\n";
    }
    return out;
}

struct Fixture {
    model::Model model;
    IdGenerator ids { 1 };
    ScoreBuilder build { model, ids };
    History history;
    std::vector<ScoreBuilder::PlayerIds> players;

    void make(int playerCount, int measures)
    {
        Transaction tx(model, "New score");
        build.newScore(tx, "Test");
        build.addMeasures(tx, measures);
        for (int p = 0; p < playerCount; ++p) {
            players.push_back(build.addPlayer(tx, "Trumpet " + std::to_string(p + 1), "trumpet", "brass", p + 1, -2));
        }
        history.push(tx.commit());
    }
};
}

TEST(Model, SetRulesAndFieldReading)
{
    model::Model m;
    EXPECT_THROW(m.set(Id(5), F::PITCH, int64_t(60)), std::logic_error);   // not created yet
    m.set(Id(5), F::EXISTS, true);
    m.set(Id(5), F::PITCH, int64_t(60));
    EXPECT_EQ(std::get<int64_t>(m.get(Id(5), F::PITCH)), 60);
    EXPECT_TRUE(std::get<bool>(m.get(Id(5), F::EXISTS)));
    const uint64_t v = m.version();
    m.set(Id(5), F::PITCH, int64_t(60));   // unchanged: no new version
    EXPECT_EQ(m.version(), v);
    m.set(Id(5), F::EXISTS, false);
    EXPECT_TRUE(model::isAbsent(m.get(Id(5), F::EXISTS)));
    EXPECT_TRUE(model::isAbsent(m.get(Id(5), F::PITCH)));
}

TEST(Model, BuiltScoreIsConsistent)
{
    Fixture f;
    f.make(4, 8);
    Transaction tx(f.model, "Notes");
    for (int i = 0; i < 8 * 4; ++i) {
        f.build.addChord(tx, f.players[0].voice, Fraction(i, 4), Fraction(1, 4), { 60 + i % 12 });
    }
    f.history.push(tx.commit());
    const model::Snapshot s = f.model.snapshot();
    EXPECT_EQ(problems(s), "");
    const model::Object* voice = s.find(f.players[0].voice);
    ASSERT_NE(voice, nullptr);
    EXPECT_EQ(voice->children().size(), 32u);
}

TEST(Model, ChildrenAreKeptInTimeOrder)
{
    Fixture f;
    f.make(1, 2);
    Transaction tx(f.model, "Notes");
    const Id v = f.players[0].voice;
    const Id c3 = f.build.addChord(tx, v, Fraction(3, 4), Fraction(1, 4), { 64 });
    const Id c1 = f.build.addChord(tx, v, Fraction(1, 4), Fraction(1, 4), { 62 });
    const Id c0 = f.build.addRest(tx, v, Fraction(0, 1), Fraction(1, 4));
    const Id c2 = f.build.addChord(tx, v, Fraction(2, 4), Fraction(1, 4), { 63 });
    f.history.push(tx.commit());
    const auto& kids = f.model.find(v)->children().ids();
    EXPECT_EQ(kids, (std::vector<Id> { c0, c1, c2, c3 }));
    EXPECT_EQ(problems(f.model.snapshot()), "");
}

TEST(Model, InvariantsCatchProblems)
{
    Fixture f;
    f.make(1, 2);
    {
        Transaction tx(f.model, "Overlap");
        f.build.addChord(tx, f.players[0].voice, Fraction(0, 1), Fraction(1, 2), { 60 });
        f.build.addChord(tx, f.players[0].voice, Fraction(1, 4), Fraction(1, 4), { 62 });
        tx.commit();
    }
    EXPECT_NE(problems(f.model.snapshot()).find("overlaps"), std::string::npos);
}

TEST(Model, RemoveAndUndoRestoreExactly)
{
    Fixture f;
    f.make(2, 4);
    Transaction tx(f.model, "Notes");
    const Id chord = f.build.addChord(tx, f.players[1].voice, Fraction(1, 4), Fraction(1, 4), { 60, 64, 67 });
    f.history.push(tx.commit());
    const std::string before = f.model.snapshot().dump();

    Transaction rm(f.model, "Delete");
    removeTree(rm, chord);
    f.history.push(rm.commit());
    EXPECT_EQ(f.model.find(chord), nullptr);
    EXPECT_EQ(problems(f.model.snapshot()), "");

    f.history.undo(f.model);
    EXPECT_EQ(f.model.snapshot().dump(), before);
    f.history.redo(f.model);
    EXPECT_EQ(f.model.find(chord), nullptr);
    f.history.undo(f.model);
    EXPECT_EQ(f.model.snapshot().dump(), before);
}

TEST(Model, SnapshotIsUnaffectedByLaterEdits)
{
    Fixture f;
    f.make(1, 1);
    Transaction tx(f.model, "Note");
    const Id chord = f.build.addChord(tx, f.players[0].voice, Fraction(0, 1), Fraction(1, 4), { 60 });
    f.history.push(tx.commit());
    const model::Snapshot old = f.model.snapshot();
    const std::string oldDump = old.dump();
    const Id note = f.build.notesOf(chord).front();

    Transaction edit(f.model, "Pitch");
    edit.set(note, F::PITCH, int64_t(61));
    f.history.push(edit.commit());
    EXPECT_EQ(old.dump(), oldDump);
    EXPECT_EQ(std::get<int64_t>(old.find(note)->get(F::PITCH)), 60);
    EXPECT_EQ(std::get<int64_t>(f.model.snapshot().find(note)->get(F::PITCH)), 61);
}

// Property test: random structural and property edits, random undo and redo. After every step the score is
// consistent, and returning to a history position gives exactly the content it had there.
TEST(Model, RandomEditsUndoRedoRestoreExactContent)
{
    Fixture f;
    f.make(3, 6);   // 3 players, 6 bars of 4/4 = 24 quarter slots each
    std::mt19937 rng(7);
    std::map<size_t, std::string> dumpAt;
    dumpAt[f.history.index()] = f.model.snapshot().dump();

    auto eventsOf = [&f](Id voice) { return f.model.find(voice)->children().ids(); };
    auto slotFree = [&f, &eventsOf](Id voice, int slot) {
        for (Id e : eventsOf(voice)) {
            if (f.model.find(e)->fractionOr(F::TIME, Fraction()) == Fraction(slot, 4)) {
                return false;
            }
        }
        return true;
    };

    for (int step = 0; step < 1500; ++step) {
        const int op = int(rng() % 10);
        if (op < 2 && f.history.canUndo()) {
            f.history.undo(f.model);
        } else if (op < 3 && f.history.canRedo()) {
            f.history.redo(f.model);
        } else {
            const Id voice = f.players[rng() % f.players.size()].voice;
            Transaction tx(f.model, "Edit");
            const auto events = eventsOf(voice);
            const int kind = int(rng() % 5);
            if (kind == 0 || events.empty()) {
                const int slot = int(rng() % 24);
                if (slotFree(voice, slot)) {
                    if (rng() % 4 == 0) {
                        f.build.addRest(tx, voice, Fraction(slot, 4), Fraction(1, 4));
                    } else {
                        std::vector<int> pitches { 55 + int(rng() % 24) };
                        if (rng() % 3 == 0) {
                            pitches.push_back(pitches[0] + 4);
                        }
                        f.build.addChord(tx, voice, Fraction(slot, 4), Fraction(1, 4), pitches);
                    }
                }
            } else if (kind == 1) {
                removeTree(tx, events[rng() % events.size()]);
            } else if (kind == 2) {
                const Id e = events[rng() % events.size()];
                for (Id n : f.build.notesOf(e)) {
                    tx.set(n, F::PITCH, int64_t(50 + rng() % 30));
                }
            } else if (kind == 3) {
                // add an articulation to a chord
                const Id e = events[rng() % events.size()];
                if (f.model.find(e)->kind() == Kind::Chord) {
                    create(tx, f.ids, e, { Kind::Attachment, { { F::SUBKIND, std::string("staccato") } } });
                }
            } else {
                // two edits in one step: remove one event and add another where it was
                const Id e = events[rng() % events.size()];
                const Fraction t = f.model.find(e)->fractionOr(F::TIME, Fraction());
                removeTree(tx, e);
                f.build.addChord(tx, voice, t, Fraction(1, 4), { 72 });
            }
            ChangeSet cs = tx.commit();
            if (!cs.empty()) {
                f.history.push(std::move(cs));
                // positions after this one are gone (the redo branch was dropped)
                for (auto it = dumpAt.upper_bound(f.history.index() - 1); it != dumpAt.end();) {
                    it = dumpAt.erase(it);
                }
            }
        }
        const model::Snapshot s = f.model.snapshot();
        ASSERT_EQ(problems(s), "") << "after step " << step;
        auto it = dumpAt.find(f.history.index());
        if (it == dumpAt.end()) {
            dumpAt[f.history.index()] = s.dump();
        } else {
            ASSERT_EQ(s.dump(), it->second) << "content differs at history position " << f.history.index() << ", step " << step;
        }
    }
}

// Snapshots published by the editing thread are read on other threads while editing continues (ThreadSanitizer)
TEST(Model, SnapshotsCanBeReadOnOtherThreadsDuringEdits)
{
    Fixture f;
    f.make(2, 16);
    std::mutex mutex;
    model::Snapshot published = f.model.snapshot();
    int64_t publishedSum = 0;
    std::atomic<bool> stop { false };
    std::atomic<int> checks { 0 };
    std::atomic<int> failures { 0 };

    auto pitchSum = [](const model::Snapshot& s) {
        int64_t sum = 0;
        s.forEachObject([&sum](const model::Object& o) {
            if (o.kind() == Kind::Note) {
                sum += o.intOr(F::PITCH, 0);
            }
        });
        return sum;
    };

    std::vector<std::thread> readers;
    for (int r = 0; r < 3; ++r) {
        readers.emplace_back([&]() {
            while (!stop.load()) {
                model::Snapshot s;
                int64_t expected = 0;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    s = published;
                    expected = publishedSum;
                }
                if (pitchSum(s) != expected) {
                    failures.fetch_add(1);
                }
                checks.fetch_add(1);
            }
        });
    }

    std::mt19937 rng(3);
    for (int i = 0; i < 64; ++i) {
        Transaction tx(f.model, "Note");
        f.build.addChord(tx, f.players[size_t(i % 2)].voice, Fraction(i / 2, 4), Fraction(1, 4), { 50 + int(rng() % 40) });
        f.history.push(tx.commit());
        const model::Snapshot s = f.model.snapshot();
        const int64_t sum = pitchSum(s);
        std::lock_guard<std::mutex> lock(mutex);
        published = s;
        publishedSum = sum;
    }
    while (checks.load() < 50) {
        std::this_thread::yield();
    }
    stop = true;
    for (auto& t : readers) {
        t.join();
    }
    EXPECT_EQ(failures.load(), 0);
}
