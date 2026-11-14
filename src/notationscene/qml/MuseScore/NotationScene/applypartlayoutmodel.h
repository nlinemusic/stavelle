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

#include "notation/internal/jubelle/partlayout.h"

namespace mu::engraving {
class Score;
}

namespace mu::notation {
//! Jubelle: Format → Apply Part Layout…  Copies one part's layout to the
//! parts you tick, as one undo step, after showing what will and won't match.
class ApplyPartLayoutModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
    Q_PROPERTY(int sourceIndex READ sourceIndex WRITE setSourceIndex NOTIFY changed)
    Q_PROPERTY(bool sourceFixed READ sourceFixed NOTIFY changed)
    Q_PROPERTY(QVariantList destinations READ destinations NOTIFY changed)

    Q_PROPERTY(bool systemLocks READ systemLocks WRITE setSystemLocks NOTIFY changed)
    Q_PROPERTY(bool measureWidths READ measureWidths WRITE setMeasureWidths NOTIFY changed)
    Q_PROPERTY(bool spacers READ spacers WRITE setSpacers NOTIFY changed)
    Q_PROPERTY(bool pageAndSpacing READ pageAndSpacing WRITE setPageAndSpacing NOTIFY changed)
    Q_PROPERTY(bool breaksInsideMMRests READ breaksInsideMMRests WRITE setBreaksInsideMMRests NOTIFY changed)

    Q_PROPERTY(bool canApply READ canApply NOTIFY changed)
    Q_PROPERTY(QString resultText READ resultText NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit ApplyPartLayoutModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void setDestinationChecked(int row, bool checked);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool apply();

    QVariantList sources() const;
    int sourceIndex() const;
    void setSourceIndex(int index);
    bool sourceFixed() const;
    QVariantList destinations() const;

    bool systemLocks() const { return m_options.systemLocks; }
    void setSystemLocks(bool v) { m_options.systemLocks = v; touch(); }
    bool measureWidths() const { return m_options.measureWidths; }
    void setMeasureWidths(bool v) { m_options.measureWidths = v; touch(); }
    bool spacers() const { return m_options.spacers; }
    void setSpacers(bool v) { m_options.spacers = v; touch(); }
    bool pageAndSpacing() const { return m_options.pageAndSpacing; }
    void setPageAndSpacing(bool v) { m_options.pageAndSpacing = v; touch(); }
    bool breaksInsideMMRests() const { return m_options.breaksInsideMMRests; }
    void setBreaksInsideMMRests(bool v) { m_options.breaksInsideMMRests = v; touch(); }

    bool canApply() const;
    QString resultText() const;

signals:
    void changed();

private:
    void touch();
    engraving::Score* source() const;

    struct PartEntry {
        QString name;
        engraving::Score* score = nullptr;
        bool checked = false;
    };
    std::vector<PartEntry> m_parts;
    int m_sourceIndex = 0;
    bool m_sourceFixed = false;
    jubelle::PartLayoutOptions m_options;
    QString m_resultText;
};
}
