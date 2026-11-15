/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#pragma once

#include <set>
#include <string>

#include <QObject>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "actions/iactionsdispatcher.h"
#include "context/iglobalcontext.h"
#include "project/iprojectconfiguration.h"

namespace mu::project {
//! Jubelle R1: the slim bar under the toolbar when the open score isn't in
//! your Jubelle Projects: "Add to Projects…" · "Not now" · "Don't ask for this file"
class NotInProjectsBarModel : public QObject, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY shownChanged)

    QML_ELEMENT

    muse::GlobalInject<IProjectConfiguration> configuration;
    muse::ContextInject<context::IGlobalContext> context = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };

public:
    explicit NotInProjectsBarModel(QObject* parent = nullptr);

    bool shown() const;
    QString fileName() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void addToProjects();
    Q_INVOKABLE void notNow();
    Q_INVOKABLE void dontAskForThisFile();

    // the rule itself: projectlibrary::shouldOfferAddToProjects (tested)

signals:
    void shownChanged();

private:
    void update();
    std::string currentPath() const;

    bool m_shown = false;
    std::set<std::string> m_dismissed;   // "Not now", for this session
};
}
