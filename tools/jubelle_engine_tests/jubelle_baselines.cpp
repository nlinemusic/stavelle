/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle: engine baselines (core engine improvements, §11; approved 15 Nov
// 2026). Measurements only: nothing is changed and nothing is asserted about
// speed. Each run prints a table and writes engine_baselines.json next to the
// tool, so later optimisation can be compared before and after.
//
// Measured per score, on the real engine (Linux, no Qt, realistic font metrics):
//   read      load the file (no layout)
//   layout    a full layout
//   edit      one note entered (a command with its incremental layout), average
//   edit+hook the same with Jubelle's linked-passages command hook installed
//   undo      undoing one of those edits, average
//   save      writing the score to memory
//   playback  building MuseScore's playback model (all events)
//   memory    resident memory growth while the score is open

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "engraving/compat/mscxcompat.h"
#include "engraving/compat/scoreaccess.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/editing/undo.h"
#include "engraving/infrastructure/localfileinfoprovider.h"
#include "engraving/playback/playbackmodel.h"
#include "engraving/rw/rwregister.h"
#include "engraving/style/style.h"
#include "global/io/buffer.h"
#include "mpe/tests/mocks/articulationprofilesrepositorymock.h"

#include "fontproviderstub.h"
#include "notation/internal/jubelle/linkedpassages.h"

using namespace mu::engraving;
using namespace ::testing;

namespace {
const std::string ROOT = std::string(MU_ROOT_DIR) + "/";

double msSince(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

double residentMb()
{
    std::ifstream f("/proc/self/statm");
    long pages = 0, resident = 0;
    f >> pages >> resident;
    return resident * 4096.0 / (1024.0 * 1024.0);
}

struct Row {
    std::string name;
    size_t staves = 0, measures = 0;
    double readMs = 0, layoutMs = 0, editMs = 0, editHookMs = 0, undoMs = 0, saveMs = 0, playbackMs = 0, memMb = 0;
};

MasterScore* load(const std::string& path)
{
    MasterScore* s = compat::ScoreAccess::createMasterScoreWithBaseStyle(nullptr);
    s->setFileInfoProvider(std::make_shared<LocalFileInfoProvider>(muse::io::path_t(path)));
    if (!compat::loadMsczOrMscx(s, muse::io::path_t(path), false)) {
        delete s;
        return nullptr;
    }
    return s;
}

//! Enters `count` notes on the first staff from the middle of the score, one command each
double editAverage(MasterScore* s, int count)
{
    Measure* m = s->firstMeasure();
    for (size_t i = 0; m && i < s->nmeasures() / 2; ++i) {
        m = m->nextMeasure();
    }
    double total = 0;
    int done = 0;
    for (int i = 0; i < count && m; ++i, m = m->nextMeasure()) {
        Segment* seg = m->first(SegmentType::ChordRest);
        if (!seg) {
            continue;
        }
        const auto t0 = std::chrono::steady_clock::now();
        s->startCmd(muse::TranslatableString::untranslatable("baseline edit"));
        s->setNoteRest(seg, 0, NoteVal(72), Fraction(1, 4));
        s->endCmd();
        total += msSince(t0);
        ++done;
    }
    return done ? total / done : 0;
}

double undoAverage(MasterScore* s, int count)
{
    double total = 0;
    int done = 0;
    for (int i = 0; i < count && s->undoStack()->canUndo(); ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        s->undoRedo(true, nullptr);
        total += msSince(t0);
        ++done;
    }
    return done ? total / done : 0;
}
}

TEST(Jubelle_Baselines, EngineTimings)
{
    // About two minutes, so only on request: JUBELLE_BASELINES=1
    const char* want = std::getenv("JUBELLE_BASELINES");
    if (!want || std::string(want) != "1") {
        GTEST_SKIP() << "set JUBELLE_BASELINES=1 to measure";
    }
    if (!muse::draw::FontProviderStub::realisticMetrics()) {
        GTEST_SKIP() << "needs realistic metrics (second pass)";
    }

    // A large orchestral score like the Windows app check's (39 staves, 285 bars)
    const std::string large = "/tmp/jubelle_baseline_large.mscx";
    std::system(("python3 " + ROOT + "tools/jubelle_checks/make_large_score.py " + ROOT + "demos/Dawn.mscx " + large
                 + " --instruments 3 --measures 15 > /dev/null 2>&1").c_str());

    const std::vector<std::pair<std::string, std::string> > scores {
        { "Unclaimed Gift (piano/voice)", ROOT + "demos/Unclaimed_Gift.mscx" },
        { "Dynamic Strings", ROOT + "demos/Dynamic_Strings.mscx" },
        { "Fugue 1", ROOT + "demos/Fugue_1.mscx" },
        { "Dawn (orchestra)", ROOT + "demos/Dawn.mscx" },
        { "Big Score (MuseScore test)", ROOT + "share/autobotscripts/data/Big_Score.mscz" },
        { "Large orchestra 39 staves", large },
        { "Big Band test chart", ROOT + "tools/jubelle_templates/testscores/Big_Band_test_music.mscz" },
    };

    constexpr int EDITS = 20;
    std::vector<Row> rows;
    for (const auto& [name, path] : scores) {
        if (!std::ifstream(path).good()) {
            continue;
        }
        Row r;
        r.name = name;
        const double mem0 = residentMb();

        auto t0 = std::chrono::steady_clock::now();
        std::unique_ptr<MasterScore> s(load(path));
        r.readMs = msSince(t0);
        if (!s) {
            continue;
        }
        r.staves = s->nstaves();
        r.measures = s->nmeasures();

        t0 = std::chrono::steady_clock::now();
        s->setLayoutAll();
        s->doLayout();
        r.layoutMs = msSince(t0);
        r.memMb = residentMb() - mem0;

        r.editMs = editAverage(s.get(), EDITS);
        r.undoMs = undoAverage(s.get(), EDITS);

        mu::notation::jubelle::installLinkedPassagesHook();
        r.editHookMs = editAverage(s.get(), EDITS);
        undoAverage(s.get(), EDITS);
        Score::jubelleCmdHook = nullptr;

        t0 = std::chrono::steady_clock::now();
        muse::io::Buffer out;
        out.open(muse::io::IODevice::WriteOnly);
        rw::RWRegister::writer(s->iocContext())->writeScore(s.get(), &out);
        r.saveMs = msSince(t0);

        auto repo = std::make_shared<NiceMock<muse::mpe::ArticulationProfilesRepositoryMock> >();
        ON_CALL(*repo, defaultProfile(_)).WillByDefault(Return(std::make_shared<muse::mpe::ArticulationsProfile>()));
        t0 = std::chrono::steady_clock::now();
        {
            PlaybackModel model(muse::modularity::globalCtx());
            model.profilesRepository.set(repo);
            model.load(s.get());
        }
        r.playbackMs = msSince(t0);
        rows.push_back(r);
    }
    ASSERT_FALSE(rows.empty());

    std::ostringstream table, json;
    table << "| Score | Staves | Bars | Read | Layout | Edit | Edit + hook | Undo | Save | Playback model | Memory |\n"
          << "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    json << "[\n";
    char buf[512];
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        std::snprintf(buf, sizeof(buf), "| %s | %zu | %zu | %.0f ms | %.0f ms | %.1f ms | %.1f ms | %.1f ms | %.0f ms | %.0f ms | %.0f MB |\n",
                      r.name.c_str(), r.staves, r.measures, r.readMs, r.layoutMs, r.editMs, r.editHookMs, r.undoMs, r.saveMs,
                      r.playbackMs, r.memMb);
        table << buf;
        std::snprintf(buf, sizeof(buf),
                      "  {\"score\": \"%s\", \"staves\": %zu, \"bars\": %zu, \"readMs\": %.1f, \"layoutMs\": %.1f, \"editMs\": %.2f, "
                      "\"editHookMs\": %.2f, \"undoMs\": %.2f, \"saveMs\": %.1f, \"playbackModelMs\": %.1f, \"memoryMb\": %.1f}%s\n",
                      r.name.c_str(), r.staves, r.measures, r.readMs, r.layoutMs, r.editMs, r.editHookMs, r.undoMs, r.saveMs,
                      r.playbackMs, r.memMb, i + 1 < rows.size() ? "," : "");
        json << buf;
    }
    json << "]\n";
    std::cout << "\n[ Baselines ]\n" << table.str() << std::endl;
    std::ofstream("engine_baselines.json") << json.str();
    std::ofstream("engine_baselines.md") << table.str();
}

// ---------------------------------------------------------------------------
// Where does the time of one edit go? (17 Nov 2026: editing felt slower than
// MuseScore.) Each row enters the same notes with one Jubelle feature switched
// on at a time; "MuseScore path" has every Jubelle feature and hook off, so it
// is MuseScore 4.7.7's own code. JUBELLE_BASELINES=1 to run.
// ---------------------------------------------------------------------------
TEST(Jubelle_Baselines, EditingCostByFeature)
{
    const char* want = std::getenv("JUBELLE_BASELINES");
    if (!want || std::string(want) != "1") {
        GTEST_SKIP() << "set JUBELLE_BASELINES=1 to measure";
    }
    if (!muse::draw::FontProviderStub::realisticMetrics()) {
        GTEST_SKIP() << "needs realistic metrics (second pass)";
    }
    const std::string large = "/tmp/jubelle_baseline_large/score.mscx";
    std::system(("mkdir -p /tmp/jubelle_baseline_large && python3 " + ROOT + "tools/jubelle_checks/make_large_score.py " + ROOT
                 + "demos/Dawn.mscx " + large + " --instruments 3 --measures 15 > /dev/null 2>&1").c_str());
    const std::vector<std::pair<std::string, std::string> > scores {
        { "Big Band test chart", ROOT + "tools/jubelle_templates/testscores/Big_Band_test_music.mscz" },
        { "Large orchestra 39 staves", large },
    };
    struct Config {
        std::string name;
        std::function<void(MasterScore*)> setup;
        bool hook = false;
        bool playback = false;
    };
    const std::vector<Config> configs {
        { "MuseScore path (all Jubelle features off)", [](MasterScore*) {}, false, false },
        { "+ linked-passages hook", [](MasterScore*) {}, true, false },
        { "+ courtesy accidentals", [](MasterScore* s) { s->style().set(Sid::jubelleCourtesyAccidentals, true); }, false, false },
        { "+ magnetic layout", [](MasterScore* s) { s->style().set(Sid::jubelleMagneticLayout, true); }, false, false },
        { "+ collision avoidance", [](MasterScore* s) { s->style().set(Sid::jubelleCollisionAvoidance, true); }, false, false },
        { "+ engraving rules (jazz preset)", [](MasterScore* s) {
              s->style().set(Sid::jubelleEngravingRules,
                             muse::String(u"dynamics all below\nhairpins all below\ndynamics group:vocals above\nchordsymbols all above 1"));
          }, false, false },
        { "MuseScore path + playback model", [](MasterScore*) {}, false, true },
        { "everything on (a new Jubelle score) + playback", [](MasterScore* s) {
              s->style().set(Sid::jubelleCourtesyAccidentals, true);
              s->style().set(Sid::jubelleMagneticLayout, true);
              s->style().set(Sid::jubelleCollisionAvoidance, true);
          }, true, true },
    };
    constexpr int EDITS = 8;
    std::ostringstream table;
    table << "| Score | Configuration | Edit | Undo |\n|---|---|---:|---:|\n";
    for (const auto& [name, path] : scores) {
        if (!std::ifstream(path).good()) {
            continue;
        }
        for (const Config& c : configs) {
            std::unique_ptr<MasterScore> s(load(path));
            ASSERT_TRUE(s);
            c.setup(s.get());
            s->setLayoutAll();
            s->doLayout();
            Score::jubelleCmdHook = nullptr;
            if (c.hook) {
                mu::notation::jubelle::installLinkedPassagesHook();
            }
            auto repo = std::make_shared<NiceMock<muse::mpe::ArticulationProfilesRepositoryMock> >();
            ON_CALL(*repo, defaultProfile(_)).WillByDefault(Return(std::make_shared<muse::mpe::ArticulationsProfile>()));
            std::unique_ptr<PlaybackModel> model;
            if (c.playback) {
                model = std::make_unique<PlaybackModel>(muse::modularity::globalCtx());
                model->profilesRepository.set(repo);
                model->load(s.get());
            }
            editAverage(s.get(), 3);   // warm up
            const double edit = editAverage(s.get(), EDITS);
            const double undo = undoAverage(s.get(), EDITS);
            Score::jubelleCmdHook = nullptr;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "| %s | %s | %.1f ms | %.1f ms |\n", name.c_str(), c.name.c_str(), edit, undo);
            table << buf;
            std::cerr << buf << std::flush;
            model.reset();
        }
    }
    std::cout << "\n[ Editing cost ]\n" << table.str() << std::endl;
    std::ofstream("engine_editing_cost.md") << table.str();
}
