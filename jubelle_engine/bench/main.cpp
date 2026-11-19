// SPDX-License-Identifier: GPL-3.0-only
// Jubelle engine benchmarks (Phase 3 skeleton). Writes JSON lines: {"name":..,"ms":..}.
// The score benchmarks (read, layout, edit, undo, save against MuseScore's engine)
// are added with the converter (Phase 3.3), using the same scores as
// "Jubelle - Engine Baselines (15 Nov 2026)".
#include <chrono>
#include <cstdio>
#include <vector>

#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include <string>

#include "edit/changeset.h"
#include "edit/history.h"
#include "edit/scorebuilder.h"
#include "edit/structure.h"
#include "exec/threadpool.h"
#include "model/cowvector.h"
#include "model/bulkbuilder.h"
#include "model/invariants.h"
#include "model/model.h"

namespace {
template<typename F>
double timeMs(F&& f, int reps = 5)
{
    double best = 1e30;
    for (int r = 0; r < reps; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        f();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        best = ms < best ? ms : best;
    }
    return best;
}

//! Live heap in MB (glibc only; -1 elsewhere)
double heapMb()
{
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    return double(mallinfo2().uordblks) / (1024.0 * 1024.0);
#else
    return -1;
#endif
}

void report(const char* name, double ms)
{
    std::printf("{\"name\":\"%s\",\"ms\":%.3f}\n", name, ms);
}

//! The model at the size of the baseline orchestra score: 39 players × 285 bars of 4/4, a quarter-note chord
//! on every beat (44,460 chords and notes). Measures building, snapshots, one edit, undo, structural edits.
void modelBenchmarks()
{
    using namespace je;
    using namespace je::edit;
    namespace F = je::model::fields;
    const double memBefore = heapMb();

    model::Model m;
    IdGenerator ids(1);
    ScoreBuilder b(m, ids);
    History history;
    std::vector<ScoreBuilder::PlayerIds> players;
    std::vector<Id> lastChords;

    const auto t0 = std::chrono::steady_clock::now();
    {
        Transaction tx(m, "New score");
        b.newScore(tx, "Large orchestra");
        b.addMeasures(tx, 285);
        for (int p = 0; p < 39; ++p) {
            players.push_back(b.addPlayer(tx, "Player " + std::to_string(p + 1), "violin", "strings"));
        }
        history.push(tx.commit());
    }
    for (const auto& p : players) {
        Transaction tx(m, "Notes");
        Id chord;
        for (int beat = 0; beat < 285 * 4; ++beat) {
            chord = b.addChord(tx, p.voice, Fraction(beat, 4), Fraction(1, 4), { 55 + beat % 24 });
        }
        lastChords.push_back(chord);
        history.push(tx.commit());
    }
    const double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("{\"name\":\"model: build 39 x 285 orchestra (%zu objects)\",\"ms\":%.1f}\n", m.snapshot().objectCount(), buildMs);
    const double memAfter = heapMb();
    {
        // the score's model alone: the building steps are dropped from the history (loading a file isn't undoable)
        History empty;
        std::swap(history, empty);
    }
    const double memModel = heapMb();
    if (memBefore >= 0) {
        std::printf("{\"name\":\"model: heap of the orchestra model (with the undo history of building it)\",\"mb\":%.1f}\n", memAfter - memBefore);
        std::printf("{\"name\":\"model: heap of the orchestra model alone\",\"mb\":%.1f}\n", memModel - memBefore);
    }

    report("model: snapshot (whole score)", timeMs([&] { model::Snapshot s = m.snapshot(); (void)s; }, 20));

    const Id note = b.notesOf(lastChords[20]).front();
    int64_t pitch = 60;
    report("model: edit one note (transaction, commit, history)", timeMs([&] {
        Transaction tx(m, "Pitch");
        tx.set(note, F::PITCH, ++pitch);
        history.push(tx.commit());
    }, 50));
    report("model: undo one note edit", timeMs([&] { history.undo(m); history.redo(m); }, 50) / 2.0);

    const Id voice = players[20].voice;
    report("model: delete a chord in a 1,140-event voice, then undo", timeMs([&] {
        const Id victim = m.find(voice)->children()[570];
        Transaction tx(m, "Delete");
        removeTree(tx, victim);
        history.push(tx.commit());
        history.undo(m);
    }, 50));
    report("model: insert a chord into a 1,140-event voice", timeMs([&] {
        Transaction tx(m, "Insert");
        const Id victim = m.find(voice)->children()[570];
        const Fraction t = m.find(victim)->fractionOr(F::TIME, Fraction());
        removeTree(tx, victim);
        b.addChord(tx, voice, t, Fraction(1, 4), { 72 });
        history.push(tx.commit());
    }, 50));
    report("model: build the same orchestra at once (BulkBuilder, as a file reader will)", timeMs([&] {
        model::Model fresh;
        IdGenerator bulkIds(1);
        model::BulkBuilder bb(bulkIds);
        const Id score = bb.add(model::Kind::Score, Id(), { { F::NAME, std::string("Large orchestra") } });
        const Id flow = bb.add(model::Kind::Flow, score, {});
        for (int mm = 0; mm < 285; ++mm) {
            bb.add(model::Kind::Measure, flow, { { F::TIME, Fraction(mm, 1) }, { F::DURATION, Fraction(1, 1) } },
                   model::BulkBuilder::Order::ByTime);
        }
        for (int p = 0; p < 39; ++p) {
            const Id player = bb.add(model::Kind::Player, score, { { F::NAME, "Player " + std::to_string(p + 1) } });
            const Id staff = bb.add(model::Kind::Staff, player, {});
            const Id v = bb.add(model::Kind::Voice, staff, { { F::FLOW, flow } });
            for (int beat = 0; beat < 285 * 4; ++beat) {
                const Id c = bb.add(model::Kind::Chord, v, { { F::TIME, Fraction(beat, 4) }, { F::DURATION, Fraction(1, 4) } },
                                    model::BulkBuilder::Order::ByTime);
                bb.add(model::Kind::Note, c, { { F::PITCH, int64_t(55 + beat % 24) } });
            }
        }
        bb.finish(fresh);
    }, 3));
    report("model: check every invariant (whole score)", timeMs([&] {
        auto problems = model::checkInvariants(m.snapshot());
        if (!problems.empty()) {
            std::printf("invariant problem: %s\n", problems.front().c_str());
        }
    }, 3));
}
}

int main()
{
    je::exec::ThreadPool pool;
    std::vector<double> out(100000);
    std::printf("{\"name\":\"parallelFor 100k items (%zu threads)\",\"ms\":%.3f}\n", pool.size(),
                timeMs([&] { pool.parallelFor(out.size(), [&](size_t i) { out[i] = double(i) * 0.5; }); }));

    je::CowVector<int> v;
    for (int i = 0; i < 1000000; ++i) {
        v.push_back(i);
    }
    std::printf("{\"name\":\"snapshot of 1M-element CowVector\",\"ms\":%.3f}\n", timeMs([&] { auto s = v; (void)s; }));
    modelBenchmarks();
    return 0;
}
