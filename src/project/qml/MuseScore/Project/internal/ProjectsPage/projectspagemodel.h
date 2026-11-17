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
#include <QStringList>
#include <QVariantList>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "iprojectconfiguration.h"
#include "irecentfilescontroller.h"
#include "iprojectcreator.h"
#include "actions/iactionsdispatcher.h"
#include "context/iglobalcontext.h"
#include "iinteractive.h"

namespace mu::project {
//! Jubelle: the Projects page on the Home screen (Recent, categories, tags, cards).
//! All project organisation goes through projectlibrary / projectinfo; the score
//! files themselves are only ever opened through MuseScore's normal "file-open".
class ProjectsPageModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList recentScores READ recentScores NOTIFY recentScoresChanged)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY categoriesChanged)
    Q_PROPERTY(QString currentCategory READ currentCategory WRITE setCurrentCategory NOTIFY currentCategoryChanged)
    Q_PROPERTY(QVariantList projects READ projects NOTIFY projectsChanged)
    Q_PROPERTY(int projectsInCategoryCount READ projectsInCategoryCount NOTIFY projectsChanged)
    Q_PROPERTY(QStringList availableTags READ availableTags NOTIFY tagsChanged)
    Q_PROPERTY(QStringList activeTags READ activeTags NOTIFY tagsChanged)
    Q_PROPERTY(bool matchAllTags READ matchAllTags WRITE setMatchAllTags NOTIFY tagsChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY projectsChanged)
    Q_PROPERTY(QString mainFolder READ mainFolder NOTIFY categoriesChanged)

    QML_ELEMENT

    muse::GlobalInject<IProjectConfiguration> configuration;
    muse::ContextInject<IRecentFilesController> recentFilesController = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<context::IGlobalContext> context = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::GlobalInject<IProjectCreator> projectCreator;   // Jubelle: converting MusicXML on import

public:
    explicit ProjectsPageModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void reload();

    QVariantList recentScores() const;
    QVariantList categories() const;
    QString currentCategory() const;
    void setCurrentCategory(const QString& category);
    QVariantList projects() const;
    int projectsInCategoryCount() const;
    QStringList availableTags() const;
    QStringList activeTags() const;
    bool matchAllTags() const;
    void setMatchAllTags(bool all);
    QString searchText() const;
    void setSearchText(const QString& text);
    QString mainFolder() const;

    // Tag filter
    Q_INVOKABLE void toggleTag(const QString& tag);
    Q_INVOKABLE void removeActiveTag(const QString& tag);
    Q_INVOKABLE void clearTags();
    Q_INVOKABLE bool addTag(const QString& tag);

    // Categories
    Q_INVOKABLE bool createCategory(const QString& name);
    Q_INVOKABLE bool renameCategory(const QString& oldName, const QString& newName);
    Q_INVOKABLE bool deleteCategory(const QString& name);
    Q_INVOKABLE void moveCategory(const QString& name, int delta);
    Q_INVOKABLE void setCategoryOrder(const QStringList& names);
    Q_INVOKABLE void revealCategory(const QString& name);

    // Projects
    Q_INVOKABLE void createNewScore();
    Q_INVOKABLE void createScoreFromTemplate(const QString& templatePath);
    Q_INVOKABLE void openOther();
    Q_INVOKABLE void importScoresInteractive();   // Jubelle R1
    Q_INVOKABLE void openScore(const QString& scorePath);
    Q_INVOKABLE void revealInFileBrowser(const QString& path);
    Q_INVOKABLE void openProjectsFolder();
    Q_INVOKABLE bool isProjectOpen(const QString& projectFolder) const;
    //! Shows the "close the score first" message and returns false when the project is open.
    Q_INVOKABLE bool checkCanChangeProject(const QString& projectFolder);
    Q_INVOKABLE QString validateName(const QString& name) const;
    Q_INVOKABLE bool renameProject(const QString& projectFolder, const QString& newName, bool changePrintedTitle);
    Q_INVOKABLE bool moveProject(const QString& projectFolder, const QString& category);
    Q_INVOKABLE QStringList projectCategories(const QString& projectFolder) const;
    Q_INVOKABLE QString homeCategory(const QString& projectFolder) const;
    Q_INVOKABLE bool setProjectCategories(const QString& projectFolder, const QStringList& categories);
    Q_INVOKABLE QStringList projectTags(const QString& projectFolder) const;
    Q_INVOKABLE bool setProjectTags(const QString& projectFolder, const QStringList& tags);

    // Card image
    Q_INVOKABLE QString chooseImageFile(const QString& projectFolder = QString());
    Q_INVOKABLE void openPicturesFolder(const QString& projectFolder);   // P4
    Q_INVOKABLE QString imageUrl(const QString& filePath) const;
    Q_INVOKABLE QVariantMap cardImage(const QString& projectFolder) const;
    Q_INVOKABLE bool setCardImage(const QString& projectFolder, const QString& sourcePath, double x, double y);
    Q_INVOKABLE bool setCardImagePosition(const QString& projectFolder, double x, double y);
    Q_INVOKABLE bool removeCardImage(const QString& projectFolder);

    // Dialog-driven actions (the ⋯ menu and the category menu)
    Q_INVOKABLE void renameProjectInteractive(const QString& projectFolder);
    Q_INVOKABLE void moveProjectInteractive(const QString& projectFolder);
    Q_INVOKABLE void editCategoriesAndTags(const QString& projectFolder);
    Q_INVOKABLE void editCardImage(const QString& projectFolder);
    Q_INVOKABLE void newCategoryInteractive();
    Q_INVOKABLE void renameCategoryInteractive(const QString& name);
    Q_INVOKABLE void deleteCategoryInteractive(const QString& name);

signals:
    void recentScoresChanged();
    void categoriesChanged();
    void currentCategoryChanged();
    void projectsChanged();
    void tagsChanged();

private:
    //! Jubelle: converts a MusicXML file (from Noteflight or another program) into a new
    //! project; returns the new score's path ("" on failure, with the reason in `error`)
    std::string importMusicXml(const std::string& mainFolder, const std::string& category, const std::string& name,
                               const std::string& sourcePath, QString* error, QString* report);

    std::string mainFolderStd() const;
    QVariantMap projectItem(const std::string& projectFolder, int64_t savedTime) const;
    void updateRecent();
    void updateCategories();
    void updateProjects();
    void updateTags();
    void showError(const QString& title, const std::string& message);
    void updateRecentFileAfterMove(const QString& oldScorePath, const QString& newScorePath);
    void applyPrintedTitle(const QString& scorePath, const QString& title);
    void openCardImageDialog(const QString& projectFolder, const QString& imageFile, double x, double y, bool isNewImage);
    QStringList categoryNames() const;

    QVariantList m_recent;
    QVariantList m_categories;
    QString m_currentCategory;
    QVariantList m_projects;
    int m_projectsInCategoryCount = 0;
    QStringList m_availableTags;
    QStringList m_activeTags;
    bool m_matchAll = true;
    QString m_searchText;
};
}
