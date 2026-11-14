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

#pragma once

#include <QObject>
#include <qqmlintegration.h>

#include "modularity/ioc.h"

#include "project/iprojectconfiguration.h"
#include "project/iprojectcreator.h"
#include "notation/notationtypes.h"
#include "context/iglobalcontext.h"
#include "notation/iinstrumentsrepository.h"

namespace mu::project {
class NewScoreModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    //! Jubelle: project categories for the "Project" row of the dialog.
    Q_PROPERTY(QStringList projectCategories READ projectCategories NOTIFY projectCategoriesChanged)
    Q_PROPERTY(QString mainFolder READ mainFolder CONSTANT)

    QML_ELEMENT

    muse::GlobalInject<IProjectConfiguration> configuration;
    muse::GlobalInject<IProjectCreator> notationCreator;
    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<notation::IInstrumentsRepository> instrumentsRepository = { this };

public:
    explicit NewScoreModel(QObject* parent = nullptr);

    Q_INVOKABLE QString preferredScoreCreationMode() const;
    Q_INVOKABLE bool createScore(const QVariant& info);

    // Jubelle: project setup inside the New Score dialog
    QStringList projectCategories() const;
    QString mainFolder() const;

    //! The category used last time (or the first one).
    Q_INVOKABLE QString defaultProjectCategory() const;
    //! "" if the name can be used, otherwise a plain-language reason.
    Q_INVOKABLE QString validateCompositionName(const QString& name) const;
    //! Creates a category in the main project folder; "" on success, otherwise the reason.
    Q_INVOKABLE QString createProjectCategory(const QString& name);
    //! The folder name the project will actually get (e.g. "Song (2)" if "Song" exists).
    Q_INVOKABLE QString projectFolderName(const QString& category, const QString& name) const;
    //! The main score file of the project created by the last successful createScore(),
    //! or "" when no project folder was made. The dialog passes it on to be saved.
    Q_INVOKABLE QString createdScorePath() const;

signals:
    void projectCategoriesChanged();

private:
    project::ProjectCreateOptions parseOptions(const QVariantMap& info) const;
    notation::DurationType noteIconToDurationType(int noteIconCode) const;
    void updatePreferredScoreCreationMode(bool isScoreCreatedFromInstruments);

    std::string mainFolderStd() const;

    QString m_createdScorePath;
};
}
