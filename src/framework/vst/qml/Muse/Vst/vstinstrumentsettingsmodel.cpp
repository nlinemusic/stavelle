/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */
#include "vstinstrumentsettingsmodel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "internal/jubelle/vstkeyswitches.h"
#include "translation.h"

using namespace muse::vst;

static constexpr const char* BEND_SETTING = "jubelle.bendRange";
static constexpr const char* MIDDLE_C_SETTING = "jubelle.middleC";   // "C4" (default) or "C3": how note names are read

VstInstrumentSettingsModel::VstInstrumentSettingsModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void VstInstrumentSettingsModel::setInstanceId(int id)
{
    if (m_instanceId == id) {
        return;
    }
    m_instanceId = id;
    emit instanceIdChanged();
    emit changed();
}

std::string VstInstrumentSettingsModel::extra(const char* key) const
{
    IVstPluginInstancePtr instance = instancesRegister() ? instancesRegister()->instanceById(m_instanceId) : nullptr;
    if (!instance) {
        return std::string();
    }
    const auto extras = instance->jubelleExtras();
    auto it = extras.find(key);
    return it != extras.end() ? it->second : std::string();
}

void VstInstrumentSettingsModel::setExtra(const char* key, const std::string& value)
{
    IVstPluginInstancePtr instance = instancesRegister() ? instancesRegister()->instanceById(m_instanceId) : nullptr;
    if (!instance) {
        return;
    }
    instance->jubelleSetExtra(key, value);
    emit changed();
}

double VstInstrumentSettingsModel::trimDb() const
{
    return jubelle::parseTrimDb(extra(jubelle::TRIM_SETTING));
}

void VstInstrumentSettingsModel::setTrimDb(double db)
{
    const double clamped = jubelle::parseTrimDb(std::to_string(db));
    if (qFuzzyCompare(clamped + 100.0, trimDb() + 100.0)) {
        return;
    }
    setExtra(jubelle::TRIM_SETTING, std::to_string(clamped));
}

double VstInstrumentSettingsModel::bendRange() const
{
    const std::string v = extra(BEND_SETTING);
    const double r = v.empty() ? 2.0 : std::atof(v.c_str());
    return r >= 1.0 && r <= 48.0 ? r : 2.0;
}

void VstInstrumentSettingsModel::setBendRange(double semitones)
{
    const int r = std::clamp(int(std::lround(semitones)), 1, 48);
    if (r == int(bendRange())) {
        return;
    }
    setExtra(BEND_SETTING, std::to_string(r));
}

bool VstInstrumentSettingsModel::middleCIsC4() const
{
    return extra(MIDDLE_C_SETTING) != "C3";
}

void VstInstrumentSettingsModel::setMiddleCIsC4(bool c4)
{
    if (c4 == middleCIsC4()) {
        return;
    }
    setExtra(MIDDLE_C_SETTING, c4 ? "C4" : "C3");
}

QVariantList VstInstrumentSettingsModel::keySwitches() const
{
    QVariantList result;
    const jubelle::KeySwitchMap map = jubelle::parseKeySwitches(extra(jubelle::KEYSWITCHES_SETTING));
    const bool c4 = middleCIsC4();
    for (const std::string& g : jubelle::keySwitchGroups()) {
        QVariantMap m;
        m["group"] = QString::fromStdString(g);
        m["title"] = muse::qtrc("vst", jubelle::keySwitchGroupTitle(g).c_str());
        auto it = map.keys.find(g);
        m["note"] = it != map.keys.end() ? QString::fromStdString(jubelle::midiToNoteName(it->second, c4)) : QString();
        result << m;
    }
    return result;
}

bool VstInstrumentSettingsModel::setKeySwitch(const QString& group, const QString& note)
{
    jubelle::KeySwitchMap map = jubelle::parseKeySwitches(extra(jubelle::KEYSWITCHES_SETTING));
    const std::string g = group.toStdString();
    if (note.trimmed().isEmpty()) {
        map.keys.erase(g);
    } else {
        const int midi = jubelle::noteNameToMidi(note.toStdString(), middleCIsC4());
        if (midi < 0) {
            return false;
        }
        map.keys[g] = midi;
    }
    setExtra(jubelle::KEYSWITCHES_SETTING, jubelle::keySwitchesToString(map));
    return true;
}

void VstInstrumentSettingsModel::clearKeySwitches()
{
    setExtra(jubelle::KEYSWITCHES_SETTING, std::string());
}
