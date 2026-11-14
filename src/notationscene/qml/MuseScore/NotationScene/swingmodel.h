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
#include <QVariantMap>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"

#include "engraving/playback/jubelle/swingfeel.h"

namespace mu::notation {
//! Jubelle: Format → Swing…  Chooses the rhythmic feel for the whole score
//! and for single instruments, and edits the feels. Playback only. Nothing
//! changes until Apply is pressed, as one undo step.
class SwingModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList feelChoices READ feelChoices NOTIFY changed)
    Q_PROPERTY(int scoreFeelIndex READ scoreFeelIndex WRITE setScoreFeelIndex NOTIFY changed)
    Q_PROPERTY(QVariantList instruments READ instruments NOTIFY changed)

    Q_PROPERTY(QVariantList editableFeels READ editableFeels NOTIFY changed)
    Q_PROPERTY(int editedFeelIndex READ editedFeelIndex WRITE setEditedFeelIndex NOTIFY changed)
    Q_PROPERTY(QVariantMap editedFeel READ editedFeel NOTIFY changed)

    Q_PROPERTY(bool hasChanges READ hasChanges NOTIFY changed)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit SwingModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void setInstrumentFeel(int row, int choiceIndex);
    Q_INVOKABLE void setFeelValue(const QString& key, const QVariant& value);
    Q_INVOKABLE void newCustomFeel();
    Q_INVOKABLE void deleteEditedFeel();
    Q_INVOKABLE void restoreEditedFeel();
    Q_INVOKABLE bool apply();

    //! Shown names for all feels, e.g. for the Swing text properties
    static QString displayName(const engraving::jubelle::SwingFeel& feel);

    QVariantList feelChoices() const;
    int scoreFeelIndex() const;
    void setScoreFeelIndex(int index);
    QVariantList instruments() const;
    QVariantList editableFeels() const;
    int editedFeelIndex() const;
    void setEditedFeelIndex(int index);
    QVariantMap editedFeel() const;
    bool hasChanges() const;
    QString resultText() const;

signals:
    void changed();
    void resultChanged();

private:
    engraving::Score* score() const;
    void setResult(const QString& text);
    void touch();

    struct Instrument {
        QString name;
        uint64_t partId = 0;
    };
    std::vector<Instrument> m_instruments;

    engraving::jubelle::SwingSettings m_original;
    engraving::jubelle::SwingSettings m_settings;
    int m_editedFeelIndex = 0;
    QString m_resultText;
};
}
