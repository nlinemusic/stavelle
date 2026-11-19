/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "startupscenario.h"

#include "jubelle/uiselftest.h"

#include "jubelle/crashguard.h"
#include "jubelle/crashreport.h"

#include <QDir>
#include <QUrl>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>

#include <QCoreApplication>

#include "async/async.h"
#include "translation.h"

#include "muse_framework_config.h"

#include "log.h"

using namespace mu::appshell;
using namespace muse;
using namespace muse::actions;

[[maybe_unused]] static const muse::UriQuery FIRST_LAUNCH_SETUP_URI("musescore://firstLaunchSetup?floating=true");

static StartupModeType modeTypeTromString(const std::string& str)
{
    if ("start-empty" == str) {
        return StartupModeType::StartEmpty;
    }

    if ("continue-last" == str) {
        return StartupModeType::ContinueLastSession;
    }

    if ("start-with-new" == str) {
        return StartupModeType::StartWithNewScore;
    }

    if ("start-with-file" == str) {
        return StartupModeType::StartWithScore;
    }

    return StartupModeType::StartEmpty;
}

static const Uri& startupPageUri(StartupModeType modeType)
{
    switch (modeType) {
    case StartupModeType::StartEmpty:
    case StartupModeType::StartWithNewScore:
    case StartupModeType::Recovery:
        return HOME_URI;
    case StartupModeType::StartWithScore:
    case StartupModeType::ContinueLastSession:
        return NOTATION_URI;
    }

    return HOME_URI;
}

void StartupScenario::setStartupType(const std::optional<std::string>& type)
{
    m_startupTypeStr = type ? type.value() : "";
}

bool StartupScenario::isStartWithNewFileAsSecondaryInstance() const
{
    if (m_startupScoreFile.isValid()) {
        return false;
    }

    if (!m_startupTypeStr.empty()) {
        return modeTypeTromString(m_startupTypeStr) == StartupModeType::StartWithNewScore;
    }

    return false;
}

const mu::project::ProjectFile& StartupScenario::startupScoreFile() const
{
    return m_startupScoreFile;
}

void StartupScenario::setStartupScoreFile(const std::optional<project::ProjectFile>& file)
{
    m_startupScoreFile = file ? file.value() : project::ProjectFile();
}

void StartupScenario::runOnSplashScreen()
{
    if (jubelle::UiSelfTest::requested()) {
        jubelle::installQmlMessageRecorder();   // from the very start, so the score window's own errors count
    }

    TRACEFUNC;

    if (multiwindowsProvider()->windowCount() != 1) {
        registerAudioPlugins();
        return;
    }

    if (appUpdateScenario() && appUpdateScenario()->needCheckForUpdate()) {
        appUpdateScenario()->checkForUpdate(/*manual*/ false);
    }

    if (museSoundsUpdateScenario() && museSoundsUpdateScenario()->needCheckForUpdate()) {
        museSoundsUpdateScenario()->checkForUpdate(/*manual*/ false);
    }

    registerAudioPlugins();
}

void StartupScenario::registerAudioPlugins()
{
    if (!registerAudioPluginsScenario()) {
        return;
    }

    //! NOTE Registering plugins shows a window (dialog) before the main window is shown.
    //! After closing it, the application may in a state where there are no open windows,
    //! which leads to automatic exit from the application.
    //! (Thanks to the splashscreen, but this is not an obvious detail)
    qApp->setQuitLockEnabled(false);
    registerAudioPluginsScenario()->updatePluginsRegistry();
    qApp->setQuitLockEnabled(true);
}

void StartupScenario::runAfterSplashScreen()
{
    TRACEFUNC;

#ifdef MUSE_MULTICONTEXT_WIP
    interactive()->open(HOME_URI);
    return;
#endif

    if (m_startupCompleted) {
        return;
    }

    StartupModeType modeType = resolveStartupModeType();
    if (multiwindowsProvider()->windowCount() == 1 && sessionsManager()->hasProjectsForRestore()) {
        modeType = StartupModeType::Recovery;
    }

    muse::async::Channel<Uri> opened = interactive()->opened();
    opened.onReceive(this, [this, opened, modeType](const Uri&) {
        static bool once = false;
        if (once) {
            return;
        }
        once = true;

        onStartupPageOpened(modeType);

        async::Async::call(this, [this, opened]() {
            muse::async::Channel<Uri> mut = opened;
            mut.disconnect(this);
            m_startupCompleted = true;
        });
    });

    const Uri& startupUri = startupPageUri(modeType);
    interactive()->open(startupUri);
}

bool StartupScenario::startupCompleted() const
{
    return m_startupCompleted;
}

StartupModeType StartupScenario::resolveStartupModeType() const
{
    if (m_startupScoreFile.isValid()) {
        return StartupModeType::StartWithScore;
    }

    if (!m_startupTypeStr.empty()) {
        return modeTypeTromString(m_startupTypeStr);
    }

    return configuration()->startupModeType();
}

void StartupScenario::onStartupPageOpened(StartupModeType modeType)
{
    TRACEFUNC;

    switch (modeType) {
    case StartupModeType::StartEmpty:
        break;
    case StartupModeType::StartWithNewScore:
        dispatcher()->dispatch("file-new");
        break;
    case StartupModeType::ContinueLastSession:
        dispatcher()->dispatch("continue-last-session");
        break;
    case StartupModeType::Recovery:
        restoreLastSession();
        break;
    case StartupModeType::StartWithScore: {
        project::ProjectFile file = m_startupScoreFile.isValid()
                                    ? m_startupScoreFile
                                    : project::ProjectFile(configuration()->startupScorePath());
        openScore(file);
    } break;
    }

    m_activeUpdateCheckCount = 0;

    if (appUpdateScenario() && appUpdateScenario()->checkInProgress()) {
        m_activeUpdateCheckCount++;
        appUpdateScenario()->checkInProgressChanged().onNotify(this, [this, modeType]() {
            appUpdateScenario()->checkInProgressChanged().disconnect(this);
            m_activeUpdateCheckCount--;
            showStartupDialogsIfNeed(modeType);
        }, Asyncable::Mode::SetReplace);
    }

    if (museSoundsUpdateScenario() && museSoundsUpdateScenario()->checkInProgress()) {
        m_activeUpdateCheckCount++;
        museSoundsUpdateScenario()->checkInProgressChanged().onNotify(this, [this, modeType]() {
            museSoundsUpdateScenario()->checkInProgressChanged().disconnect(this);
            m_activeUpdateCheckCount--;
            showStartupDialogsIfNeed(modeType);
        }, Asyncable::Mode::SetReplace);
    }

    // Jubelle build check: only with JUBELLE_UI_SELFTEST set (the Windows build's app check)
    if (jubelle::UiSelfTest::requested()) {
        auto* test = new jubelle::UiSelfTest(iocContext(), jubelle::UiSelfTest::reportPathFromEnvironment(), nullptr);
        test->start();
        return;
    }

    showStartupDialogsIfNeed(modeType);
    offerCrashReportIfNeed();
}

void StartupScenario::offerCrashReportIfNeed()
{
    // Jubelle R3 (D7): last time's crash report, offered once. Nothing is sent
    // unless you choose Send, and then only through your own browser.
    if (!jubelleGlobalConfiguration()) {
        return;
    }
    const std::string dataDir = jubelleGlobalConfiguration()->userAppDataPath().toStdString();
    const std::string pending = jubelle::CrashGuard::pendingReport(dataDir);
    if (pending.empty()) {
        return;
    }
    std::string text;
    {
        std::ifstream in(std::filesystem::u8path(pending), std::ios::binary);
        std::stringstream s;
        s << in.rdbuf();
        text = s.str();
    }
    const jubelle::CrashReport report = jubelle::CrashReport::fromText(text);
    const QString home = QDir::homePath();
    const std::string homeDir = QDir::toNativeSeparators(home).toStdString();
    const std::string userName = qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER")).toStdString();
    const std::string title = report.issueTitle();
    const std::string body = report.issueBody(homeDir, userName);

    // What would be sent, readable before anything happens
    const std::string reviewFile = jubelle::CrashGuard::folder(dataDir) + "/report-to-send.txt";
    std::ofstream(std::filesystem::u8path(reviewFile), std::ios::trunc) << jubelle::sanitize(title, homeDir, userName) << "\n\n" << body;

    const bool knownCrash = jubelle::readSentSignatures(jubelle::CrashGuard::sentSignaturesFile(dataDir)).count(report.signature()) > 0;
    const int REVIEW = int(IInteractive::Button::CustomButton) + 1;
    const int SEND = REVIEW + 1;
    const int DONT = REVIEW + 2;

    const std::string question = report.frames.empty()
                                 ? muse::trc("appshell", "Jubelle didn’t close normally last time.")
                                 : muse::trc("appshell", "Jubelle closed because of a problem last time.");
    std::string explanation = muse::trc("appshell",
                                        "A short report can help fix it. It holds only Jubelle’s version, your system’s version, "
                                        "the kind of problem and where in Jubelle’s code it happened. Names, folders, file names "
                                        "and score titles are removed. Send opens your browser on a filled-in report in your private "
                                        "crash-report repository; you look at it and submit it yourself.");
    if (knownCrash) {
        explanation += "\n\n" + muse::trc("appshell", "You’ve sent this problem before: Send opens that report so you can add a note.");
    }

    auto ask = std::make_shared<std::function<void()> >();
    *ask = [this, ask, question, explanation, title, body, reviewFile, pending, dataDir, knownCrash, report, REVIEW, SEND, DONT]() {
        auto promise = interactive()->question(question, explanation,
                                               { IInteractive::ButtonData(REVIEW, muse::trc("appshell", "Review report…")),
                                                 IInteractive::ButtonData(DONT, muse::trc("appshell", "Don’t send")),
                                                 IInteractive::ButtonData(SEND, muse::trc("appshell", "Send…"), true) });
        promise.onResolve(this, [this, ask, title, body, reviewFile, pending, dataDir, knownCrash, report, REVIEW, SEND](
                              const IInteractive::Result& res) {
            if (res.button() == REVIEW) {
                interactive()->openUrl(QUrl::fromLocalFile(QString::fromStdString(reviewFile)));
                (*ask)();   // then the same choice again
                return;
            }
            if (res.button() == SEND) {
                const std::string url = knownCrash
                                        ? jubelle::searchIssuesUrl(jubelle::CRASH_REPORT_REPOSITORY, report.signature())
                                        : jubelle::newIssueUrl(jubelle::CRASH_REPORT_REPOSITORY, title, body);
                interactive()->openUrl(QUrl::fromEncoded(QByteArray::fromStdString(url)));
                jubelle::rememberSentSignature(jubelle::CrashGuard::sentSignaturesFile(dataDir), report.signature());
            }
            std::error_code ec;
            // kept (replacing the one before) so Help › Report a bug can include it if you choose
            const std::filesystem::path lastCrash = std::filesystem::u8path(jubelle::CrashGuard::folder(dataDir)) / "last-crash.txt";
            std::filesystem::remove(lastCrash, ec);
            std::filesystem::rename(std::filesystem::u8path(pending), lastCrash, ec);
            if (ec) {
                std::filesystem::remove(std::filesystem::u8path(pending), ec);
            }
            std::filesystem::remove(std::filesystem::u8path(reviewFile), ec);
            *ask = nullptr;   // done
        });
    };
    (*ask)();
}

void StartupScenario::showStartupDialogsIfNeed(StartupModeType modeType)
{
    TRACEFUNC;

    if (m_activeUpdateCheckCount != 0) {
        return;
    }

    //! Jubelle: MuseScore's startup welcome dialog (promotions for MuseScore.com, MuseSounds
    //! and tutorials) and its first launch setup ("Getting started": theme, playback and
    //! tutorial pages) are not shown (Refinement Phase R2, approved 15 Nov 2026). The theme
    //! stays in Preferences. The MuseSounds status check still runs.
    const auto showFirstLaunchOrSamplerStatusIfNeed = [this, modeType]() {
        if (!configuration()->hasCompletedFirstLaunchSetup()) {
            configuration()->setHasCompletedFirstLaunchSetup(true);
        }

        const bool shouldCheckForMuseSamplerUpdate = modeType == StartupModeType::StartEmpty
                                                     || modeType == StartupModeType::StartWithNewScore;

        if (shouldCheckForMuseSamplerUpdate) {
            checkAndShowMuseSamplerStatusIfNeed();
        }
    };

    if (!appUpdateScenario() || !appUpdateScenario()->hasUpdate()) {
        showFirstLaunchOrSamplerStatusIfNeed();
        return;
    }

    auto promise = appUpdateScenario()->showUpdate();
    promise.onResolve(this, [showFirstLaunchOrSamplerStatusIfNeed](const Ret& ret) {
        if (ret.code() == static_cast<int>(Ret::Code::Ok)) {
            return; // OK means the user wants to close and complete installation - don't show any more dialogs...
        }
        showFirstLaunchOrSamplerStatusIfNeed();
    });
}

void StartupScenario::checkAndShowMuseSamplerStatusIfNeed()
{
    if (museSamplerCheckStatusScenario() && !museSamplerCheckStatusScenario()->alreadyChecked()) {
        museSamplerCheckStatusScenario()->checkAndShowStatusIfNeed();
    }
}

void StartupScenario::openScore(const project::ProjectFile& file)
{
    dispatcher()->dispatch("file-open", ActionData::make_arg2<QUrl, QString>(file.url, file.displayNameOverride));
}

void StartupScenario::restoreLastSession()
{
    auto promise = interactive()->question(muse::trc("appshell", "The previous session quit unexpectedly."),
                                           muse::trc("appshell", "Do you want to restore the session?"),
                                           { IInteractive::Button::No, IInteractive::Button::Yes });

    promise.onResolve(this, [this](const IInteractive::Result& res) {
        if (res.isButton(IInteractive::Button::Yes)) {
            sessionsManager()->restore();
        } else {
            removeProjectsUnsavedChanges(configuration()->sessionProjectsPaths());
            sessionsManager()->reset();
            checkAndShowMuseSamplerStatusIfNeed();
        }
    });
}

void StartupScenario::removeProjectsUnsavedChanges(const io::paths_t& projectsPaths)
{
    for (const muse::io::path_t& path : projectsPaths) {
        projectAutoSaver()->removeProjectUnsavedChanges(path);
    }
}
