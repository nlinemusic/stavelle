/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle: a score that uses the features stored in score files (recorded tempo,
 * timeline markers, Jubelle's page breaks, magnetic layout settings), made from the
 * big band test chart. The Windows coexistence check opens it in MuseScore 4.7.7 and
 * compares the tempo it plays with Jubelle's.
 *
 * Run with JUBELLE_WRITE_FEATURE_SCORE=<path.mscz> to write it; otherwise it only checks
 * that the score can be made and reread.
 */
#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "engraving/compat/scoreaccess.h"
#include "engraving/compat/mscxcompat.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/tempotext.h"
#include "engraving/infrastructure/localfileinfoprovider.h"

#include "notation/internal/jubelle/pageturns.h"
#include "notation/internal/jubelle/tempotap.h"
#include "notation/internal/jubelle/timelinemarkers.h"

#include "templatebuilder.h"

using namespace mu::engraving;
namespace jf = mu::notation::jubelle;

namespace {
const std::string ROOT = std::string(MU_ROOT_DIR) + "/";

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

Measure* bar(Score* s, int n)
{
    Measure* m = s->firstMeasure();
    for (int i = 1; i < n && m; ++i) {
        m = m->nextMeasure();
    }
    return m;
}

//! the tempo at the start of each bar, rounded to 0.1 bpm
std::vector<double> tempoMap(Score* s)
{
    std::vector<double> out;
    for (Measure* m = s->firstMeasure(); m; m = m->nextMeasure()) {
        out.push_back(std::round(s->tempo(m->tick()).toBPM().val * 10.0) / 10.0);
    }
    return out;
}
}

TEST(Jubelle_FeatureScore, MadeFromTheBigBandChartAndReread)
{
    jubelle_templates::loadInstrumentData();
    std::unique_ptr<MasterScore> s(loadChart(ROOT + "tools/jubelle_templates/testscores/Big_Band_test_music.mscz"));
    ASSERT_TRUE(s);
    s->doLayout();
    ASSERT_GE(s->nmeasures(), 24);

    s->startCmd(muse::TranslatableString::untranslatable("feature score"));
    // recorded tempo: a ritardando into bar 16, back a tempo at bar 17
    const Fraction from = bar(s.get(), 13)->tick();
    const Fraction to = bar(s.get(), 17)->tick();
    jf::applyTempoPoints(s.get(), { { bar(s.get(), 13)->tick(), 150 }, { bar(s.get(), 14)->tick(), 138 },
                             { bar(s.get(), 15)->tick(), 124 }, { bar(s.get(), 16)->tick(), 108 } }, from, to);
    // timeline markers
    jf::addTimelineMarker(s.get(), bar(s.get(), 9)->tick(), u"Shout chorus");
    jf::addTimelineMarker(s.get(), bar(s.get(), 21)->tick(), u"Check trumpet 1 voicing");
    s->endCmd();

    // parts with Jubelle's page turns
    for (Excerpt* ex : Excerpt::createExcerptsFromParts(s->parts(), s.get())) {
        s->initAndAddExcerpt(ex, false);
    }
    s->startCmd(muse::TranslatableString::untranslatable("page turns"));
    for (Excerpt* ex : s->excerpts()) {
        ex->excerptScore()->doLayout();
        jf::TurnSettings st;
        st.layout = jf::TurnLayout::EveryPage;
        jf::optimiseTurns(ex->excerptScore(), st);
    }
    s->endCmd();
    s->doLayout();

    const std::vector<double> before = tempoMap(s.get());
    EXPECT_LT(before[14], before[12]) << "the ritardando is in the tempo map";
    const size_t markers = jf::timelineMarkers(s.get()).size();
    EXPECT_EQ(markers, 2u);

    const char* outEnv = std::getenv("JUBELLE_WRITE_FEATURE_SCORE");
    const std::string out = outEnv ? std::string(outEnv) : ::testing::TempDir() + "jubelle_featurescore/Jubelle_features.mscz";
    std::filesystem::create_directories(std::filesystem::path(out).parent_path());
    ASSERT_TRUE(jubelle_templates::writeMscz(s.get(), out));

    std::unique_ptr<MasterScore> back(loadChart(out));
    ASSERT_TRUE(back);
    back->doLayout();
    EXPECT_EQ(tempoMap(back.get()), before) << "the tempo plays the same after saving";
    EXPECT_EQ(jf::timelineMarkers(back.get()).size(), markers);
    EXPECT_EQ(jf::recordedTempoPoints(back.get()).size(), jf::recordedTempoPoints(s.get()).size());

    // the tempo map, for the Windows check to compare MuseScore's MIDI against
    std::ofstream map(std::filesystem::path(out).replace_extension(".tempo.txt"));
    for (double b : before) {
        map << b << "\n";
    }
}
