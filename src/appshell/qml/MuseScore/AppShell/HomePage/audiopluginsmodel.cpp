/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */
#include "audiopluginsmodel.h"

#include <algorithm>

#include "audio/common/audiotypes.h"
#include "translation.h"

using namespace mu::appshell;
using namespace muse::audioplugins;

AudioPluginsModel::AudioPluginsModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void AudioPluginsModel::load()
{
    if (knownPlugins()) {
        knownPlugins()->pluginInfoListChanged().onNotify(this, [this]() { rebuild(); });
    }
    rebuild();
}

void AudioPluginsModel::setSearch(const QString& text)
{
    m_search = text.trimmed();
    rebuild();
}

QString AudioPluginsModel::errorText(int code)
{
    // The code is what the separate checking process returned when it tried to load the plugin
    if (code == 0) {
        return QString();
    }
    if (code == 351) {
        return muse::qtrc("appshell", "Jubelle can't read this file as a VST3 plugin. It may be damaged or of another kind.");
    }
    const unsigned u = static_cast<unsigned>(code);
    if (u == 0xC0000005u || u == 0xC0000409u || u == 0xC000001Du || code == 139 || code == 134 || code == 11 || code == 6) {
        return muse::qtrc("appshell", "The plugin stopped Jubelle's checking process while loading (error %1). "
                                      "Jubelle itself was not affected. Try updating the plugin.").arg(code);
    }
    if (u == 0xC0000135u || u == 0xC000007Bu || u == 0xC0000139u) {
        return muse::qtrc("appshell", "The plugin is missing a part it needs, or is built for another kind of computer "
                                      "(for example 32-bit) (error %1). Reinstall it with its own installer.").arg(code);
    }
    return muse::qtrc("appshell", "The plugin could not be loaded (error %1). It may need its own installer, "
                                  "an authorisation from its maker, or a newer version.").arg(code);
}

void AudioPluginsModel::rebuild()
{
    m_plugins.clear();
    m_failedCount = 0;
    if (!knownPlugins()) {
        emit changed();
        return;
    }
    AudioPluginInfoList list = knownPlugins()->pluginInfoList();
    std::sort(list.begin(), list.end(), [](const AudioPluginInfo& a, const AudioPluginInfo& b) {
        return QString::fromStdString(a.meta.id).compare(QString::fromStdString(b.meta.id), Qt::CaseInsensitive) < 0;
    });
    for (const AudioPluginInfo& info : list) {
        const QString id = QString::fromStdString(info.meta.id);
        const QString vendor = QString::fromStdString(info.meta.vendor);
        if (!m_search.isEmpty() && !id.contains(m_search, Qt::CaseInsensitive) && !vendor.contains(m_search, Qt::CaseInsensitive)) {
            continue;
        }
        const bool failed = info.errorCode != 0 || (!info.enabled && info.type == AudioPluginType::Undefined);
        const bool userOff = pluginsConfiguration() && pluginsConfiguration()->isUserDisabled(info.meta.id);
        QVariantMap m;
        m["id"] = id;
        m["vendor"] = vendor;
        m["path"] = info.path.toQString();
        m["kind"] = info.type == AudioPluginType::Instrument ? muse::qtrc("appshell", "Instrument")
                    : info.type == AudioPluginType::Fx ? muse::qtrc("appshell", "Effect") : muse::qtrc("appshell", "Unknown");
        m["failed"] = failed;
        m["error"] = failed ? errorText(info.errorCode ? info.errorCode : -1) : QString();
        m["enabled"] = !failed && info.enabled && !userOff;
        m["canToggle"] = !failed;
        m_plugins << m;
        if (failed) {
            ++m_failedCount;
        }
    }
    emit changed();
}

void AudioPluginsModel::setPluginEnabled(const QString& resourceId, bool enabled)
{
    if (!pluginsConfiguration()) {
        return;
    }
    pluginsConfiguration()->setUserDisabled(resourceId.toStdString(), !enabled);
    rebuild();
}

void AudioPluginsModel::rescan()
{
    if (registerScenario()) {
        registerScenario()->rescanAllPlugins();
    }
    rebuild();
}
