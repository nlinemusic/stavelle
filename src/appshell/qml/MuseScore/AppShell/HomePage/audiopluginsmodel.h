/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "audioplugins/iknownaudiopluginsregister.h"
#include "audioplugins/iregisteraudiopluginsscenario.h"
#include "audioplugins/iaudiopluginsconfiguration.h"

namespace mu::appshell {
//! Jubelle: audio plugins (VST3 instruments and effects) on the Plugins page.
//! Built on MuseScore's own plugin register: it already records each plugin,
//! whether it loaded and, if not, the error. This adds a plain explanation of
//! each error, switching a plugin off and on (kept across rescans), and Rescan.
class AudioPluginsModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY changed)
    Q_PROPERTY(int failedCount READ failedCount NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<muse::audioplugins::IKnownAudioPluginsRegister> knownPlugins = { this };
    muse::ContextInject<muse::audioplugins::IRegisterAudioPluginsScenario> registerScenario = { this };
    muse::GlobalInject<muse::audioplugins::IAudioPluginsConfiguration> pluginsConfiguration;

public:
    explicit AudioPluginsModel(QObject* parent = nullptr);

    QVariantList plugins() const { return m_plugins; }
    int failedCount() const { return m_failedCount; }

    Q_INVOKABLE void load();
    Q_INVOKABLE void setSearch(const QString& text);
    Q_INVOKABLE void setPluginEnabled(const QString& resourceId, bool enabled);
    Q_INVOKABLE void rescan();

    //! The plain-language reason a plugin could not be loaded (also used by the tests)
    static QString errorText(int errorCode);

signals:
    void changed();

private:
    void rebuild();

    QVariantList m_plugins;
    QString m_search;
    int m_failedCount = 0;
};
}
