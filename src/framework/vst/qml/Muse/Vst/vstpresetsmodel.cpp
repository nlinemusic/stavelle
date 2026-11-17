/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle: the Presets bar of a VST3 plugin's window. See vstpresetsmodel.h.
 */
#include "vstpresetsmodel.h"

#include <QFileInfo>
#include <QVariantMap>

#include "internal/jubelle/vstpresetfile.h"
#include "translation.h"
#include "log.h"

using namespace muse;
using namespace muse::vst;

namespace {
std::filesystem::path toFsPath(const QString& s)
{
#ifdef _WIN32
    return std::filesystem::path(s.toStdWString());
#else
    return std::filesystem::path(s.toStdString());
#endif
}

QString fromFsPath(const std::filesystem::path& p)
{
#ifdef _WIN32
    return QString::fromStdWString(p.wstring());
#else
    return QString::fromStdString(p.string());
#endif
}
}

VstPresetsModel::VstPresetsModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void VstPresetsModel::setInstanceId(int id)
{
    if (m_instanceId == id) {
        return;
    }
    m_instanceId = id;
    emit instanceIdChanged();
    reload();
}

void VstPresetsModel::setMessage(const QString& m)
{
    if (m_message != m) {
        m_message = m;
        emit messageChanged();
    }
}

void VstPresetsModel::reload()
{
    m_presets.clear();

    IVstPluginInstancePtr instance = instancesRegister() ? instancesRegister()->instanceById(m_instanceId) : nullptr;
    if (instance) {
        std::filesystem::path appFolder;
        if (globalConfiguration()) {
            appFolder = toFsPath(globalConfiguration()->appBinDirPath().toQString()).parent_path();
        }
        const auto roots = jubelle::presetRoots(appFolder);
        const auto found = jubelle::findPresets(roots, instance->jubellePluginVendor(), instance->jubellePluginName(),
                                                instance->jubellePluginClassId());
        for (const auto& p : found) {
            QVariantMap item;
            const QString title = QString::fromStdString(p.title);
            const QString group = QString::fromStdString(p.group);
            item["title"] = group.isEmpty() ? title : group + " › " + title;
            item["path"] = fromFsPath(p.path);
            m_presets << item;
        }
        if (m_presets.isEmpty()) {
            setMessage(muse::qtrc("vst", "No preset files found for this plugin. Its own window may still have presets of its own."));
        } else {
            setMessage(QString());
        }
    }
    emit presetsChanged();
}

bool VstPresetsModel::applyFile(const QString& path, const QString& title)
{
    IVstPluginInstancePtr instance = instancesRegister() ? instancesRegister()->instanceById(m_instanceId) : nullptr;
    if (!instance) {
        return false;
    }
    const auto data = jubelle::readVstPresetFile(toFsPath(path));
    if (!data) {
        setMessage(muse::qtrc("vst", "This file isn’t a VST3 preset."));
        return false;
    }
    const std::string classId = instance->jubellePluginClassId();
    if (!classId.empty() && !jubelle::sameClassId(data->classId, classId)) {
        setMessage(muse::qtrc("vst", "This preset is for a different plugin."));
        return false;
    }
    if (!instance->jubelleApplyPresetStates(data->componentState, data->controllerState)) {
        setMessage(muse::qtrc("vst", "The plugin didn’t accept this preset."));
        return false;
    }
    m_currentTitle = title;
    emit currentPresetChanged();
    setMessage(QString());
    LOGI() << "Jubelle: loaded VST3 preset " << path.toStdString();
    return true;
}

void VstPresetsModel::applyPreset(int index)
{
    if (index < 0 || index >= m_presets.size()) {
        return;
    }
    const QVariantMap item = m_presets.at(index).toMap();
    applyFile(item.value("path").toString(), item.value("title").toString());
}

void VstPresetsModel::openPresetFile()
{
    if (!interactive()) {
        return;
    }
    const std::vector<std::string> filter { muse::trc("vst", "VST3 presets") + " (*.vstpreset)" };
    io::path_t dir;
    const auto roots = jubelle::presetRoots();
    if (!roots.empty()) {
        dir = io::path_t(fromFsPath(roots.front()));
    }
    const io::path_t file = interactive()->selectOpeningFileSync(muse::trc("vst", "Open VST3 preset"), dir, filter);
    if (file.empty()) {
        return;
    }
    const QString path = file.toQString();
    applyFile(path, QFileInfo(path).completeBaseName());
}
