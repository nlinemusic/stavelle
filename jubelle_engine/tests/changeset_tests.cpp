// SPDX-License-Identifier: GPL-3.0-only
#include <gtest/gtest.h>

#include <map>
#include <random>

#include "edit/changeset.h"
#include "edit/history.h"

using namespace je::edit;
using je::Fraction;
using je::Id;

namespace {
//! A plain property store standing in for the model
class Store : public ChangeTarget
{
public:
    Value get(Id o, Field f) const override
    {
        auto it = m.find({ o.value(), f });
        return it == m.end() ? Value() : it->second;
    }

    void set(Id o, Field f, const Value& v) override
    {
        if (std::holds_alternative<std::monostate>(v)) {
            m.erase({ o.value(), f });
        } else {
            m[{ o.value(), f }] = v;
        }
    }

    std::map<std::pair<uint64_t, Field>, Value> m;
};

constexpr Field PITCH = 1;
constexpr Field DURATION = 2;
}

TEST(JE_Transaction, CommitRecordsBeforeAndAfter)
{
    Store s;
    Transaction t(s, "Add note");
    t.set(Id(1), FIELD_EXISTS, true);
    t.set(Id(1), PITCH, int64_t(60));
    t.set(Id(1), DURATION, Fraction(1, 4));
    const ChangeSet c = t.commit();
    EXPECT_EQ(c.actionName, "Add note");
    ASSERT_EQ(c.records.size(), 3u);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(c.records[1].before));
    EXPECT_EQ(std::get<int64_t>(c.records[1].after), 60);
    EXPECT_EQ(c.objects(), std::vector<Id>({ Id(1) }));
}

TEST(JE_Transaction, RepeatedChangesMergeAndNoOpsDisappear)
{
    Store s;
    s.set(Id(7), PITCH, int64_t(60));
    Transaction t(s, "Move");
    t.set(Id(7), PITCH, int64_t(61));
    t.set(Id(7), PITCH, int64_t(62));
    t.set(Id(8), PITCH, int64_t(50));
    t.set(Id(8), PITCH, Value());   // back to absent
    const ChangeSet c = t.commit();
    ASSERT_EQ(c.records.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(c.records[0].before), 60);
    EXPECT_EQ(std::get<int64_t>(c.records[0].after), 62);
}

TEST(JE_Transaction, RollbackAndDestructorRestoreEverything)
{
    Store s;
    s.set(Id(1), PITCH, int64_t(60));
    const auto before = s.m;
    {
        Transaction t(s, "x");
        t.set(Id(1), PITCH, int64_t(70));
        t.set(Id(2), FIELD_EXISTS, true);
        t.rollback();
    }
    EXPECT_EQ(s.m, before);
    {
        Transaction t(s, "y");   // never committed: rolled back when it ends
        t.set(Id(1), PITCH, int64_t(71));
    }
    EXPECT_EQ(s.m, before);
}

TEST(JE_Transaction, HooksAddToTheSameStepOnce)
{
    Store s;
    int calls = 0;
    // a "linked passage": whatever changes Id(1)'s pitch also changes Id(2)'s
    Transaction::Hook link = [&calls](Transaction& t, const ChangeSet& soFar) {
        ++calls;
        for (const auto& r : soFar.records) {
            if (r.object == Id(1) && r.field == PITCH) {
                t.set(Id(2), PITCH, r.after);
            }
        }
    };
    Transaction t(s, "Edit", { link });
    t.set(Id(1), PITCH, int64_t(64));
    const ChangeSet c = t.commit();
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(c.records.size(), 2u);
    EXPECT_EQ(std::get<int64_t>(s.get(Id(2), PITCH)), 64);
}

TEST(JE_History, UndoRedoAreExactInverses)
{
    // property test: random edits, then undo all and redo all
    std::mt19937 rng(12345);
    Store s;
    History h;
    std::vector<decltype(s.m)> states { s.m };
    for (int step = 0; step < 200; ++step) {
        Transaction t(s, "step");
        const int n = 1 + int(rng() % 5);
        for (int k = 0; k < n; ++k) {
            const Id o(1 + rng() % 20);
            const Field f = Field(1 + rng() % 3);
            if (rng() % 6 == 0) {
                t.set(o, f, Value());
            } else {
                t.set(o, f, int64_t(rng() % 100));
            }
        }
        h.push(t.commit());
        if (states.back() != s.m) {
            states.push_back(s.m);
        }
    }
    ASSERT_EQ(h.size() + 1, states.size());
    for (size_t i = states.size() - 1; i > 0; --i) {
        h.undo(s);
        EXPECT_EQ(s.m, states[i - 1]);
    }
    EXPECT_FALSE(h.canUndo());
    for (size_t i = 1; i < states.size(); ++i) {
        h.redo(s);
        EXPECT_EQ(s.m, states[i]);
    }
}

TEST(JE_History, NewEditDropsRedoAndLimitDropsOldest)
{
    Store s;
    History h(3);
    for (int i = 0; i < 5; ++i) {
        Transaction t(s, "e" + std::to_string(i));
        t.set(Id(1), PITCH, int64_t(i + 1));
        h.push(t.commit());
    }
    EXPECT_EQ(h.size(), 3u);
    EXPECT_EQ(h.step(0).actionName, "e2");
    h.undo(s);
    EXPECT_TRUE(h.canRedo());
    Transaction t(s, "new");
    t.set(Id(9), PITCH, int64_t(1));
    h.push(t.commit());
    EXPECT_FALSE(h.canRedo());
    h.push(ChangeSet {});   // empty steps are ignored
    EXPECT_EQ(h.size(), 3u);
}
