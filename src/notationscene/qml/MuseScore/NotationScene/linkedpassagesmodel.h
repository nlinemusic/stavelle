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

#include <functional>

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"

#include "notation/internal/jubelle/linkedpassages.h"

namespace mu::notation {
//! Jubelle: Tools → Linked Passages…  Link the highlighted passage to the
//! same bars on other staves, and see, re-sync or unlink existing links.
//! Every change is one undo step. Works on the full score only (links are
//! stored there; parts show the same music).
class LinkedPassagesModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    //! "Trumpet 1 · bars 1–4" for the highlighted passage, or empty
    Q_PROPERTY(QString selectionTitle READ selectionTitle NOTIFY changed)
    //! Staves the highlighted passage could be linked to: { staff, name, checked }
    Q_PROPERTY(QVariantList candidateStaves READ candidateStaves NOTIFY changed)
    //! Existing links: { id, title, sync: {...}, members: [{ line, name, bars, isSource, ownNotes }] }
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
    Q_PROPERTY(QStringList messages READ messages NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit LinkedPassagesModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void setCandidateChecked(int staff, bool checked);
    //! Links the highlighted passage to the ticked staves
    Q_INVOKABLE void link(bool rhythm, bool durations, bool articulations, bool dynamics, bool markings);
    Q_INVOKABLE void setSync(int groupId, const QString& option, bool on);
    Q_INVOKABLE void resync(int groupId, int memberIndex);
    Q_INVOKABLE void unlink(int groupId, int memberIndex);
    //! Highlights a member's bars in the score
    Q_INVOKABLE void show(int groupId, int memberIndex);

    QString selectionTitle() const;
    QVariantList candidateStaves() const;
    QVariantList groups() const;
    QStringList messages() const;
    QString problem() const;

signals:
    void changed();

private:
    engraving::Score* score() const;
    const jubelle::LinkMember* member(int groupId, int memberIndex) const;
    QString staffName(size_t staffIdx) const;
    QString barsText(int startTick, int endTick) const;
    void apply(const char* undoText, const std::function<void(engraving::Score*)>& f);

    size_t m_sourceStaff = 0;
    int m_startTick = 0;
    int m_endTick = 0;
    bool m_hasSelection = false;
    std::vector<std::pair<size_t, bool> > m_candidates;
    std::vector<jubelle::LinkGroup> m_groups;
    QStringList m_messages;
    QString m_problem;
};
}
