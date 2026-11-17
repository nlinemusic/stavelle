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
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"

#include "notation/internal/jubelle/articulationpassage.h"

namespace mu::notation {
//! Jubelle: the quick popup "Articulation Playback for Selection…". Each
//! change is applied at once, as its own undo step, to the highlighted
//! passage of each highlighted instrument.
class PassagePlaybackModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QString passageTitle READ passageTitle NOTIFY changed)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(bool showAll READ showAll WRITE setShowAll NOTIFY changed)
    Q_PROPERTY(bool isValid READ isValid NOTIFY changed)
    Q_PROPERTY(bool hasOverrides READ hasOverrides NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit PassagePlaybackModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void setValue(const QString& key, int value);
    Q_INVOKABLE void clearValue(const QString& key);
    Q_INVOKABLE void clearAll();

    QString passageTitle() const;
    QVariantList rows() const;
    bool showAll() const;
    void setShowAll(bool v);
    bool isValid() const;
    bool hasOverrides() const;

signals:
    void changed();

private:
    void change(const QString& key, std::optional<int> value);

    jubelle::PassageRange m_range;
    std::vector<jubelle::PassageRow> m_rows;
    QString m_title;
    bool m_showAll = false;
};
}
