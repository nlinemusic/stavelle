/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "context/iglobalcontext.h"

#include "notation/internal/jubelle/proofread.h"

namespace mu::notation {
//! Jubelle G5: the Proofread panel (Tools › Proofread score). A grouped list
//! of findings; click one to go there; Dismiss hides it until the next check;
//! Mark as intentional remembers it in the score. Never changes notes or
//! notation.
class ProofreadModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList findings READ findings NOTIFY changed)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY changed)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY changed)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY changed)
    Q_PROPERTY(int suggestionCount READ suggestionCount NOTIFY changed)
    Q_PROPERTY(bool hasScore READ hasScore NOTIFY changed)
    Q_PROPERTY(bool showIntentional READ showIntentional NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit ProofreadModel(QObject* parent = nullptr);

    QVariantList findings() const;
    QVariantList categories() const;
    int errorCount() const { return m_counts[0]; }
    int warningCount() const { return m_counts[1]; }
    int suggestionCount() const { return m_counts[2]; }
    bool hasScore() const;
    bool showIntentional() const { return m_options.includeIntentional; }

    Q_INVOKABLE void load();
    //! Checks the score again
    Q_INVOKABLE void check();
    Q_INVOKABLE void setCategory(int category, bool on);
    Q_INVOKABLE void setShowIntentional(bool on);
    Q_INVOKABLE void goTo(int index);
    Q_INVOKABLE void dismiss(int index);
    Q_INVOKABLE void setIntentional(int index, bool intentional);

signals:
    void changed();

private:
    engraving::Score* score() const;

    jubelle::ProofreadOptions m_options;
    std::vector<jubelle::Finding> m_findings;
    std::set<std::string> m_dismissed;
    std::set<std::string> m_intentional;
    int m_counts[3] = { 0, 0, 0 };
};
}
