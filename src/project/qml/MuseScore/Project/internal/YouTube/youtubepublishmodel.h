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
    Q_PROPERTY(bool isConnected READ isConnected CONSTANT)

    QML_ELEMENT

    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<context::IGlobalContext> context = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };

public:
    explicit YouTubePublishModel(QObject* parent = nullptr);

    QVariantList videos() const;
    QString selectedPath() const;
    void setSelectedPath(const QString& path);
    QString projectName() const;

    //! Always false for now: the Google connection is not implemented yet.
    bool isConnected() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void chooseOtherFile();
    Q_INVOKABLE void revealSelected();
    Q_INVOKABLE void exportNewVideo();
    Q_INVOKABLE void openYouTubeStudio();

signals:
    void videosChanged();
    void selectedPathChanged();

private:
    QString projectFolder() const;
    QString scoreFolder() const;

    QVariantList m_videos;
    QString m_selectedPath;
};
}
