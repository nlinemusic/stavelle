/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "uiselftest.h"

#include <QCoreApplication>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

#include "actions/actiontypes.h"

#include "log.h"

using namespace mu::appshell::jubelle;
using namespace muse;

namespace {
QtMessageHandler s_previous = nullptr;
QMutex s_mutex;

void recorder(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    const QString file = ctx.file ? QString::fromUtf8(ctx.file) : QString();
    const bool qml = msg.contains(QStringLiteral(".qml")) || file.endsWith(QStringLiteral(".qml"));
    if (qml && type != QtDebugMsg && type != QtInfoMsg) {
        QMutexLocker lock(&s_mutex);
        recordedQmlMessages() << (file.isEmpty() ? msg : file + QStringLiteral(":") + QString::number(ctx.line) + QStringLiteral(": ") + msg);
    }
    if (s_previous) {
        s_previous(type, ctx, msg);
    }
}
}

QStringList& mu::appshell::jubelle::recordedQmlMessages()
{
    static QStringList list;
    return list;
}

void mu::appshell::jubelle::installQmlMessageRecorder()
{
    static bool installed = false;
    if (!installed) {
        installed = true;
        s_previous = qInstallMessageHandler(recorder);
    }
}

bool UiSelfTest::requested()
{
    return !reportPathFromEnvironment().isEmpty();
}

QString UiSelfTest::reportPathFromEnvironment()
{
    return qEnvironmentVariable("JUBELLE_UI_SELFTEST");
}

UiSelfTest::UiSelfTest(const muse::modularity::ContextPtr& ctx, const QString& reportPath, QObject* parent)
    : QObject(parent), muse::Contextable(ctx), m_reportPath(reportPath)
{
    // every window and panel Jubelle added (G4–G10 and earlier), and the score-window panels
    m_steps = {
        { "Find and Select", "musescore://notation/jubelle/findselect", "", true },
        { "Record tempo", "musescore://notation/jubelle/temporecord", "", true },
        { "Page turns", "musescore://notation/jubelle/pageturns", "", true },
        { "Export drum stems", "musescore://project/jubelle/drumstems", "", true },
        { "Publish to YouTube", "musescore://project/jubelle/youtube", "", true },
        { "Export (with MP4 settings)", "musescore://project/export", "", true },
        { "Swing", "", "jubelle-swing", true },
        { "Articulation playback", "", "jubelle-articulation-playback", true },
        { "Repeats", "", "jubelle-repeats", true },
        { "Engraving presets", "", "jubelle-engraving-presets", true },
        { "Apply Part Layout", "", "jubelle-apply-part-layout", true },
        { "Linked passages", "", "jubelle-linked-passages", true },
        { "Chord Identifier", "", "jubelle-identify-chords", true },
        { "Style (with the Jubelle layout page)", "", "edit-style", true },
        { "Proofread panel", "", "jubelle-toggle-proofread", false },
        { "Timeline", "", "toggle-timeline", false },
        { "Home: Learning Center", "musescore://home?section=learning", "", false },
        { "Home: Projects", "musescore://home?section=scores", "", false },
        { "Home: Templates", "musescore://home?section=templates", "", false },
        { "Home: Plugins", "musescore://home?section=plugins", "", false },
    };
}

void UiSelfTest::start()
{
    installQmlMessageRecorder();
    m_lines << QStringLiteral("# Jubelle interface self-test") << QString()
            << QStringLiteral("| Window or panel | Opened | QML errors while open |")
            << QStringLiteral("|---|---|---|");
    QTimer::singleShot(1000, this, &UiSelfTest::waitForScore);
}

void UiSelfTest::waitForScore()
{
    if (globalContext()->currentNotation() || m_waited > 120) {
        if (!globalContext()->currentNotation()) {
            m_lines << QStringLiteral("| (no score opened within 2 minutes) | no | |");
            ++m_failures;
        }
        // errors from opening the score window itself
        if (!recordedQmlMessages().isEmpty()) {
            m_lines << QStringLiteral("| Score window | yes | %1 |").arg(recordedQmlMessages().join(QStringLiteral("<br>")).toHtmlEscaped());
        } else {
            m_lines << QStringLiteral("| Score window | yes | none |");
        }
        QTimer::singleShot(1500, this, &UiSelfTest::runStep);
        return;
    }
    ++m_waited;
    QTimer::singleShot(1000, this, &UiSelfTest::waitForScore);
}

void UiSelfTest::runStep()
{
    if (m_index >= m_steps.size()) {
        writeReportAndQuit();
        return;
    }
    const Step& s = m_steps[m_index];
    m_errorsBefore = int(recordedQmlMessages().size());
    if (!s.uri.isEmpty()) {
        interactive()->open(UriQuery(s.uri.toStdString()));
    } else {
        dispatcher()->dispatch(s.action.toStdString());
    }
    QTimer::singleShot(3000, this, &UiSelfTest::finishStep);
}

void UiSelfTest::finishStep()
{
    const Step& s = m_steps[m_index];
    bool opened = true;
    if (s.dialog && !s.uri.isEmpty()) {
        opened = interactive()->isOpened(UriQuery(s.uri.toStdString())).val;
    }
    const QStringList all = recordedQmlMessages();
    const QStringList mine = all.mid(m_errorsBefore);
    if (!opened || !mine.isEmpty()) {
        ++m_failures;
    }
    m_lines << QStringLiteral("| %1 | %2 | %3 |").arg(s.name, opened ? QStringLiteral("yes") : QStringLiteral("**no**"),
                                                    mine.isEmpty() ? QStringLiteral("none") : mine.join(QStringLiteral("<br>")).toHtmlEscaped());

    // close what this step opened, then go on
    interactive()->closeAllDialogsSync();
    if (!s.dialog && !s.action.isEmpty()) {
        dispatcher()->dispatch(s.action.toStdString());   // panels: toggle back off
    }
    if (!s.dialog && !s.uri.isEmpty()) {
        interactive()->open(UriQuery("musescore://notation"));
    }
    ++m_index;
    QTimer::singleShot(1500, this, &UiSelfTest::runStep);
}

void UiSelfTest::writeReportAndQuit()
{
    m_lines << QString() << (m_failures == 0 ? QStringLiteral("**Result: PASS** (every window opened, no QML errors)")
                             : QStringLiteral("**Result: FAIL** (%1 problem(s))").arg(m_failures));
    QFile f(m_reportPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(m_lines.join(QStringLiteral("\n")).toUtf8() + "\n");
    }
    LOGI() << "Jubelle UI self-test written to " << m_reportPath;
    interactive()->closeAllDialogsSync();
    dispatcher()->dispatch("quit", actions::ActionData::make_arg1<bool>(false));
}
