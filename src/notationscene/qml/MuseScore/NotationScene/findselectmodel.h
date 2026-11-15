/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "global/iglobalconfiguration.h"

#include "notation/internal/jubelle/findselect.h"

namespace mu::engraving {
class Score;
}

namespace mu::notation {
//! Jubelle G5: Select › Find and Select…  Criteria rows, a live count of what
//! matches, Select, and saved filters (kept in Jubelle's settings folder, not
//! in the score). Finding never changes the score.
class FindSelectModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList kindOptions READ kindOptions CONSTANT)
    Q_PROPERTY(QVariantList selectedKinds READ selectedKinds NOTIFY changed)
    Q_PROPERTY(QVariantList staffOptions READ staffOptions NOTIFY changed)
    Q_PROPERTY(QVariantList selectedStaves READ selectedStaves NOTIFY changed)
    Q_PROPERTY(QVariantList articulationOptions READ articulationOptions CONSTANT)
    Q_PROPERTY(QString articulation READ articulation NOTIFY changed)

    Q_PROPERTY(int voices READ voices NOTIFY changed)
    Q_PROPERTY(bool usePitch READ usePitch NOTIFY changed)
    Q_PROPERTY(int lowestPitch READ lowestPitch NOTIFY changed)
    Q_PROPERTY(int highestPitch READ highestPitch NOTIFY changed)
    Q_PROPERTY(QString lowestPitchName READ lowestPitchName NOTIFY changed)
    Q_PROPERTY(QString highestPitchName READ highestPitchName NOTIFY changed)
    Q_PROPERTY(bool writtenPitch READ writtenPitch NOTIFY changed)
    Q_PROPERTY(bool useLength READ useLength NOTIFY changed)
    Q_PROPERTY(QVariantList lengthOptions READ lengthOptions CONSTANT)
    Q_PROPERTY(int shortestIndex READ shortestIndex NOTIFY changed)
    Q_PROPERTY(int longestIndex READ longestIndex NOTIFY changed)
    Q_PROPERTY(bool skipTied READ skipTied NOTIFY changed)
    Q_PROPERTY(int rangeMode READ rangeMode NOTIFY changed)   // 0 whole score, 1 bars, 2 current selection
    Q_PROPERTY(int fromBar READ fromBar NOTIFY changed)
    Q_PROPERTY(int toBar READ toBar NOTIFY changed)
    Q_PROPERTY(int barCount READ barCount NOTIFY changed)

    Q_PROPERTY(int matchCount READ matchCount NOTIFY changed)
    Q_PROPERTY(QStringList savedFilters READ savedFilters NOTIFY savedFiltersChanged)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::GlobalInject<muse::IGlobalConfiguration> globalConfiguration;

public:
    explicit FindSelectModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();

    QVariantList kindOptions() const;
    QVariantList selectedKinds() const;
    QVariantList staffOptions() const;
    QVariantList selectedStaves() const;
    QVariantList articulationOptions() const;
    QString articulation() const { return m_articulation; }
    int voices() const { return int(m_query.voices); }
    bool usePitch() const { return m_usePitch; }
    int lowestPitch() const { return m_lowest; }
    int highestPitch() const { return m_highest; }
    QString lowestPitchName() const;
    QString highestPitchName() const;
    bool writtenPitch() const { return m_query.writtenPitch; }
    bool useLength() const { return m_useLength; }
    QVariantList lengthOptions() const;
    int shortestIndex() const { return m_shortestIndex; }
    int longestIndex() const { return m_longestIndex; }
    bool skipTied() const { return m_query.skipTiedContinuations; }
    int rangeMode() const { return m_rangeMode; }
    int fromBar() const { return m_fromBar; }
    int toBar() const { return m_toBar; }
    int barCount() const;
    int matchCount() const { return m_matchCount; }
    QStringList savedFilters() const;

    Q_INVOKABLE void setKind(const QString& kind, bool on);
    Q_INVOKABLE void setStaff(int staffIdx, bool on);
    Q_INVOKABLE void setAllStaves();
    Q_INVOKABLE void setVoice(int voice, bool on);
    Q_INVOKABLE void setArticulation(const QString& name);
    Q_INVOKABLE void setUsePitch(bool on);
    Q_INVOKABLE void setLowestPitch(int pitch);
    Q_INVOKABLE void setHighestPitch(int pitch);
    Q_INVOKABLE void setWrittenPitch(bool on);
    Q_INVOKABLE void setUseLength(bool on);
    Q_INVOKABLE void setShortestIndex(int i);
    Q_INVOKABLE void setLongestIndex(int i);
    Q_INVOKABLE void setSkipTied(bool on);
    Q_INVOKABLE void setRangeMode(int mode);
    Q_INVOKABLE void setFromBar(int bar);
    Q_INVOKABLE void setToBar(int bar);

    //! Selects every match in the score; returns how many
    Q_INVOKABLE int selectMatches();

    Q_INVOKABLE void saveFilter(const QString& name);
    Q_INVOKABLE void loadFilter(const QString& name);
    Q_INVOKABLE void deleteFilter(const QString& name);

signals:
    void changed();
    void savedFiltersChanged();

private:
    engraving::Score* score() const;
    jubelle::FindQuery effectiveQuery() const;
    void update();
    QString filtersPath() const;
    QVariantMap readFilters() const;
    void writeFilters(const QVariantMap& filters);
    void fromQuery(const jubelle::FindQuery& q);

    jubelle::FindQuery m_query;
    QString m_articulation;
    bool m_usePitch = false;
    int m_lowest = 60;
    int m_highest = 84;
    bool m_useLength = false;
    int m_shortestIndex = 0;
    int m_longestIndex = 6;
    int m_rangeMode = 0;
    int m_fromBar = 1;
    int m_toBar = 1;
    int m_matchCount = 0;
};
}
