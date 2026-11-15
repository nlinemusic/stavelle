/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle: the Presets bar of a VST3 instrument's or effect's window.
 * Lists the plugin's .vstpreset files from the standard VST3 preset folders
 * and loads one into the plugin; the result is saved with the score like any
 * other change to the plugin's settings.
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "global/modularity/ioc.h"
#include "global/iinteractive.h"
#include "global/iglobalconfiguration.h"
#include "ivstinstancesregister.h"

namespace muse::vst {
class VstPresetsModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(int instanceId READ instanceId WRITE setInstanceId NOTIFY instanceIdChanged FINAL)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged FINAL)
    Q_PROPERTY(QString currentPresetTitle READ currentPresetTitle NOTIFY currentPresetChanged FINAL)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged FINAL)

    QML_ELEMENT

    muse::ContextInject<IVstInstancesRegister> instancesRegister { this };
    muse::ContextInject<IInteractive> interactive { this };
    muse::GlobalInject<IGlobalConfiguration> globalConfiguration;

public:
    explicit VstPresetsModel(QObject* parent = nullptr);

    int instanceId() const { return m_instanceId; }
    void setInstanceId(int id);

    QVariantList presets() const { return m_presets; }
    QString currentPresetTitle() const { return m_currentTitle; }
    QString message() const { return m_message; }

    //! Looks for this plugin's presets again
    Q_INVOKABLE void reload();
    //! Loads the preset at `index` in `presets`
    Q_INVOKABLE void applyPreset(int index);
    //! Lets you choose any .vstpreset file
    Q_INVOKABLE void openPresetFile();

signals:
    void instanceIdChanged();
    void presetsChanged();
    void currentPresetChanged();
    void messageChanged();

private:
    bool applyFile(const QString& path, const QString& title);
    void setMessage(const QString& m);

    int m_instanceId = -1;
    QVariantList m_presets;
    QString m_currentTitle;
    QString m_message;
};
}
