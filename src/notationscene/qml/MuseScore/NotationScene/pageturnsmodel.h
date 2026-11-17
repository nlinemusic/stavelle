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

#include "notation/internal/jubelle/pageturns.h"

namespace mu::engraving {
class Score;
}

namespace mu::notation {
//! Jubelle G8: Format › Page turns…  Shows where each part's page turns fall and how much
//! rest there is, previews better turns, and applies them (one undo). Never changes music;
//! your own page breaks are kept.
class PageTurnsModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList parts READ parts NOTIFY changed)
    Q_PROPERTY(QVariantList turns READ turns NOTIFY changed)
    Q_PROPERTY(int partIndex READ partIndex NOTIFY changed)
    Q_PROPERTY(double neededSeconds READ neededSeconds NOTIFY changed)
    Q_PROPERTY(int layoutIndex READ layoutIndex NOTIFY changed)
    Q_PROPERTY(QVariantList layoutOptions READ layoutOptions CONSTANT)
    Q_PROPERTY(bool previewing READ previewing NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(bool hasJubelleTurns READ hasJubelleTurns NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit PageTurnsModel(QObject* parent = nullptr);

    QVariantList parts() const;
    QVariantList turns() const { return m_turns; }
    int partIndex() const { return m_partIndex; }
    double neededSeconds() const { return m_settings.neededSeconds; }
    int layoutIndex() const { return int(m_settings.layout); }
    QVariantList layoutOptions() const;
    bool previewing() const { return m_previewing; }
    QString summary() const { return m_summary; }
    bool hasJubelleTurns() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void setPartIndex(int index);
    Q_INVOKABLE void setNeededSeconds(double seconds);
    Q_INVOKABLE void setLayoutIndex(int index);

    //! Works out better turns for the chosen part and shows them, without changing anything
    Q_INVOKABLE void preview();
    //! Applies better turns to the chosen part, or to every part
    Q_INVOKABLE void apply(bool allParts);
    //! Removes the turns Jubelle added (yours stay), from the chosen part or every part
    Q_INVOKABLE void clear(bool allParts);
    Q_INVOKABLE void goTo(int bar);

signals:
    void changed();

private:
    struct PartEntry {
        QString name;
        engraving::Score* score = nullptr;
    };

    engraving::Score* chosen() const;
    void showCurrent();
    void showResult(const jubelle::TurnResult& result);
    QString qualityText(jubelle::TurnQuality q) const;

    std::vector<PartEntry> m_parts;
    int m_partIndex = 0;
    jubelle::TurnSettings m_settings;
    QVariantList m_turns;
    bool m_previewing = false;
    QString m_summary;
};
}
