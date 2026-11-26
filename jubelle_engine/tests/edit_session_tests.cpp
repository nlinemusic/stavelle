/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine: tests for the undo history's clean state, the autosave journal and crash recovery (S4).
 *
 * The central check: a score is edited at random (notes entered, pitches changed, undo, redo, saving), and the journal
 * is cut at every record boundary and at random bytes between them, as a crash would leave it. Each cut journal is
 * recovered in a fresh session from the score's file, and the recovered score must be exactly the score as it was
 * after the last whole record: the same music, the same ids, and the same undo history.
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <new>
#include <random>
#include <string>
#include <vector>

#include "edit/history.h"
#include "edit/journal.h"
#include "edit/links.h"
#include "edit/operations.h"
#include "edit/scorebuilder.h"
#include "edit/session.h"
#include "io/mscxreader.h"
#include "io/mscxwriter.h"
#include "model/fields.h"
#include "model/model.h"

using namespace je;
using namespace je::edit;
using je::model::Kind;
namespace F = je::model::fields;
namespace fs = std::filesystem;

namespace {
constexpr model::Field TEST_FIELD = F::ENGINE_BASE + 9001;   // a field no engine code uses

struct TempDir {
    fs::path path;
    TempDir()
    {
        static int n = 0;
        path = fs::temp_directory_path() / ("je_session_tests_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed())
                                            + "_" + std::to_string(++n));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string operator/(const std::string& name) const { return (path / name).string(); }
};

//! A two-staff score of `bars` empty 4/4 bars, saved as `file`
void writeScore(const std::string& file, int bars = 8)
{
    model::Model m;
    IdGenerator ids(1);
    ScoreBuilder b(m, ids);
    Transaction tx(m, "New score");
    b.newScore(tx, "Journal");
    b.addMeasures(tx, bars, 4, 4);
    b.addPlayer(tx, "Flute", "flute", "flutes", 1);
    b.addPlayer(tx, "Cello", "violoncello", "strings", 1);
    tx.commit();
    std::string error;
    ASSERT_TRUE(io::saveScore(m.snapshot(), file, error)) << error;
}

std::string readBytes(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeBytes(const std::string& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), std::streamsize(bytes.size()));
}

//! The state a recovery must give back exactly
struct State {
    std::string dump;                 // the music and every field, ids as tree paths
    std::vector<std::pair<uint64_t, int>> ids;   // every object's id and kind
    std::size_t index = 0, size = 0;
    std::string undoName, redoName;
    bool clean = true;

    static State of(const EditSession& s)
    {
        State st;
        st.dump = io::modelDump(s.model().snapshot());
        s.model().snapshot().forEachObject([&](const model::Object& o) { st.ids.emplace_back(o.id().value(), int(o.kind())); });
        std::sort(st.ids.begin(), st.ids.end());
        st.index = s.history().index();
        st.size = s.history().size();
        st.undoName = s.history().undoName();
        st.redoName = s.history().redoName();
        st.clean = !s.isModified();
        return st;
    }
};

void expectSame(const State& want, const State& got, const std::string& where)
{
    EXPECT_EQ(want.dump, got.dump) << where;
    EXPECT_EQ(want.ids, got.ids) << where;
    EXPECT_EQ(want.index, got.index) << where;
    EXPECT_EQ(want.size, got.size) << where;
    EXPECT_EQ(want.undoName, got.undoName) << where;
    EXPECT_EQ(want.redoName, got.redoName) << where;
    EXPECT_EQ(want.clean, got.clean) << where;
}

//! Random edits on an open session: notes entered, pitches changed, undo and redo
struct Editor {
    EditSession& s;
    std::mt19937 rng;
    explicit Editor(EditSession& session, unsigned seed)
        : s(session), rng(seed) {}

    std::vector<Id> voices() const
    {
        std::vector<Id> v;
        s.model().snapshot().forEachObject([&](const model::Object& o) {
            if (o.kind() == Kind::Voice && o.intOr(F::NUMBER, 0) == 0) {
                v.push_back(o.id());
            }
        });
        std::sort(v.begin(), v.end());
        return v;
    }
    std::vector<Id> notes() const
    {
        std::vector<Id> v;
        s.model().snapshot().forEachObject([&](const model::Object& o) {
            if (o.kind() == Kind::Note) {
                v.push_back(o.id());
            }
        });
        std::sort(v.begin(), v.end());
        return v;
    }
    int pick(int n) { return int(rng() % uint32_t(n)); }   // the same sequence on every platform

    enum Done { NOTHING = 0, UNDID, REDID, EDITED };
    //! One action: what it did (NOTHING is false); only edits if `undoRedo` is false
    int step(bool undoRedo = true)
    {
        const int what = undoRedo ? pick(10) : 3 + pick(7);
        if (what < 2 && s.canUndo()) {
            s.undo();
            return UNDID;
        }
        if (what == 2 && s.canRedo()) {
            s.redo();
            return REDID;
        }
        Operations ops(s.model(), s.ids());
        const std::vector<Id> ns = notes();
        if (what < 5 && !ns.empty()) {
            Transaction tx(s.model(), "Change pitch", { linkedStavesHook(s.model(), s.ids()) });
            ops.setPitch(tx, ns[size_t(pick(int(ns.size())))], 48 + pick(36));
            return s.commit(tx).empty() ? NOTHING : EDITED;
        }
        const std::vector<Id> vs = voices();
        const std::vector<Id> ms = ops.measures();
        Transaction tx(s.model(), "Enter note", { linkedStavesHook(s.model(), s.ids()) });
        const Fraction t = ops.measureStart(ms[size_t(pick(int(ms.size())))]) + Fraction(pick(4), 4);
        std::vector<int> pitches { 55 + pick(24) };
        if (pick(3) == 0) {
            pitches.push_back(pitches[0] + 4);
        }
        ops.enterChord(tx, vs[size_t(pick(int(vs.size())))], t, { Fraction(1, 4 << pick(2)), pick(2) }, pitches);
        return s.commit(tx).empty() ? NOTHING : EDITED;
    }
};

//! Recovers a copy of `journal` cut to `bytes` in a fresh session
State recoverCut(const std::string& journalBytes, size_t bytes, const std::string& copy, RecoveryReport& report)
{
    writeBytes(copy, journalBytes.substr(0, bytes));
    EditSession r;
    std::string error;
    EXPECT_TRUE(r.recover(copy, report, error)) << error << " (cut at " << bytes << ")";
    State st = State::of(r);
    r.close();
    return st;
}
}

TEST(EditHistory, CleanStateFollowsUndoRedoAndSaving)
{
    model::Model m;
    IdGenerator ids(1);
    ScoreBuilder b(m, ids);
    History h;
    {
        Transaction tx(m, "New score");
        b.newScore(tx);
        b.addMeasures(tx, 2);
        h.push(tx.commit());
    }
    h.setClean();
    EXPECT_TRUE(h.isClean());
    auto change = [&](const std::string& name, int64_t v) {
        Transaction tx(m, name);
        tx.set(m.score(), TEST_FIELD, model::Value(v));
        h.push(tx.commit());
    };
    change("One", 1);
    change("Two", 2);
    EXPECT_FALSE(h.isClean());
    EXPECT_EQ(h.undoName(), "Two");
    EXPECT_EQ(h.redoName(), "");
    h.undo(m);
    EXPECT_EQ(h.undoName(), "One");
    EXPECT_EQ(h.redoName(), "Two");
    h.undo(m);
    EXPECT_TRUE(h.isClean());   // back where it was saved: no longer modified, as in MuseScore
    h.redo(m);
    EXPECT_FALSE(h.isClean());
    h.setClean();               // saved after one step
    h.undo(m);
    change("Three", 3);         // the saved state was in the redo branch, which is gone now
    EXPECT_FALSE(h.isClean());
    h.undo(m);
    EXPECT_FALSE(h.isClean());  // it can't be reached again
    h.clear();
    EXPECT_TRUE(h.isClean());
    EXPECT_FALSE(h.canUndo());

    History limited(2);
    limited.setClean();
    for (int i = 0; i < 3; ++i) {
        Transaction t(m, "Step " + std::to_string(i));
        t.set(m.score(), TEST_FIELD, model::Value(int64_t(10 + i)));
        limited.push(t.commit());
    }
    EXPECT_EQ(limited.size(), 2u);
    EXPECT_FALSE(limited.isClean());
    limited.undo(m);
    limited.undo(m);
    EXPECT_FALSE(limited.isClean());   // the saved state was older than the oldest step kept
}

TEST(EditJournal, EveryValueKindIsRecoveredExactly)
{
    TempDir dir;
    const std::string score = dir / "values.mscz";
    writeScore(score, 2);
    EditSession s;
    std::string error;
    ASSERT_TRUE(s.open(score, dir.path.string(), error)) << error;
    const Id target = s.model().score();
    const std::vector<model::Value> values {
        model::Value(true), model::Value(int64_t(-1234567890123)), model::Value(0.1 + 0.2), model::Value(-0.0),
        model::Value(model::Text("Ad lib. – dolce “cantabile” ♩=60\n")), model::Value(model::Text("")),
        model::Value(Fraction(-7, 12)), model::Value(Id(987654321)), model::Value(model::IdList({ Id(3), Id(1), Id(2) })),
        model::Value(model::IdList()), model::Value(),
    };
    for (size_t i = 0; i < values.size(); ++i) {
        Transaction tx(s.model(), "Value " + std::to_string(i));
        tx.set(target, TEST_FIELD + model::Field(i), model::Value(int64_t(1)));   // so that the absent value is a change
        s.commit(tx);
        Transaction tx2(s.model(), "Set " + std::to_string(i));
        tx2.set(target, TEST_FIELD + model::Field(i), values[i]);
        s.commit(tx2);
    }
    const State want = State::of(s);
    const std::string journal = readBytes(s.journalPath());
    RecoveryReport report;
    EditSession r;
    writeBytes(dir / "copy.jej", journal);
    ASSERT_TRUE(r.recover(dir / "copy.jej", report, error)) << error;
    EXPECT_FALSE(report.truncated);
    EXPECT_EQ(report.steps, values.size() * 2);
    for (size_t i = 0; i < values.size(); ++i) {
        const model::Value got = r.model().get(target, TEST_FIELD + model::Field(i));
        EXPECT_EQ(got.index(), values[i].index()) << i;
        EXPECT_TRUE(got == values[i]) << i;
    }
    // the ids made by the journal's records (Id(987654321)) are never handed out again
    EXPECT_GT(r.ids().next().value(), 987654321u);
    expectSame(want, State::of(r), "values");
    r.close();
    s.close();
}

TEST(EditJournal, ACrashAtAnyByteRecoversTheLastWholeRecord)
{
    TempDir dir;
    const std::string score = dir / "random.mscz";
    writeScore(score);
    for (unsigned seed : { 1u, 2u, 3u }) {
        EditSession s;
        std::string error;
        ASSERT_TRUE(s.open(score, dir.path.string(), error)) << error;
        ASSERT_TRUE(s.journalError().empty()) << s.journalError();
        Editor ed(s, seed);
        std::vector<std::pair<size_t, State>> marks;   // journal size after each record, and the state then
        marks.emplace_back(fs::file_size(s.journalPath()), State::of(s));
        int saves = 0;
        for (int i = 0; i < 60; ++i) {
            if (i == 30 && seed != 1) {
                // saving in the middle: the journal starts again from the saved file, with the history
                ASSERT_TRUE(s.save(error)) << error;
                ++saves;
                marks.clear();
                marks.emplace_back(fs::file_size(s.journalPath()), State::of(s));
                EXPECT_TRUE(marks.back().second.clean);
                continue;
            }
            if (ed.step()) {
                marks.emplace_back(fs::file_size(s.journalPath()), State::of(s));
            }
        }
        const std::string journal = readBytes(s.journalPath());
        ASSERT_EQ(journal.size(), marks.back().first);
        std::mt19937 rng(seed * 77);
        size_t checked = 0;
        for (size_t m = 0; m < marks.size(); ++m) {
            std::vector<size_t> cuts { marks[m].first };
            const size_t end = m + 1 < marks.size() ? marks[m + 1].first : marks[m].first;
            for (int k = 0; k < 2 && end > marks[m].first + 1; ++k) {
                cuts.push_back(marks[m].first + 1 + rng() % (end - marks[m].first - 1));   // inside the next record
            }
            for (size_t cut : cuts) {
                RecoveryReport report;
                const State got = recoverCut(journal, cut, dir / "cut.jej", report);
                EXPECT_EQ(report.truncated, cut != marks[m].first) << cut;
                expectSame(marks[m].second, got, "seed " + std::to_string(seed) + ", cut at " + std::to_string(cut));
                ++checked;
            }
        }
        EXPECT_GT(checked, 60u);
        // a crash: the session ends without closing, the journal stays
        const std::string path = s.journalPath();
        s.~EditSession();
        new (&s) EditSession();
        EXPECT_TRUE(fs::exists(path));
        EXPECT_TRUE(discardJournal(path));
        EXPECT_FALSE(fs::exists(path));
        if (saves) {
            writeScore(score);   // the next seed starts from the same file
        }
    }
}

TEST(EditJournal, ARecoveredScoreUndoesPastTheLastSave)
{
    TempDir dir;
    const std::string score = dir / "saved.mscz";
    writeScore(score);
    std::string error, journal;
    State original, saved, crashed;
    {
        EditSession s;
        ASSERT_TRUE(s.open(score, dir.path.string(), error)) << error;
        original = State::of(s);
        Editor ed(s, 9);
        for (int i = 0; i < 12; ++i) {
            ed.step();
        }
        while (s.canRedo()) {
            s.redo();
        }
        ASSERT_TRUE(s.save(error)) << error;
        EXPECT_FALSE(s.isModified());
        saved = State::of(s);
        for (int i = 0; i < 8; ++i) {
            ed.step(false);   // edits only: undoing below the save and editing would make the saved state unreachable
        }
        crashed = State::of(s);
        journal = s.journalPath();
    }   // the program ends without closing the score
    const auto found = findJournals(dir.path.string());
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].first, journal);
    EXPECT_TRUE(found[0].second.ok);
    EXPECT_EQ(fs::path(found[0].second.base.file), fs::absolute(score).lexically_normal());

    EditSession r;
    RecoveryReport report;
    ASSERT_TRUE(r.recover(journal, report, error)) << error;
    expectSame(crashed, State::of(r), "recovered");
    // undoing goes back past the save to the score as it was opened; the saved point is clean again on the way
    bool passedSave = false;
    size_t undone = 0;
    while (r.canUndo()) {
        r.undo();
        ++undone;
        if (r.history().index() == saved.index) {
            passedSave = !r.isModified() && io::modelDump(r.model().snapshot()) == saved.dump;
        }
    }
    EXPECT_TRUE(passedSave);
    EXPECT_EQ(undone, crashed.index);
    EXPECT_EQ(io::modelDump(r.model().snapshot()), original.dump);
    for (size_t i = 0; i < undone; ++i) {
        r.redo();
    }
    expectSame(crashed, State::of(r), "undone and redone");

    // a second crash after recovering loses nothing either
    {
        Editor ed(r, 10);
        for (int i = 0; i < 5; ++i) {
            ed.step();
        }
    }
    const State again = State::of(r);
    r.~EditSession();
    new (&r) EditSession();
    ASSERT_TRUE(r.recover(journal, report, error)) << error;
    expectSame(again, State::of(r), "recovered twice");

    // saved after recovering: clean, and the journal is for the new file
    ASSERT_TRUE(r.save(error)) << error;
    EXPECT_FALSE(r.isModified());
    const RecoveryReport now = inspectJournal(r.journalPath());
    EXPECT_TRUE(now.ok);
    EXPECT_EQ(now.steps, 0u);
    EXPECT_EQ(now.historySteps, r.history().size());
    r.close();
    EXPECT_FALSE(fs::exists(journal));   // closing removes it
    EXPECT_TRUE(findJournals(dir.path.string()).empty());
}

TEST(EditJournal, WorkIsRecoveredEvenIfTheFileChangedSince)
{
    TempDir dir;
    const std::string score = dir / "changed.mscz";
    writeScore(score);
    std::string error, journal;
    State crashed;
    {
        EditSession s;
        ASSERT_TRUE(s.open(score, dir.path.string(), error)) << error;
        Editor ed(s, 4);
        for (int i = 0; i < 6; ++i) {
            ed.step();
        }
        crashed = State::of(s);
        journal = s.journalPath();
    }
    EXPECT_FALSE(inspectJournal(journal).baseChanged);
    writeScore(score, 9);   // the file was changed elsewhere (another program, a copy from another computer)
    EXPECT_TRUE(inspectJournal(journal).baseChanged);

    // opening the score again doesn't overwrite the journal the crash left behind
    EditSession o;
    ASSERT_TRUE(o.open(score, dir.path.string(), error)) << error;
    EXPECT_NE(o.journalPath(), journal);
    EXPECT_EQ(findJournals(dir.path.string()).size(), 2u);
    o.close();
    EXPECT_EQ(findJournals(dir.path.string()).size(), 1u);

    // the work is recovered from the journal alone; the user is told the file has changed since
    EditSession r;
    RecoveryReport report;
    ASSERT_TRUE(r.recover(journal, report, error)) << error;
    EXPECT_TRUE(report.baseChanged);
    expectSame(crashed, State::of(r), "file changed");
    fs::remove(score);
    r.close();
    EditSession g;
    {
        EditSession s;
        writeScore(score);
        ASSERT_TRUE(s.open(score, dir.path.string(), error)) << error;
        Editor ed(s, 8);
        ed.step();
        ed.step();
        crashed = State::of(s);
        journal = s.journalPath();
    }
    fs::remove(score);   // and even if the file is gone
    ASSERT_TRUE(g.recover(journal, report, error)) << error;
    EXPECT_TRUE(report.baseChanged);
    expectSame(crashed, State::of(g), "file gone");
    ASSERT_TRUE(g.save(error)) << error;   // saved where it was
    EXPECT_TRUE(fs::exists(score));
    g.close();
}

TEST(EditJournal, ADamagedRecordEndsTheRecovery)
{
    TempDir dir;
    const std::string score = dir / "damaged.mscz";
    writeScore(score);
    std::string error;
    EditSession s;
    ASSERT_TRUE(s.open(score, dir.path.string(), error)) << error;
    Editor ed(s, 5);
    std::vector<std::pair<size_t, State>> marks;
    marks.emplace_back(fs::file_size(s.journalPath()), State::of(s));
    while (marks.size() < 6) {
        if (ed.step()) {
            marks.emplace_back(fs::file_size(s.journalPath()), State::of(s));
        }
    }
    std::string journal = readBytes(s.journalPath());
    // a byte of the fourth record changed (a disk error): its middle (an undo or redo record is only 9 bytes)
    journal[(marks[3].first + marks[4].first) / 2] ^= 0x5A;
    writeBytes(dir / "damaged.jej", journal);
    EditSession r;
    RecoveryReport report;
    ASSERT_TRUE(r.recover(dir / "damaged.jej", report, error)) << error;
    EXPECT_TRUE(report.truncated);
    expectSame(marks[3].second, State::of(r), "damaged");
    r.close();

    writeBytes(dir / "notjournal.jej", "MuseScore");
    EXPECT_FALSE(inspectJournal(dir / "notjournal.jej").ok);
    EXPECT_FALSE(r.recover(dir / "notjournal.jej", report, error));
    s.close();
}

TEST(EditJournal, SaveAsMovesTheJournalAndReopeningGivesTheSameIds)
{
    TempDir dir;
    const std::string score = dir / "first.mscz";
    writeScore(score);
    std::string error;
    EditSession a, b;
    ASSERT_TRUE(a.open(score, "", error)) << error;
    ASSERT_TRUE(b.open(score, "", error)) << error;
    EXPECT_TRUE(a.journalPath().empty());
    expectSame(State::of(a), State::of(b), "opened twice");   // replay depends on this
    a.close();
    b.close();

    EditSession s;
    ASSERT_TRUE(s.open(score, dir.path.string(), error)) << error;
    Editor ed(s, 6);
    for (int i = 0; i < 5; ++i) {
        ed.step();
    }
    const std::string first = s.journalPath();
    const std::string other = dir / "second.mscx";
    ASSERT_TRUE(s.saveAs(other, error)) << error;
    EXPECT_EQ(s.path(), other);
    EXPECT_FALSE(fs::exists(first));
    EXPECT_EQ(s.journalPath(), journalPathFor(other, dir.path.string()));
    ed.step();
    ed.step();
    const State want = State::of(s);
    const std::string journal = s.journalPath();
    s.~EditSession();
    new (&s) EditSession();
    RecoveryReport report;
    ASSERT_TRUE(s.recover(journal, report, error)) << error;
    EXPECT_EQ(fs::path(s.path()), fs::absolute(other).lexically_normal());
    // the steps before Save As are kept for undo, though the file they were made on was another
    EXPECT_GT(report.historySteps, 0u);
    expectSame(want, State::of(s), "saved as");
    s.close();
}

TEST(EditJournal, BackgroundSyncsDontLoseOrReorderRecords)
{
    TempDir dir;
    const std::string path = dir / "sync.jej";
    model::Model m;
    IdGenerator ids(1);
    ScoreBuilder b(m, ids);
    {
        Transaction tx(m, "New score");
        b.newScore(tx);
        tx.commit();
    }
    Journal j;
    std::string error;
    ASSERT_TRUE(j.start(path, JournalBase(), m.snapshot(), History(), error)) << error;
    for (int i = 0; i < 300; ++i) {
        Transaction tx(m, "Step " + std::to_string(i));
        tx.set(m.score(), TEST_FIELD, model::Value(int64_t(i)));
        ASSERT_TRUE(j.recordStep(tx.commit()));
        if (i % 3 == 0) {
            j.syncLater();
        }
        if (i % 50 == 0) {
            ASSERT_TRUE(j.recordUndo());
            ASSERT_TRUE(j.recordRedo());
        }
    }
    EXPECT_FALSE(j.syncFailed());
    j.close(false);   // the program ends: the journal stays, all of it on the disk
    const RecoveryReport r = inspectJournal(path);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_FALSE(r.truncated);
    EXPECT_EQ(r.steps, 300u);
    EXPECT_EQ(r.undos, 6u);
    model::Model back;
    History h;
    IdGenerator ids2(1);
    ASSERT_TRUE(replayJournal(path, back, h, ids2).ok);
    EXPECT_EQ(std::get<int64_t>(back.get(back.score(), TEST_FIELD)), 299);
    EXPECT_EQ(h.undoName(), "Step 299");
}

//! S4's property test (14_full_engine_roadmap.md): 10,000 random steps (edits, undo, redo, a save now and then), the
//! score checked after every one against the state it must be in (undo gives back exactly the state before the step,
//! redo exactly the state after), then recovered from its journal and undone to the score as it was opened
TEST(EditJournal, TenThousandRandomStepsUndoRedoAndRecoverExactly)
{
    TempDir dir;
    const std::string score = dir / "long.mscz";
    writeScore(score, 4);
    std::string error;
    State crashed;
    std::string journal;
    std::string opened;
    {
        EditSession s;
        ASSERT_TRUE(s.open(score, dir.path.string(), error)) << error;
        opened = io::modelDump(s.model().snapshot());
        Editor ed(s, 2026);
        std::vector<size_t> want { std::hash<std::string>()(opened) };   // the state after each step of the branch
        size_t undos = 0, redos = 0, edits = 0;
        for (int i = 0; i < 10000; ++i) {
            const size_t before = s.history().index();
            if (i % 2500 == 1249) {
                ASSERT_TRUE(s.save(error)) << error;
                EXPECT_FALSE(s.isModified());
                continue;
            }
            const int done = ed.step();
            if (!done) {
                continue;
            }
            const size_t now = s.history().index();
            const size_t h = std::hash<std::string>()(io::modelDump(s.model().snapshot()));
            if (done == Editor::UNDID) {
                ++undos;
                ASSERT_EQ(now, before - 1);
                ASSERT_EQ(h, want[now]) << "undo at step " << i;
            } else if (done == Editor::REDID) {
                ASSERT_EQ(now, before + 1);
                ++redos;
                ASSERT_EQ(h, want[now]) << "redo at step " << i;
            } else {
                ++edits;
                ASSERT_EQ(now, before + 1);
                want.resize(before + 1);
                want.push_back(h);
            }
        }
        EXPECT_GT(undos, 1000u);
        EXPECT_GT(redos, 200u);
        EXPECT_GT(edits, 5000u);
        crashed = State::of(s);
        journal = s.journalPath();
    }
    EditSession r;
    RecoveryReport report;
    ASSERT_TRUE(r.recover(journal, report, error)) << error;
    expectSame(crashed, State::of(r), "after 10,000 steps");
    while (r.canUndo()) {
        r.undo();
    }
    EXPECT_EQ(io::modelDump(r.model().snapshot()), opened);
    r.close();
}
