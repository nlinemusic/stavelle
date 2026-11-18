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
#include "project/iprojectconfiguration.h"

#include <QSet>
#include <QStringList>

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
    //! Jubelle: which instruments to show: -1 every instrument, -2 your own selection, 0... an ensemble preset
    Q_PROPERTY(int preset READ preset WRITE setPreset NOTIFY changed)
    //! {name} per ensemble preset (read from the installed templates)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<notation::IInstrumentsRepository> instrumentsRepository = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::GlobalInject<project::IProjectConfiguration> projectConfiguration;

public:
    explicit LearningCenterModel(QObject* parent = nullptr);

    QVariantList instruments() const { return m_instruments; }
    QString search() const { return m_search; }
    bool onlyTransposing() const { return m_onlyTransposing; }
    int instrumentCount() const { return m_instrumentCount; }
    int preset() const { return m_preset; }
    void setPreset(int preset);
    QVariantList presets() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void setSearch(const QString& text);
    Q_INVOKABLE void setOnlyTransposing(bool on);
    Q_INVOKABLE void openShortcuts();
    //! Folds or unfolds a family
    Q_INVOKABLE void toggleFamily(const QString& family);
    Q_INVOKABLE void setAllFamiliesCollapsed(bool collapsed);
    //! Adds an instrument to, or removes it from, your own selection (kept between sessions)
    Q_INVOKABLE void toggleSelected(const QString& instrumentId);

signals:
    void changed();

private:
    void rebuild();
    void loadPresets();
    void loadSelection();
    void saveSelection();

    struct Preset {
        QString name;
        QSet<QString> ids;
    };
    std::vector<Preset> m_presets;
    bool m_presetsLoaded = false;
    QSet<QString> m_selection;
    bool m_selectionLoaded = false;
    QSet<QString> m_collapsed;
    int m_preset = -1;
    int m_instrumentCount = 0;

    QVariantList m_instruments;
    QString m_search;
    bool m_onlyTransposing = false;
};
}
