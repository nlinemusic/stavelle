// Jubelle: Qt-free runner for engraving (notation engine) gtest files, using the
// real engine. Sets up what EngravingModule would: file system, fonts (stub
// drawing, real SMuFL metadata), renderers, default style, staff types.
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "modularity/ioc.h"
#include "fontproviderstub.h"
#include "stdfs.h"

#include "engraving/dom/mscore.h"
#include "engraving/internal/engravingfontsprovider.h"
#include "engraving/rendering/score/scorerenderer.h"
#include "engraving/rendering/single/singlerenderer.h"
#include "engraving/rendering/editmode/editmoderenderer.h"
#include "engraving/infrastructure/smufl.h"
#include "engraving/style/defaultstyle.h"
#include "engraving/dom/stafftype.h"
#include "engraving/dom/drumset.h"
#include "engraving/dom/figuredbass.h"
#include "mocks/engravingconfigurationmock.h"
#include "global/tests/mocks/applicationmock.h"
#include "utils/scorerw.h"

using namespace mu::engraving;

static const IEngravingConfiguration::DebuggingOptions debugOpt {};

int main(int argc, char** argv)
{
    ::testing::InitGoogleMock(&argc, argv);

    // MuseScore tests that can't run without Qt here (not Jubelle problems):
    // repeat14 reads chord-symbol descriptions (number parsing needs Qt);
    // the glissando renderer tests lay out glissando lines, which needs real
    // font outlines; Playback_Setup_Data_MultiInstrument needs the
    // instruments list from Qt resources. They run in "Check: Unit Tests".
    if (::testing::GTEST_FLAG(filter) == "*") {
        ::testing::GTEST_FLAG(filter) = "-Engraving_RepeatTests.repeat14"
                                        ":Engraving_PlaybackEventsRendererTests.*Glissando*"
                                        ":Engraving_PlaybackModelTests.Playback_Setup_Data_MultiInstrument";
    }
    auto ioc = muse::modularity::globalIoc();

    ioc->registerExport<muse::io::IFileSystem>("jubelle_engine_tests", new StdFileSystem());
    ioc->registerExport<muse::draw::IFontProvider>("jubelle_engine_tests", new muse::draw::FontProviderStub());

    using ECMock = ::testing::NiceMock<EngravingConfigurationMock>;
    std::shared_ptr<ECMock> cfg(new ECMock(), [](ECMock*) {});
    ON_CALL(*cfg, isAccessibleEnabled()).WillByDefault(::testing::Return(false));
    ON_CALL(*cfg, defaultColor()).WillByDefault(::testing::Return(muse::draw::Color::BLACK));
    ON_CALL(*cfg, debuggingOptions()).WillByDefault(::testing::ReturnRef(debugOpt));
    // Score objects keep pointers to the configuration until exit, as in the
    // engraving test environment, so the mock is intentionally never deleted.
    ::testing::Mock::AllowLeak(cfg.get());
    ioc->registerExport<IEngravingConfiguration>("jubelle_engine_tests", cfg);

    // Saving a score asks the application for its version
    using AppMock = ::testing::NiceMock<muse::ApplicationMock>;
    std::shared_ptr<AppMock> app(new AppMock(), [](AppMock*) {});
    ON_CALL(*app, version()).WillByDefault(::testing::Return(muse::Version(4, 7, 7)));
    ON_CALL(*app, revision()).WillByDefault(::testing::Return(muse::String(u"0")));
    ::testing::Mock::AllowLeak(app.get());
    ioc->registerExport<muse::IApplication>("jubelle_engine_tests", app);

    auto fonts = std::make_shared<EngravingFontsProvider>(nullptr);
    const std::string fontsDir = std::string(MU_ROOT_DIR) + "/fonts/";
    fonts->addExternalFont("Leland", "Leland", fontsDir + "leland/Leland.otf", fontsDir + "leland/leland_metadata.json");
    fonts->addExternalFont("Bravura", "Bravura", fontsDir + "bravura/Bravura.otf", fontsDir + "bravura/bravura_metadata.json");
    fonts->setFallbackFont("Bravura");
    ioc->registerExport<IEngravingFontsProvider>("jubelle_engine_tests", fonts);

    ioc->registerExport<rendering::IScoreRenderer>("jubelle_engine_tests", new rendering::score::ScoreRenderer());
    ioc->registerExport<rendering::ISingleRenderer>("jubelle_engine_tests", new rendering::single::SingleRenderer());
    ioc->registerExport<rendering::IEditModeRenderer>("jubelle_engine_tests", new rendering::editmode::EditModeRenderer());

    Smufl::init();
    DefaultStyle::instance()->init(muse::io::path_t(), muse::io::path_t(), SizeF(210, 297));
    StaffType::initStaffTypes(muse::draw::Color::BLACK);
    Drumset::initDrumset();
    FiguredBass::readConfigFile(muse::String());
    MScore::testMode = true;
    MScore::noGui = true;
    ScoreRW::setRootPath(muse::String::fromUtf8(std::string(MU_ROOT_DIR) + "/src/engraving/tests"));

    return RUN_ALL_TESTS();
}
