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
#include "newscoremodel.h"

#include <QDir>

#include "settings.h"
#include "ui/view/musicalsymbolcodes.h"

#include "project/internal/projectlibrary.h"

#include "log.h"

using namespace mu::project;
using namespace mu::notation;
using namespace muse;
using namespace muse::ui;

using PreferredScoreCreationMode = IProjectConfiguration::PreferredScoreCreationMode;

NewScoreModel::NewScoreModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QString NewScoreModel::preferredScoreCreationMode() const
{
    switch (configuration()->preferredScoreCreationMode()) {
    case PreferredScoreCreationMode::FromInstruments: return "FromInstruments";
    case PreferredScoreCreationMode::FromTemplate: return "FromTemplate";
    }

    return "";
}

namespace pl = mu::project::projectlibrary;

//! Jubelle: remembers the category chosen for the last new score.
static const Settings::Key LAST_NEW_SCORE_CATEGORY("project", "jubelle/lastNewScoreCategory");

std::string NewScoreModel::mainFolderStd() const
{
    return QDir::toNativeSeparators(configuration()->userProjectsPath().toQString()).toStdString();
}

QString NewScoreModel::mainFolder() const
{
    return configuration()->userProjectsPath().toQString();
}

QStringList NewScoreModel::projectCategories() const
{
    const std::string main = mainFolderStd();
    pl::ensureMainFolder(main);

    QStringList list;
    for (const std::string& c : pl::categories(main)) {
        list << QString::fromStdString(c);
    }
    return list;
}

QString NewScoreModel::defaultProjectCategory() const
{
    const QStringList categories = projectCategories();
    const QString last = settings()->value(LAST_NEW_SCORE_CATEGORY).toQString();
    if (!last.isEmpty() && categories.contains(last)) {
        return last;
    }
    return categories.value(0);
}

QString NewScoreModel::validateCompositionName(const QString& name) const
{
    return QString::fromStdString(pl::validateName(name.trimmed().toStdString()));
}

QString NewScoreModel::createProjectCategory(const QString& name)
{
    pl::Result r = pl::createCategory(mainFolderStd(), name.trimmed().toStdString());
    if (!r.ok) {
        return QString::fromStdString(r.error);
    }
    emit projectCategoriesChanged();
    return QString();
}

QString NewScoreModel::projectFolderName(const QString& category, const QString& name) const
{
    if (category.isEmpty() || name.trimmed().isEmpty()) {
        return QString();
    }
    return QString::fromStdString(pl::availableProjectName(mainFolderStd(), category.toStdString(),
                                                           name.trimmed().toStdString()));
}

QString NewScoreModel::createdScorePath() const
{
    return m_createdScorePath;
}

bool NewScoreModel::createScore(const QVariant& info)
{
    m_createdScorePath.clear();

    const QVariantMap infoMap = info.toMap();
    ProjectCreateOptions options = parseOptions(infoMap);

    // Jubelle: with a composition name and a category, the project folder is made
    // first (never reusing an existing folder), and the score is saved into it
    // right after it opens.
    const QString compositionName = infoMap.value("compositionName").toString().trimmed();
    const QString category = infoMap.value("projectCategory").toString();
    std::string projectFolder;
    if (!compositionName.isEmpty() && !category.isEmpty()) {
        const std::string main = mainFolderStd();
        pl::ensureMainFolder(main);
        pl::Result folder = pl::createProject(main, category.toStdString(), compositionName.toStdString());
        if (!folder.ok) {
            LOGE() << "Project folder not created: " << folder.error;
            return false;
        }
        projectFolder = folder.path;

        if (options.title.isEmpty()) {
            options.title = compositionName; // the printed title can be changed later as usual
        }
    }

    auto project = notationCreator()->newProject(iocContext());
    Ret ret = project->createNew(options);

    if (!ret) {
        LOGE() << ret.toString();
        if (!projectFolder.empty()) {
            pl::removeUnusedProject(projectFolder); // only removes it if still empty
        }
        return false;
    }

    if (!projectFolder.empty()) {
        m_createdScorePath = QDir::fromNativeSeparators(QString::fromStdString(pl::mainScorePath(projectFolder)));
        settings()->setSharedValue(LAST_NEW_SCORE_CATEGORY, Val(category));
    }

    globalContext()->setCurrentProject(project);

    bool isScoreCreatedFromInstruments = options.templatePath.empty();
    updatePreferredScoreCreationMode(isScoreCreatedFromInstruments);

    return true;
}

ProjectCreateOptions NewScoreModel::parseOptions(const QVariantMap& info) const
{
    ProjectCreateOptions projectOptions;

    projectOptions.title = info["title"].toString();
    projectOptions.subtitle = info["subtitle"].toString();
    projectOptions.composer = info["composer"].toString();
    projectOptions.lyricist = info["lyricist"].toString();
    projectOptions.copyright = info["copyright"].toString();

    projectOptions.templatePath = info["templatePath"].toString();

    ScoreCreateOptions& scoreOptions = projectOptions.scoreOptions;

    scoreOptions.withTempo = info["withTempo"].toBool();

    QVariantMap tempo = info["tempo"].toMap();
    scoreOptions.tempo.valueBpm = tempo["value"].toInt();
    scoreOptions.tempo.duration = noteIconToDurationType(tempo["noteIcon"].toInt());
    scoreOptions.tempo.withDot = tempo["withDot"].toBool();

    QVariantMap timeSignature = info["timeSignature"].toMap();
    scoreOptions.timesigType = static_cast<TimeSigType>(info["timeSignatureType"].toInt());

    const int timesigNumerator = timeSignature["numerator"].toInt();
    const int timesigDenominator = timeSignature["denominator"].toInt();
    scoreOptions.globalTimesig = Fraction(timesigNumerator, timesigDenominator);

    QVariantMap keySignature = info["keySignature"].toMap();
    scoreOptions.key = static_cast<Key>(keySignature["key"].toInt());

    QVariantMap measuresPickup = info["pickupTimeSignature"].toMap();
    scoreOptions.totalMeasures = info["measureCount"].toInt();

    scoreOptions.withPickupMeasure = info["withPickupMeasure"].toBool();
    const int pickupNumerator = measuresPickup["numerator"].toInt();
    const int pickupDenominator = measuresPickup["denominator"].toInt();
    scoreOptions.pickupTimesig = Fraction(pickupNumerator, pickupDenominator);

    QVariantList instruments = info["instruments"].toList();

    for (const QVariant& obj : instruments) {
        QVariantMap objMap = obj.toMap();

        PartInstrument pi;

        String instrumentId = objMap["instrumentId"].toString();
        pi.instrumentTemplate = instrumentsRepository()->instrumentTemplate(instrumentId);
        pi.isExistingPart = objMap["isExistingPart"].toBool();
        pi.isSoloist = objMap["isSoloist"].toBool();

        scoreOptions.parts << pi;
    }

    QVariantMap orderMap = info["scoreOrder"].toMap();
    scoreOptions.order = instrumentsRepository()->order(orderMap["id"].toString());
    scoreOptions.order.customized = orderMap["customized"].toBool();

    return projectOptions;
}

DurationType NewScoreModel::noteIconToDurationType(int noteIconCode) const
{
    static const QMap<MusicalSymbolCodes::Code, DurationType> iconToDuration {
        { MusicalSymbolCodes::Code::SEMIBREVE, DurationType::V_WHOLE },
        { MusicalSymbolCodes::Code::MINIM, DurationType::V_HALF },
        { MusicalSymbolCodes::Code::CROTCHET, DurationType::V_QUARTER },
        { MusicalSymbolCodes::Code::QUAVER, DurationType::V_EIGHTH },
        { MusicalSymbolCodes::Code::SEMIQUAVER, DurationType::V_16TH }
    };

    MusicalSymbolCodes::Code symbol = static_cast<MusicalSymbolCodes::Code>(noteIconCode);
    return iconToDuration.value(symbol, DurationType::V_QUARTER);
}

void NewScoreModel::updatePreferredScoreCreationMode(bool isScoreCreatedFromInstruments)
{
    if (isScoreCreatedFromInstruments) {
        configuration()->setPreferredScoreCreationMode(PreferredScoreCreationMode::FromInstruments);
    } else {
        configuration()->setPreferredScoreCreationMode(PreferredScoreCreationMode::FromTemplate);
    }
}
