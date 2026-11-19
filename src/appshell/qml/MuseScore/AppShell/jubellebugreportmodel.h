/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore). Help › Report a bug…: the window.
 */
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "global/iglobalconfiguration.h"
#include "global/iinteractive.h"
#include "ui/imainwindow.h"
#include "context/iglobalcontext.h"

#include "internal/jubelle/bugreport/bugreport.h"

namespace mu::appshell {
class BugReportModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString title MEMBER m_title NOTIFY changed)
    Q_PROPERTY(QString description MEMBER m_description NOTIFY changed)
    Q_PROPERTY(QString steps MEMBER m_steps NOTIFY changed)
    Q_PROPERTY(QString expected MEMBER m_expected NOTIFY changed)
    Q_PROPERTY(QString actual MEMBER m_actual NOTIFY changed)
    Q_PROPERTY(QString comments MEMBER m_comments NOTIFY changed)
    Q_PROPERTY(bool includeLogs MEMBER m_includeLogs NOTIFY changed)
    Q_PROPERTY(bool includeScore MEMBER m_includeScore NOTIFY changed)
    Q_PROPERTY(bool scoreAvailable READ scoreAvailable NOTIFY changed)
    Q_PROPERTY(QStringList screenshots READ screenshotNames NOTIFY screenshotsChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    Q_PROPERTY(QString savedPath READ savedPath NOTIFY savedPathChanged)

    muse::GlobalInject<muse::IGlobalConfiguration> globalConfiguration;
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::ui::IMainWindow> mainWindow = { this };
    muse::ContextInject<mu::context::IGlobalContext> globalContext = { this };

public:
    explicit BugReportModel(QObject* parent = nullptr);

    bool scoreAvailable() const;
    QStringList screenshotNames() const;
    QString problem() const;
    QString savedPath() const;

    //! What the package will hold and what each part is, in words
    Q_INVOKABLE QString includedSummary() const;
    //! The whole report as it will be saved (Markdown), already cleaned
    Q_INVOKABLE QString preview();

    Q_INVOKABLE void addScreenshotFromFile();
    Q_INVOKABLE void addScreenshotOfJubelle();
    Q_INVOKABLE void removeScreenshot(int index);

    //! Makes the ZIP; true when saved
    Q_INVOKABLE bool createReport();
    Q_INVOKABLE void showReportInFolder() const;

signals:
    void changed();
    void screenshotsChanged();
    void savedPathChanged();

private:
    jubelle::bugreport::BugReportInput input() const;
    jubelle::bugreport::BugReport build();

    QString m_title, m_description, m_steps, m_expected, m_actual, m_comments;
    bool m_includeLogs = true;
    bool m_includeScore = false;   // never on by default
    std::vector<jubelle::bugreport::Attachment> m_screenshots;
    std::string m_id;
    std::string m_createdAt;
    QString m_savedPath;
};
}
