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

#include <QObject>
#include <QStringList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "actions/iactionsdispatcher.h"
#include "context/iglobalcontext.h"
#include "multiwindows/imultiwindowsprovider.h"

#include "notation/internal/jubelle/condensedscore.h"

namespace mu::notation {
//! Jubelle G2: Tools → Condensing → Condensed Score…
//! Creates or updates "<name> (condensed).mscz" next to the score, only when
//! you press the button. The score itself is never changed. Updating replaces
//! everything in the copy (your overrides made in the copy are kept).
class CondensedScoreModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(bool condensingEnabled READ condensingEnabled NOTIFY changed)
    Q_PROPERTY(bool isCopy READ isCopy NOTIFY changed)
    Q_PROPERTY(QString scoreName READ scoreName NOTIFY changed)
    Q_PROPERTY(QString copyFileName READ copyFileName NOTIFY changed)
    Q_PROPERTY(QString copyFolder READ copyFolder NOTIFY changed)
    Q_PROPERTY(bool copyExists READ copyExists NOTIFY changed)
    Q_PROPERTY(QStringList pairs READ pairs NOTIFY changed)
    Q_PROPERTY(QStringList overrides READ overrides NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    Q_PROPERTY(QString resultText READ resultText NOTIFY changed)
    Q_PROPERTY(bool done READ done NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::GlobalInject<muse::mi::IMultiWindowsProvider> multiwindowsProvider;

public:
    explicit CondensedScoreModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void toggleEnabled();
    Q_INVOKABLE void createOrUpdate();
    Q_INVOKABLE void openCopy();

    bool condensingEnabled() const;
    bool isCopy() const;
    QString scoreName() const;
    QString copyFileName() const;
    QString copyFolder() const;
    bool copyExists() const;
    QStringList pairs() const;
    QStringList overrides() const;
    QString problem() const;
    QString resultText() const;
    bool done() const;

signals:
    void changed();

private:
    engraving::MasterScore* masterScore() const;
    QString describe(const std::vector<jubelle::CondenseOverride>& list) const;

    bool m_isCopy = false;
    QString m_scoreName;
    muse::io::path_t m_copyPath;
    bool m_copyExists = false;
    QStringList m_pairs;
    QStringList m_overrides;
    std::vector<jubelle::CondenseOverride> m_existingOverrides;
    QString m_problem;
    QString m_result;
    bool m_done = false;
};
}
