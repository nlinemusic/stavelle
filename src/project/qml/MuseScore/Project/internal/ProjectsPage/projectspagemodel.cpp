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
#include "projectspagemodel.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>
#include <QUrl>

#include <set>

#include "actions/actiontypes.h"
#include "translation.h"

#include "project/internal/projectinfo.h"
#include "project/internal/projectlibrary.h"
#include "project/internal/musicxmlsummary.h"

#include "global/io/file.h"
#include "global/serialization/zipreader.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/spanner.h"
#include "engraving/dom/tuplet.h"
#include "notation/imasternotation.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/text.h"

#include "log.h"

using namespace mu::project;
using namespace muse;
using namespace muse::actions;

namespace pl = mu::project::projectlibrary;
namespace pi = mu::project::projectinfo;

static constexpr size_t RECENT_LIMIT = 10;
static constexpr int CARD_IMAGE_MAX_SIDE = 1200;

static std::string toStd(const QString& s)
{
    return QDir::toNativeSeparators(s).toStdString();
}

static QString fromStd(const std::string& s)
{
    return QDir::fromNativeSeparators(QString::fromStdString(s));
}

static QString folderName(const std::string& folder)
{
    return QFileInfo(fromStd(folder)).fileName();
}

static QString savedAgo(int64_t seconds)
{
    const QDateTime saved = QDateTime::fromSecsSinceEpoch(seconds);
    const qint64 secs = saved.secsTo(QDateTime::currentDateTime());
    if (secs < 60) {
        return muse::qtrc("project", "just now");
    }
    if (secs < 3600) {
        return muse::qtrc("project", "%1 min ago").arg(secs / 60);
    }
    if (secs < 24 * 3600) {
        return muse::qtrc("project", "%1 h ago").arg(secs / 3600);
    }
    const qint64 days = saved.date().daysTo(QDate::currentDate());
    if (days <= 1) {
        return muse::qtrc("project", "yesterday");
    }
    if (days < 7) {
        return muse::qtrc("project", "%1 days ago").arg(days);
    }
    return QLocale().toString(saved.date(), QLocale::ShortFormat);
}

static int64_t fileSavedTime(const std::string& scorePath)
{
    const QFileInfo fi(fromStd(scorePath));
    return fi.exists() ? fi.lastModified().toSecsSinceEpoch() : 0;
}

ProjectsPageModel::ProjectsPageModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void ProjectsPageModel::load()
{
    // Saving (including Save As) updates MuseScore's recent-files list; that is the
    // moment Recent must refresh. Opening the Home page also refreshes it.
    if (recentFilesController()) {
        recentFilesController()->recentFilesListChanged().onReceive(this, [this](const RecentFilesChange&) {
            updateRecent();
        });
    }

    if (context()) {
        context()->currentProjectChanged().onNotify(this, [this]() {
            reload();
        });
    }

    configuration()->userProjectsPathChanged().onReceive(this, [this](const muse::io::path_t&) {
        reload();
    });

    reload();
}

void ProjectsPageModel::reload()
{
    updateCategories();
    updateRecent();
    updateTags();
    updateProjects();
}

std::string ProjectsPageModel::mainFolderStd() const
{
    return toStd(configuration()->userProjectsPath().toQString());
}

QString ProjectsPageModel::mainFolder() const
{
    return configuration()->userProjectsPath().toQString();
}

QVariantMap ProjectsPageModel::projectItem(const std::string& projectFolder, int64_t savedTime) const
{
    const pi::ProjectInfo info = pi::read(projectFolder);
    const QString name = folderName(projectFolder);
    const std::string imagePath = pi::cardImagePath(projectFolder);

    QStringList tags;
    for (const std::string& t : info.tags) {
        tags << QString::fromStdString(t);
    }

    const std::string home = pi::homeCategory(projectFolder);
    QStringList otherCategories;
    for (const std::string& c : pi::categoriesOf(projectFolder, mainFolderStd())) {
        const QString cat = QString::fromStdString(c);
        if (cat != m_currentCategory) {
            otherCategories << cat;
        }
    }

    QVariantMap item;
    item["name"] = name;
    item["folder"] = fromStd(projectFolder);
    item["scorePath"] = fromStd(pl::mainScorePath(projectFolder));
    item["hasScore"] = QFileInfo::exists(fromStd(pl::mainScorePath(projectFolder)));
    item["saved"] = savedTime > 0 ? savedAgo(savedTime) : QString();
    item["initial"] = name.isEmpty() ? QString() : name.left(1).toUpper();
    item["tags"] = tags;
    item["homeCategory"] = QString::fromStdString(home);
    item["otherCategories"] = otherCategories.join(", ");
    item["hasImage"] = !imagePath.empty();
    item["imageUrl"] = imagePath.empty() ? QString() : imageUrl(fromStd(imagePath));
    item["imageX"] = info.cardImage ? info.cardImage->x : 50.0;
    item["imageY"] = info.cardImage ? info.cardImage->y : 50.0;
    return item;
}

void ProjectsPageModel::updateRecent()
{
    QVariantList list;
    for (const pi::RecentScore& r : pi::recentScores(mainFolderStd(), RECENT_LIMIT)) {
        list << projectItem(r.projectFolder, r.savedTime);
    }
    m_recent = list;
    emit recentScoresChanged();
}

void ProjectsPageModel::updateCategories()
{
    const std::string main = mainFolderStd();
    pl::ensureMainFolder(main);

    QVariantList list;
    QStringList names;
    for (const std::string& c : pl::categories(main)) {
        QVariantMap item;
        item["name"] = QString::fromStdString(c);
        item["count"] = static_cast<int>(pi::projectsIn(main, c).size());
        list << item;
        names << QString::fromStdString(c);
    }
    m_categories = list;

    if (!names.contains(m_currentCategory)) {
        m_currentCategory = names.value(0);
        m_activeTags.clear();
        emit currentCategoryChanged();
    }

    emit categoriesChanged();
}

void ProjectsPageModel::updateProjects()
{
    const std::string main = mainFolderStd();
    const std::vector<std::string> inCategory = m_currentCategory.isEmpty()
                                                ? std::vector<std::string>()
                                                : pi::projectsIn(main, m_currentCategory.toStdString());

    std::vector<std::string> selected;
    for (const QString& t : m_activeTags) {
        selected.push_back(t.toStdString());
    }

    QVariantList list;
    for (const std::string& folder : inCategory) {
        const pi::ProjectInfo info = pi::read(folder);
        if (!pi::matchesTags(info.tags, selected, m_matchAll ? pi::TagMatch::All : pi::TagMatch::Any)) {
            continue;
        }
        if (!m_searchText.isEmpty() && !folderName(folder).contains(m_searchText, Qt::CaseInsensitive)) {
            continue;
        }
        list << projectItem(folder, fileSavedTime(pl::mainScorePath(folder)));
    }

    m_projectsInCategoryCount = static_cast<int>(inCategory.size());
    m_projects = list;
    emit projectsChanged();
}

void ProjectsPageModel::updateTags()
{
    QStringList tags;
    for (const std::string& t : pi::tagLibrary(mainFolderStd())) {
        tags << QString::fromStdString(t);
    }
    // Keep active filters visible even if no project uses them any more.
    for (const QString& t : m_activeTags) {
        if (!tags.contains(t, Qt::CaseInsensitive)) {
            tags << t;
        }
    }
    m_availableTags = tags;
    emit tagsChanged();
}

QVariantList ProjectsPageModel::recentScores() const
{
    return m_recent;
}

QVariantList ProjectsPageModel::categories() const
{
    return m_categories;
}

QString ProjectsPageModel::currentCategory() const
{
    return m_currentCategory;
}

void ProjectsPageModel::setCurrentCategory(const QString& category)
{
    if (m_currentCategory == category) {
        return;
    }
    m_currentCategory = category;
    m_activeTags.clear(); // switching category clears the selected tags; the match mode stays
    emit currentCategoryChanged();
    updateTags();
    updateProjects();
}

QVariantList ProjectsPageModel::projects() const
{
    return m_projects;
}

int ProjectsPageModel::projectsInCategoryCount() const
{
    return m_projectsInCategoryCount;
}

QStringList ProjectsPageModel::availableTags() const
{
    return m_availableTags;
}

QStringList ProjectsPageModel::activeTags() const
{
    return m_activeTags;
}

bool ProjectsPageModel::matchAllTags() const
{
    return m_matchAll;
}

void ProjectsPageModel::setMatchAllTags(bool all)
{
    if (m_matchAll == all) {
        return;
    }
    m_matchAll = all;
    emit tagsChanged();
    updateProjects();
}

QString ProjectsPageModel::searchText() const
{
    return m_searchText;
}

void ProjectsPageModel::setSearchText(const QString& text)
{
    if (m_searchText == text) {
        return;
    }
    m_searchText = text;
    updateProjects();
}

// ---------------------------------------------------------------------------
// Tags
// ---------------------------------------------------------------------------

void ProjectsPageModel::toggleTag(const QString& tag)
{
    if (m_activeTags.contains(tag)) {
        m_activeTags.removeAll(tag);
    } else {
        m_activeTags << tag;
    }
    emit tagsChanged();
    updateProjects();
}

void ProjectsPageModel::removeActiveTag(const QString& tag)
{
    if (m_activeTags.removeAll(tag) > 0) {
        emit tagsChanged();
        updateProjects();
    }
}

void ProjectsPageModel::clearTags()
{
    if (m_activeTags.isEmpty()) {
        return;
    }
    m_activeTags.clear();
    emit tagsChanged();
    updateProjects();
}

bool ProjectsPageModel::addTag(const QString& tag)
{
    pl::Result r = pi::addTagToLibrary(mainFolderStd(), tag.trimmed().toStdString());
    if (!r.ok) {
        showError(muse::qtrc("project", "The tag could not be added"), r.error);
        return false;
    }
    updateTags();
    return true;
}

// ---------------------------------------------------------------------------
// Categories
// ---------------------------------------------------------------------------

void ProjectsPageModel::showError(const QString& title, const std::string& message)
{
    interactive()->error(title.toStdString(), message);
}

bool ProjectsPageModel::createCategory(const QString& name)
{
    pl::Result r = pl::createCategory(mainFolderStd(), name.trimmed().toStdString());
    if (!r.ok) {
        showError(muse::qtrc("project", "The category could not be created"), r.error);
        return false;
    }
    updateCategories();
    return true;
}

bool ProjectsPageModel::renameCategory(const QString& oldName, const QString& newName)
{
    // A project stored in this category whose score is open would have its path changed.
    const QDir categoryDir(QDir(mainFolder()).filePath(oldName));
    for (const std::string& name : pl::projects(mainFolderStd(), oldName.toStdString())) {
        if (isProjectOpen(categoryDir.filePath(QString::fromStdString(name)))) {
            interactive()->info(muse::trc("project", "Close the score first"),
                                muse::trc("project", "A score in this category is open. Save and close it, then rename the category."));
            return false;
        }
    }

    pl::Result r = pi::renameCategory(mainFolderStd(), oldName.toStdString(), newName.trimmed().toStdString());
    if (!r.ok) {
        showError(muse::qtrc("project", "The category could not be renamed"), r.error);
        return false;
    }
    if (!r.error.empty()) {
        showError(muse::qtrc("project", "The category was renamed"), r.error);
    }
    if (m_currentCategory == oldName) {
        m_currentCategory = newName.trimmed();
        emit currentCategoryChanged();
    }
    reload();
    return true;
}

bool ProjectsPageModel::deleteCategory(const QString& name)
{
    if (pi::isCategoryInUse(mainFolderStd(), name.toStdString())) {
        interactive()->info(muse::qtrc("project", "“%1” isn’t empty").arg(name).toStdString(),
                            muse::trc("project", "It still contains or lists scores. Move them to another category first, "
                                                 "then delete it. Jubelle never deletes your projects."));
        return false;
    }

    pl::Result r = pi::deleteCategory(mainFolderStd(), name.toStdString());
    if (!r.ok) {
        showError(muse::qtrc("project", "The category could not be deleted"), r.error);
        return false;
    }
    updateCategories();
    updateProjects();
    return true;
}

void ProjectsPageModel::moveCategory(const QString& name, int delta)
{
    QStringList names;
    for (const QVariant& v : m_categories) {
        names << v.toMap().value("name").toString();
    }
    const int from = names.indexOf(name);
    const int to = from + delta;
    if (from < 0 || to < 0 || to >= names.size()) {
        return;
    }
    names.move(from, to);
    setCategoryOrder(names);
}

void ProjectsPageModel::setCategoryOrder(const QStringList& names)
{
    std::vector<std::string> order;
    for (const QString& n : names) {
        order.push_back(n.toStdString());
    }
    pl::setCategoryOrder(mainFolderStd(), order);
    updateCategories();
}

void ProjectsPageModel::revealCategory(const QString& name)
{
    revealInFileBrowser(QDir(mainFolder()).filePath(name));
}

// ---------------------------------------------------------------------------
// Projects
// ---------------------------------------------------------------------------

void ProjectsPageModel::createNewScore()
{
    dispatcher()->dispatch("file-new");
}

void ProjectsPageModel::createScoreFromTemplate(const QString& templatePath)
{
    dispatcher()->dispatch("file-new", muse::actions::ActionData::make_arg1<QString>(templatePath));
}

void ProjectsPageModel::openOther()
{
    dispatcher()->dispatch("file-open");
}

void ProjectsPageModel::openScore(const QString& scorePath)
{
    if (!QFileInfo::exists(scorePath)) {
        interactive()->info(muse::trc("project", "This project has no score yet"),
                            muse::trc("project", "The score file was not found in the project folder."));
        return;
    }
    dispatcher()->dispatch("file-open", ActionData::make_arg2<QUrl, QString>(QUrl::fromLocalFile(scorePath), QString()));
}

void ProjectsPageModel::revealInFileBrowser(const QString& path)
{
    Ret ret = interactive()->revealInFileBrowser(muse::io::path_t(path));
    if (!ret) {
        LOGE() << ret.toString();
    }
}

void ProjectsPageModel::openProjectsFolder()
{
    pl::ensureMainFolder(mainFolderStd());
    interactive()->openUrl(QUrl::fromLocalFile(mainFolder()));
}

bool ProjectsPageModel::isProjectOpen(const QString& projectFolder) const
{
    if (!context()) {
        return false;
    }
    INotationProjectPtr project = context()->currentProject();
    if (!project) {
        return false;
    }
    const QString openPath = QFileInfo(project->path().toQString()).absoluteFilePath();
    const QString folder = QFileInfo(projectFolder).absoluteFilePath();
    return openPath.startsWith(folder + "/", Qt::CaseInsensitive);
}

bool ProjectsPageModel::checkCanChangeProject(const QString& projectFolder)
{
    if (!isProjectOpen(projectFolder)) {
        return true;
    }
    interactive()->info(muse::trc("project", "Close the score first"),
                        muse::trc("project", "This score is open. Save and close it, then rename or move the project. "
                                             "This keeps the open score and its backups safe."));
    return false;
}

QString ProjectsPageModel::validateName(const QString& name) const
{
    return QString::fromStdString(pl::validateName(name.trimmed().toStdString()));
}

void ProjectsPageModel::updateRecentFileAfterMove(const QString& oldScorePath, const QString& newScorePath)
{
    if (!recentFilesController()) {
        return;
    }
    for (const RecentFile& f : recentFilesController()->recentFilesList()) {
        if (QFileInfo(f.path.toQString()).absoluteFilePath().compare(QFileInfo(oldScorePath).absoluteFilePath(),
                                                                     Qt::CaseInsensitive) == 0) {
            recentFilesController()->moveRecentFile(f.path, RecentFile(muse::io::path_t(newScorePath)));
            break;
        }
    }
}

void ProjectsPageModel::applyPrintedTitle(const QString& scorePath, const QString& title)
{
    // The score is opened normally; the title is changed as an ordinary, undoable
    // edit and left for the user to save. Nothing is written to the file here.
    openScore(scorePath);

    INotationProjectPtr project = context() ? context()->currentProject() : nullptr;
    if (!project || QFileInfo(project->path().toQString()).absoluteFilePath()
        .compare(QFileInfo(scorePath).absoluteFilePath(), Qt::CaseInsensitive) != 0) {
        LOGW() << "Renamed score is not the open project; the printed title was not changed";
        return;
    }

    notation::IMasterNotationPtr master = project->masterNotation();
    if (!master || !master->masterScore()) {
        return;
    }

    mu::engraving::MasterScore* score = master->masterScore();
    mu::engraving::Text* titleText = score->getText(mu::engraving::TextStyleType::TITLE);

    ProjectMeta meta = project->metaInfo();
    meta.title = title;

    master->notation()->undoStack()->prepareChanges(TranslatableString("undoableAction", "Change title"));
    if (titleText) {
        titleText->undoChangeProperty(mu::engraving::Pid::TEXT,
                                      mu::engraving::PropertyValue(mu::engraving::TextBase::plainToXmlText(muse::String::fromQString(title))));
    }
    master->notation()->undoStack()->commitChanges();
    project->setMetaInfo(meta, true);
    master->notation()->notationChanged().notify();
}

bool ProjectsPageModel::renameProject(const QString& projectFolder, const QString& newName, bool changePrintedTitle)
{
    if (!checkCanChangeProject(projectFolder)) {
        return false;
    }

    const QString oldScore = fromStd(pl::mainScorePath(toStd(projectFolder)));
    pl::Result r = pl::renameProject(toStd(projectFolder), newName.trimmed().toStdString());
    if (!r.ok) {
        showError(muse::qtrc("project", "The project could not be renamed"), r.error);
        return false;
    }

    const QString newScore = fromStd(pl::mainScorePath(r.path));
    updateRecentFileAfterMove(oldScore, newScore);
    reload();

    if (changePrintedTitle && QFileInfo::exists(newScore)) {
        applyPrintedTitle(newScore, newName.trimmed());
    }
    return true;
}

bool ProjectsPageModel::moveProject(const QString& projectFolder, const QString& category)
{
    if (!checkCanChangeProject(projectFolder)) {
        return false;
    }

    const QString oldScore = fromStd(pl::mainScorePath(toStd(projectFolder)));
    pl::Result r = pi::moveToCategory(toStd(projectFolder), mainFolderStd(), category.toStdString());
    if (!r.ok) {
        showError(muse::qtrc("project", "The project could not be moved"), r.error);
        return false;
    }
    if (!r.error.empty()) {
        showError(muse::qtrc("project", "The project was moved"), r.error);
    }

    updateRecentFileAfterMove(oldScore, fromStd(pl::mainScorePath(r.path)));
    reload();
    return true;
}

QStringList ProjectsPageModel::projectCategories(const QString& projectFolder) const
{
    QStringList list;
    for (const std::string& c : pi::categoriesOf(toStd(projectFolder), mainFolderStd())) {
        list << QString::fromStdString(c);
    }
    return list;
}

QString ProjectsPageModel::homeCategory(const QString& projectFolder) const
{
    return QString::fromStdString(pi::homeCategory(toStd(projectFolder)));
}

bool ProjectsPageModel::setProjectCategories(const QString& projectFolder, const QStringList& categories)
{
    std::vector<std::string> list;
    for (const QString& c : categories) {
        list.push_back(c.toStdString());
    }

    const bool homeKept = categories.contains(homeCategory(projectFolder));
    if (!homeKept && !checkCanChangeProject(projectFolder)) {
        return false;
    }

    const QString oldScore = fromStd(pl::mainScorePath(toStd(projectFolder)));
    pl::Result r = pi::setCategories(toStd(projectFolder), mainFolderStd(), list);
    if (!r.ok) {
        showError(muse::qtrc("project", "The categories could not be saved"), r.error);
        return false;
    }
    if (!homeKept) {
        updateRecentFileAfterMove(oldScore, fromStd(pl::mainScorePath(r.path)));
    }
    reload();
    return true;
}

QStringList ProjectsPageModel::projectTags(const QString& projectFolder) const
{
    QStringList list;
    for (const std::string& t : pi::read(toStd(projectFolder)).tags) {
        list << QString::fromStdString(t);
    }
    return list;
}

bool ProjectsPageModel::setProjectTags(const QString& projectFolder, const QStringList& tags)
{
    std::vector<std::string> list;
    for (const QString& t : tags) {
        list.push_back(t.toStdString());
    }
    pl::Result r = pi::setTags(toStd(projectFolder), mainFolderStd(), list);
    if (!r.ok) {
        showError(muse::qtrc("project", "The tags could not be saved"), r.error);
        return false;
    }
    updateTags();
    updateProjects();
    updateRecent();
    return true;
}

// ---------------------------------------------------------------------------
// Card image
// ---------------------------------------------------------------------------

QString ProjectsPageModel::chooseImageFile(const QString& projectFolder)
{
    const std::vector<std::string> filter {
        muse::trc("project", "Images") + " (*.jpg *.jpeg *.png *.webp)"
    };
    // P4: start in the project's Pictures folder when it has one
    const std::string pictures = projectFolder.isEmpty() ? std::string() : pl::picturesFolder(toStd(projectFolder), false);
    muse::io::path_t path = interactive()->selectOpeningFileSync(muse::trc("project", "Choose a card image"),
                                                                 muse::io::path_t(pictures), filter);
    return path.toQString();
}

QString ProjectsPageModel::imageUrl(const QString& filePath) const
{
    if (filePath.isEmpty()) {
        return QString();
    }
    QUrl url = QUrl::fromLocalFile(filePath);
    // The modification time makes a replaced image show immediately.
    url.setQuery(QString("v=%1").arg(QFileInfo(filePath).lastModified().toMSecsSinceEpoch()));
    return url.toString();
}

QVariantMap ProjectsPageModel::cardImage(const QString& projectFolder) const
{
    const pi::ProjectInfo info = pi::read(toStd(projectFolder));
    const std::string path = pi::cardImagePath(toStd(projectFolder));

    QVariantMap map;
    map["hasImage"] = !path.empty();
    map["url"] = path.empty() ? QString() : imageUrl(fromStd(path));
    map["x"] = info.cardImage ? info.cardImage->x : 50.0;
    map["y"] = info.cardImage ? info.cardImage->y : 50.0;
    return map;
}

bool ProjectsPageModel::setCardImage(const QString& projectFolder, const QString& sourcePath, double x, double y)
{
    QImageReader reader(sourcePath);
    reader.setAutoTransform(true); // respect camera orientation
    QImage image = reader.read();
    if (image.isNull()) {
        showError(muse::qtrc("project", "The image could not be opened"), reader.errorString().toStdString());
        return false;
    }

    // Keep a reasonably small copy for the card; the proportions are never changed.
    if (image.width() > CARD_IMAGE_MAX_SIDE || image.height() > CARD_IMAGE_MAX_SIDE) {
        image = image.scaled(CARD_IMAGE_MAX_SIDE, CARD_IMAGE_MAX_SIDE, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        showError(muse::qtrc("project", "The image could not be prepared"), std::string());
        return false;
    }
    const bool hasAlpha = image.hasAlphaChannel();
    const QString prepared = tmpDir.filePath(hasAlpha ? "card.png" : "card.jpg");
    if (!image.save(prepared, hasAlpha ? "PNG" : "JPG", 90)) {
        showError(muse::qtrc("project", "The image could not be prepared"), std::string());
        return false;
    }

    pl::Result r = pi::setCardImage(toStd(projectFolder), toStd(prepared), x, y);
    if (!r.ok) {
        showError(muse::qtrc("project", "The card image could not be saved"), r.error);
        return false;
    }
    updateProjects();
    updateRecent();
    return true;
}

bool ProjectsPageModel::setCardImagePosition(const QString& projectFolder, double x, double y)
{
    pl::Result r = pi::setCardImagePosition(toStd(projectFolder), x, y);
    if (!r.ok) {
        showError(muse::qtrc("project", "The card image position could not be saved"), r.error);
        return false;
    }
    updateProjects();
    updateRecent();
    return true;
}

bool ProjectsPageModel::removeCardImage(const QString& projectFolder)
{
    pl::Result r = pi::removeCardImage(toStd(projectFolder));
    if (!r.ok) {
        showError(muse::qtrc("project", "The card image could not be removed"), r.error);
        return false;
    }
    updateProjects();
    updateRecent();
    return true;
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

static const muse::Uri NAME_DIALOG_URI("musescore://project/jubelle/name");
static const muse::Uri MOVE_DIALOG_URI("musescore://project/jubelle/move");
static const muse::Uri CATEGORIES_TAGS_DIALOG_URI("musescore://project/jubelle/categoriestags");
static const muse::Uri CARD_IMAGE_DIALOG_URI("musescore://project/jubelle/cardimage");
static const muse::Uri ADD_DIALOG_URI("musescore://project/jubelle/addtoprojects");

static Val toValList(const QStringList& list)
{
    ValList vals;
    for (const QString& s : list) {
        vals.push_back(Val(s));
    }
    return Val(vals);
}

static QStringList toStringList(const QVariant& v)
{
    QStringList out;
    for (const QVariant& item : v.toList()) {
        out << item.toString();
    }
    return out;
}

QStringList ProjectsPageModel::categoryNames() const
{
    QStringList names;
    for (const std::string& c : pl::categories(mainFolderStd())) {
        names << QString::fromStdString(c);
    }
    return names;
}

void ProjectsPageModel::renameProjectInteractive(const QString& projectFolder)
{
    if (!checkCanChangeProject(projectFolder)) {
        return;
    }

    const QString name = QFileInfo(projectFolder).fileName();
    UriQuery q(NAME_DIALOG_URI);
    q.addParam("heading", Val(muse::qtrc("project", "Rename project")));
    q.addParam("fieldLabel", Val(muse::qtrc("project", "New name")));
    q.addParam("initialName", Val(name));
    q.addParam("note", Val(muse::qtrc("project", "This renames the project folder, its score file and its backup. "
                                                 "Files you have already exported keep their names.")));
    q.addParam("acceptText", Val(muse::qtrc("project", "Rename")));
    q.addParam("showPrintedTitleOption", Val(true));

    interactive()->open(q).onResolve(this, [this, projectFolder, name](const Val& val) {
        const QVariantMap map = val.toQVariant().toMap();
        const QString newName = map.value("name").toString().trimmed();
        if (newName.isEmpty() || newName == name) {
            return;
        }
        renameProject(projectFolder, newName, map.value("changeTitle").toBool());
    });
}

void ProjectsPageModel::moveProjectInteractive(const QString& projectFolder)
{
    if (!checkCanChangeProject(projectFolder)) {
        return;
    }

    UriQuery q(MOVE_DIALOG_URI);
    q.addParam("projectName", Val(QFileInfo(projectFolder).fileName()));
    q.addParam("categories", toValList(categoryNames()));
    q.addParam("currentCategory", Val(homeCategory(projectFolder)));

    interactive()->open(q).onResolve(this, [this, projectFolder](const Val& val) {
        const QString category = val.toQVariant().toMap().value("category").toString();
        if (!category.isEmpty()) {
            moveProject(projectFolder, category);
        }
    });
}

void ProjectsPageModel::editCategoriesAndTags(const QString& projectFolder)
{
    UriQuery q(CATEGORIES_TAGS_DIALOG_URI);
    q.addParam("projectName", Val(QFileInfo(projectFolder).fileName()));
    q.addParam("categories", toValList(categoryNames()));
    q.addParam("checkedCategories", toValList(projectCategories(projectFolder)));
    q.addParam("homeCategory", Val(homeCategory(projectFolder)));
    q.addParam("tags", toValList(projectTags(projectFolder)));
    q.addParam("allTags", toValList(m_availableTags));

    interactive()->open(q).onResolve(this, [this, projectFolder](const Val& val) {
        const QVariantMap map = val.toQVariant().toMap();
        const QStringList categories = toStringList(map.value("categories"));
        const QStringList tags = toStringList(map.value("tags"));

        // Tags first: they are stored inside the project folder, which may then move.
        if (!setProjectTags(projectFolder, tags)) {
            return;
        }
        if (!categories.isEmpty() && categories != projectCategories(projectFolder)) {
            setProjectCategories(projectFolder, categories);
        }
    });
}

void ProjectsPageModel::openCardImageDialog(const QString& projectFolder, const QString& imageFile, double x, double y,
                                            bool isNewImage)
{
    UriQuery q(CARD_IMAGE_DIALOG_URI);
    q.addParam("projectName", Val(QFileInfo(projectFolder).fileName()));
    q.addParam("imageUrl", Val(imageUrl(imageFile)));
    q.addParam("posX", Val(x));
    q.addParam("posY", Val(y));
    q.addParam("isNewImage", Val(isNewImage));

    interactive()->open(q).onResolve(this, [this, projectFolder, imageFile, isNewImage](const Val& val) {
        const QVariantMap map = val.toQVariant().toMap();
        const QString action = map.value("action").toString();
        const double nx = map.value("x").toDouble();
        const double ny = map.value("y").toDouble();

        if (action == "choose") {
            const QString chosen = chooseImageFile(projectFolder);
            if (chosen.isEmpty()) {
                // Cancelled: reopen with what was shown before, unchanged.
                openCardImageDialog(projectFolder, imageFile, nx, ny, isNewImage);
                return;
            }
            openCardImageDialog(projectFolder, chosen, 50, 50, true); // a new picture starts centred
        } else if (action == "remove") {
            removeCardImage(projectFolder);
        } else if (action == "save") {
            if (isNewImage) {
                setCardImage(projectFolder, imageFile, nx, ny);
            } else {
                setCardImagePosition(projectFolder, nx, ny);
            }
        }
    });
}

void ProjectsPageModel::editCardImage(const QString& projectFolder)
{
    const std::string current = pi::cardImagePath(toStd(projectFolder));
    if (!current.empty()) {
        const pi::ProjectInfo info = pi::read(toStd(projectFolder));
        openCardImageDialog(projectFolder, fromStd(current), info.cardImage->x, info.cardImage->y, false);
        return;
    }

    const QString chosen = chooseImageFile(projectFolder);
    if (!chosen.isEmpty()) {
        openCardImageDialog(projectFolder, chosen, 50, 50, true);
    }
}

void ProjectsPageModel::openPicturesFolder(const QString& projectFolder)
{
    // P4: made the first time you open it
    const std::string dir = pl::picturesFolder(toStd(projectFolder), true);
    if (dir.empty()) {
        interactive()->error(muse::trc("project", "Couldn’t open the Pictures folder"),
                             muse::trc("project", "The project’s Pictures folder couldn’t be made."));
        return;
    }
    interactive()->openUrl(QUrl::fromLocalFile(fromStd(dir)));
}

void ProjectsPageModel::newCategoryInteractive()
{
    UriQuery q(NAME_DIALOG_URI);
    q.addParam("heading", Val(muse::qtrc("project", "New category")));
    q.addParam("fieldLabel", Val(muse::qtrc("project", "Name")));
    q.addParam("acceptText", Val(muse::qtrc("project", "Create")));

    interactive()->open(q).onResolve(this, [this](const Val& val) {
        const QString name = val.toQVariant().toMap().value("name").toString().trimmed();
        if (!name.isEmpty() && createCategory(name)) {
            setCurrentCategory(name);
        }
    });
}

void ProjectsPageModel::renameCategoryInteractive(const QString& oldName)
{
    UriQuery q(NAME_DIALOG_URI);
    q.addParam("heading", Val(muse::qtrc("project", "Rename category")));
    q.addParam("fieldLabel", Val(muse::qtrc("project", "New name")));
    q.addParam("initialName", Val(oldName));
    q.addParam("acceptText", Val(muse::qtrc("project", "Rename")));

    interactive()->open(q).onResolve(this, [this, oldName](const Val& val) {
        const QString name = val.toQVariant().toMap().value("name").toString().trimmed();
        if (!name.isEmpty() && name != oldName) {
            renameCategory(oldName, name);
        }
    });
}

void ProjectsPageModel::deleteCategoryInteractive(const QString& name)
{
    if (pi::isCategoryInUse(mainFolderStd(), name.toStdString())) {
        deleteCategory(name); // shows the "isn't empty" message
        return;
    }

    IInteractive::Result result = interactive()->questionSync(
        muse::qtrc("project", "Delete the category “%1”?").arg(name).toStdString(),
        muse::trc("project", "The category is empty, so no projects are affected."),
        { IInteractive::Button::Cancel, IInteractive::Button::Yes }, IInteractive::Button::Cancel);

    if (result.standardButton() == IInteractive::Button::Yes) {
        deleteCategory(name);
    }
}

namespace {
bool isMsczPath(const std::string& path)
{
    return QString::fromStdString(path).endsWith(".mscz", Qt::CaseInsensitive);
}

//! the MusicXML text of a .musicxml/.xml file, or of the score inside a compressed .mxl
std::string musicXmlText(const io::path_t& path)
{
    if (QString::fromStdString(path.toStdString()).endsWith(".mxl", Qt::CaseInsensitive)) {
        muse::ZipReader zip(path);
        if (!zip.exists() || zip.hasError()) {
            return {};
        }
        for (const muse::ZipReader::FileInfo& info : zip.fileInfoList()) {
            const QString name = QString::fromStdString(info.filePath.toStdString());
            if (info.isFile && !name.startsWith("META-INF", Qt::CaseInsensitive)
                && (name.endsWith(".xml", Qt::CaseInsensitive) || name.endsWith(".musicxml", Qt::CaseInsensitive))) {
                const muse::ByteArray data = zip.fileData(info.filePath.toStdString());
                return std::string(data.constChar(), data.size());
            }
        }
        return {};
    }
    muse::ByteArray data;
    if (!muse::io::File::readFile(path, data)) {
        return {};
    }
    return std::string(data.constChar(), data.size());
}

//! the same things musicxmlsummary counts in a file, counted in the converted score
mu::project::jubelle::MusicXmlCounts countInScore(mu::engraving::MasterScore* score)
{
    using namespace mu::engraving;
    mu::project::jubelle::MusicXmlCounts c;
    c.parts = int(score->parts().size());
    std::set<const Tuplet*> tuplets;
    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
            for (EngravingItem* e : seg->elist()) {
                if (!e || !e->isChordRest()) {
                    continue;
                }
                ChordRest* cr = toChordRest(e);
                for (const Tuplet* t = cr->tuplet(); t; t = t->tuplet()) {
                    tuplets.insert(t);
                }
                c.lyrics += int(cr->lyrics().size());
                if (cr->isChord()) {
                    Chord* chord = toChord(cr);
                    c.notes += int(chord->notes().size());
                    for (Chord* g : chord->graceNotes()) {
                        c.notes += int(g->notes().size());
                    }
                }
            }
            for (EngravingItem* a : seg->annotations()) {
                if (!a) {
                    continue;
                }
                if (a->isDynamic()) {
                    ++c.dynamics;
                } else if (a->isHarmony()) {
                    ++c.chordSymbols;
                } else if (a->isTempoText()) {
                    ++c.tempoMarks;
                } else if (a->isStaffText() || a->isExpression() || a->isSystemText() || a->isRehearsalMark()
                           || a->isPlayTechAnnotation()) {
                    ++c.texts;
                }
            }
        }
    }
    for (const auto& [tick, sp] : score->spanner()) {
        if (sp && sp->isSlur()) {
            ++c.slurs;
        } else if (sp && sp->isHairpin()) {
            ++c.hairpins;
        }
    }
    c.tuplets = int(tuplets.size());
    // a tempo written as words ("Allegro") arrives as a tempo mark: count either way
    c.texts += c.tempoMarks;
    return c;
}
}

// Jubelle (17 Nov 2026): MusicXML import, for scores from Noteflight and other
// programs. MuseScore's own MusicXML importer does the conversion (it already
// knows Noteflight's files); Jubelle makes the project, keeps the original
// file in it, remembers where it came from so it isn't imported twice, and
// reports anything that arrived short.
std::string ProjectsPageModel::importMusicXml(const std::string& mainFolder, const std::string& category, const std::string& name,
                                              const std::string& sourcePath, QString* error, QString* report)
{
    const io::path_t source(sourcePath);
    const mu::project::jubelle::MusicXmlSummary summary = mu::project::jubelle::summarizeMusicXml(musicXmlText(source));
    if (!summary.isMusicXml) {
        *error = muse::qtrc("project", "this isn’t a MusicXML file");
        return {};
    }
    INotationProjectPtr project = projectCreator() ? projectCreator()->newProject(iocContext()) : nullptr;
    if (!project) {
        *error = muse::qtrc("project", "the score could not be prepared");
        return {};
    }
    Ret ret = project->load(source);
    if (!ret) {
        *error = QString::fromStdString(ret.text().empty() ? std::string("the file could not be read") : ret.text());
        return {};
    }
    const pl::Result folder = pl::createProject(mainFolder, category, name);
    if (!folder.ok) {
        *error = QString::fromStdString(folder.error);
        return {};
    }
    const std::string target = pl::mainScorePath(folder.path);
    ret = project->save(io::path_t(target), SaveMode::SaveAs);
    if (!ret) {
        pl::removeUnusedProject(folder.path);
        *error = QString::fromStdString(ret.text().empty() ? std::string("the converted score could not be saved") : ret.text());
        return {};
    }
    pl::keepOriginal(folder.path, sourcePath);
    pl::recordImport(folder.path, sourcePath);

    mu::project::jubelle::MusicXmlCounts arrived;
    if (project->masterNotation() && project->masterNotation()->masterScore()) {
        arrived = countInScore(project->masterNotation()->masterScore());
    }
    const std::string text = mu::project::jubelle::importReport(summary, arrived);
    *report = QString::fromStdString(text);
    io::File::writeFile(io::path_t(folder.path + "/" + pl::IMPORTED_ORIGINALS_FOLDER + "/Import report.txt"),
                        muse::ByteArray::fromRawData(text.data(), text.size()));
    return target;
}

void ProjectsPageModel::importScoresInteractive()
{
    // Jubelle R1: copies MuseScore files (.mscz) into new projects; MusicXML files
    // (from Noteflight and other programs) are converted. The originals are
    // never moved or changed.
    const std::vector<std::string> filter = {
        muse::trc("project", "Scores") + " (*.mscz *.musicxml *.mxl *.xml)",
        muse::trc("project", "MuseScore files") + " (*.mscz)",
        muse::trc("project", "MusicXML files (Noteflight, Sibelius, Finale, Dorico…)") + " (*.musicxml *.mxl *.xml)",
    };
    const io::paths_t files = interactive()->selectOpeningFilesSync(muse::trc("project", "Import scores"),
                                                                    io::path_t(), filter);
    if (files.empty()) {
        return;
    }
    const std::string main = mainFolderStd();
    pl::ensureMainFolder(main);

    std::vector<std::string> toImport;
    QStringList alreadyThere;
    for (const io::path_t& f : files) {
        const std::string path = f.toStdString();
        if (pl::isInsideMainFolder(path, main) || !pl::findImportedProject(main, path).empty()) {
            alreadyThere << QString::fromStdString(io::filename(f).toStdString());
        } else {
            toImport.push_back(path);
        }
    }
    if (toImport.empty()) {
        interactive()->info(muse::trc("project", "Already in Jubelle Projects"),
                            muse::qtrc("project", "These scores are already in your Jubelle Projects: %1")
                            .arg(alreadyThere.join(", ")).toStdString());
        return;
    }

    UriQuery q(ADD_DIALOG_URI);
    const QString firstName = QString::fromStdString(io::completeBasename(io::path_t(toImport.front())).toStdString());
    q.addParam("initialName", Val(firstName));
    q.addParam("askName", Val(toImport.size() == 1));
    q.addParam("fileCount", Val(int(toImport.size())));
    q.addParam("categories", toValList(categoryNames()));

    interactive()->open(q).onResolve(this, [this, toImport, alreadyThere, main](const Val& val) {
        const QVariantMap map = val.toQVariant().toMap();
        const std::string category = map.value("category").toString().toStdString();
        if (category.empty()) {
            return;
        }
        QStringList failed;
        QStringList reports;
        std::string lastImported;
        for (const std::string& path : toImport) {
            std::string name = toImport.size() == 1 ? map.value("name").toString().toStdString()
                               : io::completeBasename(io::path_t(path)).toStdString();
            const QString fileName = QString::fromStdString(io::filename(io::path_t(path)).toStdString());
            if (isMsczPath(path)) {
                const pl::Result r = pl::importScore(main, category, name, path);
                if (!r.ok) {
                    failed << fileName + ": " + QString::fromStdString(r.error);
                } else {
                    lastImported = r.path;
                }
            } else {
                QString error, report;
                const std::string score = importMusicXml(main, category, name, path, &error, &report);
                if (score.empty()) {
                    failed << fileName + ": " + error;
                } else {
                    lastImported = score;
                    reports << fileName + "\n" + report;
                }
            }
        }
        reload();
        if (!reports.isEmpty()) {
            interactive()->info(muse::trc("project", "Import report"),
                                (reports.join("\n") + "\n"
                                 + muse::qtrc("project", "The original file and this report are kept in the project’s “Imported Originals” folder."))
                                .toStdString());
        }
        // one score imported: open it, ready for editing
        if (toImport.size() == 1 && !lastImported.empty() && failed.isEmpty()) {
            dispatcher()->dispatch("file-open", ActionData::make_arg2<QUrl, QString>(
                                       QUrl::fromLocalFile(QString::fromStdString(lastImported)), QString()));
        }
        if (!failed.isEmpty() || !alreadyThere.isEmpty()) {
            QString msg;
            if (!failed.isEmpty()) {
                msg += muse::qtrc("project", "Not imported: %1").arg(failed.join("; ")) + "\n";
            }
            if (!alreadyThere.isEmpty()) {
                msg += muse::qtrc("project", "Already in Jubelle Projects (skipped): %1").arg(alreadyThere.join(", "));
            }
            interactive()->info(muse::trc("project", "Import finished"), msg.toStdString());
        }
    });
}
