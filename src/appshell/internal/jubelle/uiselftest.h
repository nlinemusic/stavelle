/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "iinteractive.h"
#include "actions/iactionsdispatcher.h"
#include "context/iglobalcontext.h"

namespace mu::appshell::jubelle {
//! Jubelle build check, run only when the environment variable JUBELLE_UI_SELFTEST names a
//! report file (never in normal use). With a score open, it opens each of Jubelle's windows and
//! panels in turn, notes whether it appeared and every QML error while it was open, closes it,
//! writes the report and quits. A window that fails to load does not stop the build otherwise.
class UiSelfTest : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    UiSelfTest(const muse::modularity::ContextPtr& ctx, const QString& reportPath, QObject* parent = nullptr);

    static bool requested();
    static QString reportPathFromEnvironment();

    void start();

private:
    struct Step {
        QString name;
        QString uri;       // a window to open, or
        QString action;    // an action to run (panels, pages)
        bool dialog = true;
    };

    void waitForScore();
    void runStep();
    void finishStep();
    void writeReportAndQuit();

    QString m_reportPath;
    std::vector<Step> m_steps;
    size_t m_index = 0;
    int m_waited = 0;
    int m_errorsBefore = 0;
    QStringList m_lines;
    int m_failures = 0;
};

//! QML warnings and errors seen so far (collected by a message handler that passes everything on)
QStringList& recordedQmlMessages();
void installQmlMessageRecorder();
}
