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

#include "engraving/playback/jubelle/articulationplayback.h"

namespace mu::notation {
//! Jubelle: Format → Articulation Playback…
//! Edits how articulations sound, for the whole score and for single
//! instruments. Changes are only made when Apply is pressed, as one undo step.
class ArticulationPlaybackModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList scopes READ scopes NOTIFY scopesChanged)
    Q_PROPERTY(int currentScope READ currentScope WRITE setCurrentScope NOTIFY currentScopeChanged)
    Q_PROPERTY(QVariantList params READ params NOTIFY paramsChanged)
    Q_PROPERTY(bool hasChanges READ hasChanges NOTIFY paramsChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit ArticulationPlaybackModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void setValue(const QString& key, int value);
    Q_INVOKABLE void resetValue(const QString& key);
    Q_INVOKABLE void resetScope();
    Q_INVOKABLE bool apply();

    QVariantList scopes() const;
    int currentScope() const;
    void setCurrentScope(int index);
    QVariantList params() const;
    bool hasChanges() const;
    QString resultText() const;

signals:
    void scopesChanged();
    void currentScopeChanged();
    void paramsChanged();
    void resultChanged();

private:
    engraving::Score* score() const;
    engraving::jubelle::ArticulationPlaybackValues& currentValues();
    const engraving::jubelle::ArticulationPlaybackValues* currentValuesIfAny() const;
    void setResult(const QString& text);

    struct Scope {
        QString name;
        uint64_t partId = 0;   // 0 = whole score
    };
    std::vector<Scope> m_scopes;
    int m_currentScope = 0;

    engraving::jubelle::ArticulationPlaybackSettings m_original;
    engraving::jubelle::ArticulationPlaybackSettings m_settings;
    QString m_resultText;
};
}
