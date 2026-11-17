/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "notinprojectsbarmodel.h"

#include "project/internal/projectlibrary.h"

using namespace mu::project;
namespace jpl = mu::project::projectlibrary;

NotInProjectsBarModel::NotInProjectsBarModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

std::string NotInProjectsBarModel::currentPath() const
{
    INotationProjectPtr project = context() ? context()->currentProject() : nullptr;
    return project ? project->path().toStdString() : std::string();
}

void NotInProjectsBarModel::update()
{
    INotationProjectPtr project = context() ? context()->currentProject() : nullptr;
    const std::string main = configuration()->userProjectsPath().toStdString();
    const bool show = project && jpl::shouldOfferAddToProjects(project->path().toStdString(), project->isNewlyCreated(), main, m_dismissed);
    if (show != m_shown) {
        m_shown = show;
        emit shownChanged();
    } else if (show) {
        emit shownChanged();   // another outside score: its name changed
    }
}

void NotInProjectsBarModel::load()
{
    if (!context()) {
        return;
    }
    context()->currentProjectChanged().onNotify(this, [this]() {
        update();
        if (INotationProjectPtr project = context()->currentProject()) {
            project->pathChanged().onNotify(this, [this]() { update(); }, Asyncable::Mode::SetReplace);
        }
    });
    if (INotationProjectPtr project = context()->currentProject()) {
        project->pathChanged().onNotify(this, [this]() { update(); }, Asyncable::Mode::SetReplace);
    }
    update();
}

bool NotInProjectsBarModel::shown() const
{
    return m_shown;
}

QString NotInProjectsBarModel::fileName() const
{
    const std::string path = currentPath();
    const size_t slash = path.find_last_of("/\\");
    return QString::fromStdString(slash == std::string::npos ? path : path.substr(slash + 1));
}

void NotInProjectsBarModel::addToProjects()
{
    dispatcher()->dispatch("jubelle-add-to-projects");
    update();   // after a successful copy the open score is the Projects copy
}

void NotInProjectsBarModel::notNow()
{
    const std::string path = currentPath();
    if (!path.empty()) {
        m_dismissed.insert(path);
    }
    update();
}

void NotInProjectsBarModel::dontAskForThisFile()
{
    const std::string path = currentPath();
    const std::string main = configuration()->userProjectsPath().toStdString();
    if (!path.empty() && !main.empty()) {
        jpl::silenceAddToProjects(main, path);
        m_dismissed.insert(path);
    }
    update();
}
