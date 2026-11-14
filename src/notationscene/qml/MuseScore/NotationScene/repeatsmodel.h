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

namespace mu::engraving {
class Measure;
class Score;
}

namespace mu::notation {
//! Jubelle: Playback → Repeats…  Every repeated section in the score, each
//! with "Play this repeat". The repeat signs always stay printed. Nothing
//! changes until Apply is pressed, as one undo step.
class RepeatsModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList repeats READ repeats NOTIFY changed)
    Q_PROPERTY(bool hasChanges READ hasChanges NOTIFY changed)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit RepeatsModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void setPlay(int row, bool play);
    Q_INVOKABLE void setAll(bool play);
    Q_INVOKABLE bool apply();

    QVariantList repeats() const;
    bool hasChanges() const;
    QString resultText() const;

signals:
    void changed();
    void resultChanged();

private:
    engraving::Score* score() const;
    void setResult(const QString& text);

    struct Repeat {
        engraving::Measure* endMeasure = nullptr;
        QString title;
        QString detail;
        bool originalPlay = true;
        bool play = true;
    };
    std::vector<Repeat> m_repeats;
    QString m_resultText;
};
}
