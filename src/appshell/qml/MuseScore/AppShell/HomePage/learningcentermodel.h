/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "actions/iactionsdispatcher.h"
#include "notation/iinstrumentsrepository.h"

namespace mu::appshell {
//! Jubelle G10: Learning Center. The instrument reference (ranges and transpositions) comes from
//! the program's own instrument data; the guides are in LearningContent.js.
class LearningCenterModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList instruments READ instruments NOTIFY changed)
    Q_PROPERTY(QString search READ search NOTIFY changed)
    Q_PROPERTY(bool onlyTransposing READ onlyTransposing NOTIFY changed)
    Q_PROPERTY(int instrumentCount READ instrumentCount NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<notation::IInstrumentsRepository> instrumentsRepository = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };

public:
    explicit LearningCenterModel(QObject* parent = nullptr);

    QVariantList instruments() const { return m_instruments; }
    QString search() const { return m_search; }
    bool onlyTransposing() const { return m_onlyTransposing; }
    int instrumentCount() const { return int(m_instruments.size()); }

    Q_INVOKABLE void load();
    Q_INVOKABLE void setSearch(const QString& text);
    Q_INVOKABLE void setOnlyTransposing(bool on);
    Q_INVOKABLE void openShortcuts();

signals:
    void changed();

private:
    void rebuild();

    QVariantList m_instruments;
    QString m_search;
    bool m_onlyTransposing = false;
};
}
