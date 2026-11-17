/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 * Music Composition & Notation
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
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "actions/iactionsdispatcher.h"
#include "context/iglobalcontext.h"
#include "iinteractive.h"
#include "global/iglobalconfiguration.h"

#include "youtubeaccount.h"

namespace mu::project {
//! Jubelle: "Publish to YouTube" (an extra Publish destination).
//! Lists the project's exported videos and opens YouTube Studio in the web
//! browser with the chosen file shown in Explorer/Finder, ready to drag in.
//! There is deliberately no Google sign-in and no uploading here: connecting a
//! YouTube account waits for the user's approval of the exact permission.
class YouTubePublishModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList videos READ videos NOTIFY videosChanged)
    Q_PROPERTY(QString selectedPath READ selectedPath WRITE setSelectedPath NOTIFY selectedPathChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY videosChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY accountChanged)
    // Jubelle G10: the YouTube connection (design approved 17 Nov 2026)
    Q_PROPERTY(bool hasClient READ hasClient NOTIFY accountChanged)
    Q_PROPERTY(QString clientId READ clientId NOTIFY accountChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY accountChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(int uploadPercent READ uploadPercent NOTIFY statusChanged)
    Q_PROPERTY(QString lastLink READ lastLink NOTIFY statusChanged)

    QML_ELEMENT

    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<context::IGlobalContext> context = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::GlobalInject<muse::IGlobalConfiguration> globalConfiguration;

public:
    explicit YouTubePublishModel(QObject* parent = nullptr);

    QVariantList videos() const;
    QString selectedPath() const;
    void setSelectedPath(const QString& path);
    QString projectName() const;

    bool isConnected() const;
    bool hasClient() const;
    QString clientId() const;
    bool busy() const;
    QString statusText() const { return m_status; }
    int uploadPercent() const { return m_percent; }
    QString lastLink() const { return m_lastLink; }

    Q_INVOKABLE void saveClient(const QString& clientId, const QString& clientSecret);
    Q_INVOKABLE void openGoogleSetupPage();
    Q_INVOKABLE void connectAccount();
    Q_INVOKABLE void disconnectAccount();
    //! privacy: 0 private, 1 unlisted, 2 public
    Q_INVOKABLE void upload(const QString& title, const QString& description, int privacy);
    Q_INVOKABLE void cancelUpload();
    Q_INVOKABLE void openLastLink();

    Q_INVOKABLE void load();
    Q_INVOKABLE void chooseOtherFile();
    Q_INVOKABLE void revealSelected();
    Q_INVOKABLE void exportNewVideo();
    Q_INVOKABLE void openYouTubeStudio();

signals:
    void videosChanged();
    void selectedPathChanged();
    void accountChanged();
    void statusChanged();

private:
    QString projectFolder() const;
    QString scoreFolder() const;

    jubelle::YouTubeAccount* account();

    QVariantList m_videos;
    QString m_selectedPath;
    jubelle::YouTubeAccount* m_account = nullptr;
    QString m_status;
    int m_percent = -1;
    QString m_lastLink;
};
}
