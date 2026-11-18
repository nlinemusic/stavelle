/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore). Help › Report a bug…: the window.
 */
#include "bugreportmodel.h"

#include <random>

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QQuickWindow>

#include "internal/jubelle/bugreport/bugreportdiagnostics.h"
#include "translation.h"

using namespace mu::appshell;
using namespace mu::appshell::jubelle::bugreport;

namespace {
std::string str(const QString& s)
{
    return s.toStdString();
}

QString qstr(const std::string& s)
{
    return QString::fromStdString(s);
}
}

BugReportModel::BugReportModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    std::random_device rd;
    m_createdAt = nowIsoUtc();
    m_id = makeReportId(m_createdAt, rd());
}

bool BugReportModel::scoreAvailable() const
{
    auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    return project && !project->isNewlyCreated() && QFileInfo::exists(project->path().toQString());
}

QStringList BugReportModel::screenshotNames() const
{
    QStringList names;
    for (const Attachment& a : m_screenshots) {
        names << qstr(a.name);
    }
    return names;
}

QString BugReportModel::problem() const
{
    return qstr(validate(input()));
}

QString BugReportModel::savedPath() const
{
    return m_savedPath;
}

BugReportInput BugReportModel::input() const
{
    BugReportInput in;
    in.title = str(m_title);
    in.description = str(m_description);
    in.steps = str(m_steps);
    in.expected = str(m_expected);
    in.actual = str(m_actual);
    in.comments = str(m_comments);
    in.screenshots = m_screenshots;
    in.includeLogs = m_includeLogs;
    in.includeScore = m_includeScore && scoreAvailable();
    if (in.includeScore) {
        // read only now, and only because you ticked the box: the last saved version
        const QString path = globalContext()->currentProject()->path().toQString();
        QFile f(path);
        if (f.open(QIODevice::ReadOnly) && f.size() <= qint64(MAX_SCORE_BYTES)) {
            const QByteArray data = f.readAll();
            in.score.name = str(QFileInfo(path).fileName());
            in.score.data.assign(data.begin(), data.end());
        }
    }
    return in;
}

BugReport BugReportModel::build()
{
    const std::string appData = globalConfiguration()->userAppDataPath().toStdString();
    return assemble(input(), gatherDiagnostics(appData), currentPrivacy(), m_createdAt, m_id);
}

QString BugReportModel::includedSummary() const
{
    QStringList lines;
    lines << muse::qtrc("appshell/bugreport", "• What you write above (passwords and keys are removed if you type any).");
    lines << muse::qtrc("appshell/bugreport", "• Jubelle’s version and build, your system’s name and version, the graphics system, "
                                              "screen sizes, processor cores and memory, and which notation engine is in use.");
    if (m_includeLogs) {
        lines << muse::qtrc("appshell/bugreport", "• The last lines of Jubelle’s log and the last crash, if there was one. "
                                                  "Your user name, folders, file names and score titles, e-mail addresses, "
                                                  "passwords, keys and tokens are removed first.");
    }
    if (!m_screenshots.empty()) {
        lines << muse::qtrc("appshell/bugreport", "• %n screenshot(s). Pictures are added as they are: check that they show nothing private.",
                            nullptr, int(m_screenshots.size()));
    }
    if (m_includeScore && scoreAvailable()) {
        lines << muse::qtrc("appshell/bugreport", "• Your open score (the last saved version), because you ticked the box. "
                                                  "Anyone who receives the report can open it.");
    } else {
        lines << muse::qtrc("appshell/bugreport", "• Not your score: it is only added if you tick the box.");
    }
    lines << muse::qtrc("appshell/bugreport", "Nothing is sent. Jubelle saves a ZIP file on this computer; you decide where to send it.");
    return lines.join("\n");
}

QString BugReportModel::preview()
{
    return qstr(toMarkdown(build()));
}

void BugReportModel::addScreenshotFromFile()
{
    if (m_screenshots.size() >= MAX_SCREENSHOTS) {
        return;
    }
    const muse::io::path_t path = interactive()->selectOpeningFileSync(
        muse::trc("appshell/bugreport", "Add a screenshot"), muse::io::path_t(),
        { muse::trc("appshell/bugreport", "Pictures") + " (*.png *.jpg *.jpeg)" });
    if (path.empty()) {
        return;
    }
    QFile f(path.toQString());
    if (!f.open(QIODevice::ReadOnly) || f.size() > qint64(MAX_SCREENSHOT_BYTES)) {
        interactive()->warning(muse::trc("appshell/bugreport", "This picture can’t be added"),
                               muse::trc("appshell/bugreport", "Choose a PNG or JPEG picture of at most 10 MB."));
        return;
    }
    const QByteArray data = f.readAll();
    Attachment a;
    a.name = str(QFileInfo(path.toQString()).fileName());
    a.data.assign(data.begin(), data.end());
    std::string why;
    if (!isAcceptedScreenshot(a.data, &why)) {
        interactive()->warning(muse::trc("appshell/bugreport", "This picture can’t be added"), why);
        return;
    }
    m_screenshots.push_back(std::move(a));
    emit screenshotsChanged();
    emit changed();
}

void BugReportModel::addScreenshotOfJubelle()
{
    if (m_screenshots.size() >= MAX_SCREENSHOTS || !mainWindow()) {
        return;
    }
    // only Jubelle's own main window is captured, never the rest of the screen
    auto* window = qobject_cast<QQuickWindow*>(mainWindow()->qWindow());
    if (!window) {
        return;
    }
    const QImage image = window->grabWindow();
    if (image.isNull()) {
        return;
    }
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    image.save(&buf, "PNG");
    Attachment a;
    a.name = "jubelle-window-" + std::to_string(m_screenshots.size() + 1) + ".png";
    a.data.assign(png.begin(), png.end());
    if (!isAcceptedScreenshot(a.data)) {
        return;
    }
    m_screenshots.push_back(std::move(a));
    emit screenshotsChanged();
    emit changed();
}

void BugReportModel::removeScreenshot(int index)
{
    if (index < 0 || size_t(index) >= m_screenshots.size()) {
        return;
    }
    m_screenshots.erase(m_screenshots.begin() + index);
    emit screenshotsChanged();
    emit changed();
}

bool BugReportModel::createReport()
{
    if (!problem().isEmpty()) {
        return false;
    }
    const std::string appData = globalConfiguration()->userAppDataPath().toStdString();
    SaveToFolderSubmitter submitter(reportsFolder(appData));
    const SubmitResult res = submitter.submit(build());
    if (!res.ok) {
        interactive()->error(muse::trc("appshell/bugreport", "The report could not be saved"), res.error);
        return false;
    }
    m_savedPath = qstr(res.location);
    emit savedPathChanged();
    return true;
}

void BugReportModel::showReportInFolder() const
{
    if (!m_savedPath.isEmpty()) {
        interactive()->revealInFileBrowser(muse::io::path_t(m_savedPath));
    }
}
