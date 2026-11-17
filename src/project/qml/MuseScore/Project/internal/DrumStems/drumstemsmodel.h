/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "global/iglobalconfiguration.h"
#include "global/iinteractive.h"

#include "project/inotationwritersregister.h"
#include "notation/internal/jubelle/drumstems.h"

namespace mu::engraving {
class Part;
class Score;
}

namespace mu::project {
//! Jubelle G9: File › Export drum stems…  One audio file per drum of a kit (kick, snare, hi-hat…),
//! or per group of drums you put together. Groups can be saved as presets.
class DrumStemsModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(bool hasKit READ hasKit NOTIFY changed)
    Q_PROPERTY(QVariantList kits READ kits NOTIFY changed)
    Q_PROPERTY(int kitIndex READ kitIndex NOTIFY changed)
    Q_PROPERTY(QVariantList components READ components NOTIFY changed)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
    Q_PROPERTY(QVariantList groupChoices READ groupChoices NOTIFY changed)
    Q_PROPERTY(bool exportEach READ exportEach NOTIFY changed)
    Q_PROPERTY(bool exportGroups READ exportGroups NOTIFY changed)
    Q_PROPERTY(QVariantList formats READ formats CONSTANT)
    Q_PROPERTY(int formatIndex READ formatIndex NOTIFY changed)
    Q_PROPERTY(QString folder READ folder NOTIFY changed)
    Q_PROPERTY(QStringList presets READ presets NOTIFY changed)
    Q_PROPERTY(bool exporting READ exporting NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY changed)

    QML_ELEMENT

    muse::GlobalInject<muse::IGlobalConfiguration> globalConfiguration;
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<INotationWritersRegister> writers = { this };

public:
    explicit DrumStemsModel(QObject* parent = nullptr);

    bool hasKit() const { return !m_kits.empty(); }
    QVariantList kits() const;
    int kitIndex() const { return m_kitIndex; }
    QVariantList components() const;
    QVariantList groups() const;
    QVariantList groupChoices() const;
    bool exportEach() const { return m_exportEach; }
    bool exportGroups() const { return m_exportGroups; }
    QVariantList formats() const;
    int formatIndex() const { return m_formatIndex; }
    QString folder() const { return m_folder; }
    QStringList presets() const;
    bool exporting() const { return m_exporting; }
    QString status() const { return m_status; }
    int fileCount() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void setKitIndex(int index);
    Q_INVOKABLE void setExportEach(bool on);
    Q_INVOKABLE void setExportGroups(bool on);
    Q_INVOKABLE void setFormatIndex(int index);
    Q_INVOKABLE void chooseFolder();

    Q_INVOKABLE void addGroup(const QString& name);
    Q_INVOKABLE void renameGroup(int index, const QString& name);
    Q_INVOKABLE void removeGroup(int index);
    //! Puts a drum into a group (-1: into none). A drum is in one group at most.
    Q_INVOKABLE void moveToGroup(int componentId, int groupIndex);
    Q_INVOKABLE void resetGroups();

    Q_INVOKABLE void savePreset(const QString& name);
    Q_INVOKABLE void loadPreset(const QString& name);
    Q_INVOKABLE void deletePreset(const QString& name);

    Q_INVOKABLE void exportStems();

signals:
    void changed();

private:
    struct Output {
        QString name;
        std::set<int> pitches;
    };

    engraving::Score* score() const;
    engraving::Part* kit() const;
    void readKit();
    std::vector<Output> outputs() const;
    int groupOf(int componentId) const;
    QString presetsPath() const;
    std::map<QString, QString> readPresets() const;
    void writePresets(const std::map<QString, QString>& presets) const;
    bool exportOne(const Output& output, const QString& path);

    std::vector<engraving::Part*> m_kits;
    int m_kitIndex = 0;
    std::vector<notation::jubelle::DrumComponent> m_components;
    std::vector<notation::jubelle::StemGroup> m_groups;
    bool m_exportEach = true;
    bool m_exportGroups = true;
    int m_formatIndex = 0;
    QString m_folder;
    bool m_exporting = false;
    QString m_status;
};
}
