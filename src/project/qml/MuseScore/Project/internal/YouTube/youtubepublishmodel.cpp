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
#include "youtubepublishmodel.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QUrl>

#include "translation.h"

#include "project/internal/projectinfo.h"
#include "project/internal/projectlibrary.h"

#include "log.h"

using namespace mu::project;
using namespace muse;

namespace pl = mu::project::projectlibrary;
namespace pi = mu::project::projectinfo;

//! The YouTube Studio upload page. It is only opened in the user's own browser,
//! where they sign in to Google themselves.
static const char* YOUTUBE_STUDIO_UPLOAD_URL = "https://www.youtube.com/upload";

YouTubePublishModel::YouTubePublishModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QVariantList YouTubePublishModel::videos() const
{
    return m_videos;
}

QString YouTubePublishModel::selectedPath() const
{
    return m_selectedPath;
}

void YouTubePublishModel::setSelectedPath(const QString& path)
{
    if (m_selectedPath == path) {
        return;
    }
    m_selectedPath = path;
    emit selectedPathChanged();
}

bool YouTubePublishModel::isConnected() const
{
    return false;
}

QString YouTubePublishModel::scoreFolder() const
{
    INotationProjectPtr project = context() ? context()->currentProject() : nullptr;
    if (!project || project->isNewlyCreated()) {
        return QString();
    }
    return QFileInfo(project->path().toQString()).absolutePath();
}

QString YouTubePublishModel::projectFolder() const
{
    INotationProjectPtr project = context() ? context()->currentProject() : nullptr;
    if (!project || project->isNewlyCreated()) {
        return QString();
    }
    const std::string folder = pl::projectFolderForScore(QDir::toNativeSeparators(project->path().toQString()).toStdString());
    return QString::fromStdString(folder);
}

QString YouTubePublishModel::projectName() const
{
    INotationProjectPtr project = context() ? context()->currentProject() : nullptr;
    return project ? project->displayName() : QString();
}

void YouTubePublishModel::load()
{
    m_videos.clear();

    const QString folder = projectFolder();
    if (!folder.isEmpty()) {
        for (const pi::VideoFile& v : pi::videoExports(QDir::toNativeSeparators(folder).toStdString())) {
            const QString path = QDir::fromNativeSeparators(QString::fromStdString(v.path));
            const QDateTime time = QDateTime::fromSecsSinceEpoch(v.modifiedTime);
            QVariantMap item;
            item["path"] = path;
            item["name"] = QFileInfo(path).fileName();
            item["exported"] = QLocale().toString(time, QLocale::ShortFormat);
            m_videos << item;
        }
    }

    emit videosChanged();

    bool stillListed = false;
    for (const QVariant& v : m_videos) {
        if (v.toMap().value("path").toString() == m_selectedPath) {
            stillListed = true;
            break;
        }
    }
    if (!stillListed || m_selectedPath.isEmpty()) {
        setSelectedPath(m_videos.isEmpty() ? QString() : m_videos.first().toMap().value("path").toString());
    }
}

void YouTubePublishModel::chooseOtherFile()
{
    const std::vector<std::string> filter {
        muse::trc("project", "Videos") + " (*.mp4 *.mov *.m4v *.webm)"
    };

    QString dir = projectFolder();
    if (!dir.isEmpty()) {
        const QString videoDir = QDir(dir).filePath("Video Exports");
        if (QFileInfo(videoDir).isDir()) {
            dir = videoDir;
        }
    } else {
        dir = scoreFolder();
    }

    muse::io::path_t path = interactive()->selectOpeningFileSync(muse::trc("project", "Choose a video to publish"),
                                                                 muse::io::path_t(dir), filter);
    if (!path.empty()) {
        setSelectedPath(path.toQString());
    }
}

void YouTubePublishModel::revealSelected()
{
    if (m_selectedPath.isEmpty()) {
        return;
    }
    Ret ret = interactive()->revealInFileBrowser(muse::io::path_t(m_selectedPath));
    if (!ret) {
        LOGE() << ret.toString();
    }
}

void YouTubePublishModel::exportNewVideo()
{
    dispatcher()->dispatch("file-export");
}

void YouTubePublishModel::openYouTubeStudio()
{
    Ret ret = interactive()->openUrl(QUrl(QString::fromLatin1(YOUTUBE_STUDIO_UPLOAD_URL)));
    if (!ret) {
        LOGE() << ret.toString();
    }
    revealSelected();
}
