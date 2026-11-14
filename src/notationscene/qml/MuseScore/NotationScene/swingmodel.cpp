/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "swingmodel.h"

#include <cmath>

#include "translation.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"

#include "notation/inotation.h"
#include "notation/inotationelements.h"
#include "notation/internal/inotationundostack.h"

using namespace mu::notation;
using namespace mu::engraving;
using namespace mu::engraving::jubelle;

SwingModel::SwingModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QString SwingModel::displayName(const SwingFeel& feel)
{
    if (!feel.builtIn) {
        return feel.name.toQString();
    }
    // Built-in names are translated when shown, never when stored
    return muse::qtrc("notation", feel.name.toStdString().c_str());
}

// Built-in names, for the translators
[[maybe_unused]] static const char* JUBELLE_SWING_BUILT_IN_NAMES[] = {
    QT_TRANSLATE_NOOP("notation", "Straight"),
    QT_TRANSLATE_NOOP("notation", "Light swing"),
    QT_TRANSLATE_NOOP("notation", "Medium swing"),
    QT_TRANSLATE_NOOP("notation", "Heavy swing"),
    QT_TRANSLATE_NOOP("notation", "Swing 2:1 (triplet)"),
    QT_TRANSLATE_NOOP("notation", "Swing 3:1 (dotted)"),
    QT_TRANSLATE_NOOP("notation", "Basie swing"),
};

Score* SwingModel::score() const
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    return notation && notation->elements() ? notation->elements()->msScore() : nullptr;
}

void SwingModel::load()
{
    m_instruments.clear();
    Score* s = score();
    if (s && s->masterScore()) {
        for (const Part* part : s->masterScore()->parts()) {
            m_instruments.push_back({ part->partName().toQString(), part->id().toUint64() });
        }
        m_original = SwingSettings::read(s);
    } else {
        m_original = SwingSettings();
    }
    m_settings = m_original;
    m_editedFeelIndex = 0;
    emit changed();
}

void SwingModel::touch()
{
    setResult(QString());
    emit changed();
}

QVariantList SwingModel::feelChoices() const
{
    QVariantList list;
    for (const SwingFeel& f : m_settings.allFeels()) {
        QVariantMap m;
        m["text"] = displayName(f);
        m["id"] = f.id.toQString();
        list << m;
    }
    return list;
}

int SwingModel::scoreFeelIndex() const
{
    // 0 = not set; 1.. = feels
    const std::vector<SwingFeel> feels = m_settings.allFeels();
    for (size_t i = 0; i < feels.size(); ++i) {
        if (feels[i].id == m_settings.scoreFeel) {
            return int(i) + 1;
        }
    }
    return 0;
}

void SwingModel::setScoreFeelIndex(int index)
{
    const std::vector<SwingFeel> feels = m_settings.allFeels();
    m_settings.scoreFeel = (index >= 1 && index <= int(feels.size())) ? feels[index - 1].id : muse::String();
    touch();
}

QVariantList SwingModel::instruments() const
{
    const std::vector<SwingFeel> feels = m_settings.allFeels();
    QVariantList list;
    for (const Instrument& inst : m_instruments) {
        int choice = 0;   // 0 = same as whole score
        auto it = m_settings.partFeels.find(inst.partId);
        if (it != m_settings.partFeels.end()) {
            for (size_t i = 0; i < feels.size(); ++i) {
                if (feels[i].id == it->second) {
                    choice = int(i) + 1;
                }
            }
        }
        QVariantMap m;
        m["name"] = inst.name;
        m["choice"] = choice;
        list << m;
    }
    return list;
}

void SwingModel::setInstrumentFeel(int row, int choiceIndex)
{
    if (row < 0 || row >= int(m_instruments.size())) {
        return;
    }
    const std::vector<SwingFeel> feels = m_settings.allFeels();
    const uint64_t partId = m_instruments[row].partId;
    if (choiceIndex >= 1 && choiceIndex <= int(feels.size())) {
        m_settings.partFeels[partId] = feels[choiceIndex - 1].id;
    } else {
        m_settings.partFeels.erase(partId);
    }
    touch();
}

QVariantList SwingModel::editableFeels() const
{
    return feelChoices();
}

int SwingModel::editedFeelIndex() const
{
    return m_editedFeelIndex;
}

void SwingModel::setEditedFeelIndex(int index)
{
    if (index < 0 || index >= int(m_settings.allFeels().size()) || index == m_editedFeelIndex) {
        return;
    }
    m_editedFeelIndex = index;
    emit changed();
}

QVariantMap SwingModel::editedFeel() const
{
    const std::vector<SwingFeel> feels = m_settings.allFeels();
    if (m_editedFeelIndex >= int(feels.size())) {
        return {};
    }
    const SwingFeel& f = feels[m_editedFeelIndex];
    bool edited = false;
    for (const SwingFeel& b : builtInFeels()) {
        if (b.id == f.id) {
            edited = !(b == f);
        }
    }
    auto percent = [](double r) { return QString::number(SwingFeel::ratioToPercent(r), 'f', 1) + "%"; };

    QVariantMap m;
    m["name"] = displayName(f);
    m["builtIn"] = f.builtIn;
    m["edited"] = edited;
    m["unit"] = f.unit;
    m["tempoDependent"] = f.tempoDependent;
    m["lowTempo"] = f.lowTempo;
    m["highTempo"] = f.highTempo;
    m["lowTempoRatio"] = f.lowTempoRatio;
    m["highTempoRatio"] = f.highTempoRatio;
    m["ratio"] = f.ratio;
    m["lowTempoPercent"] = percent(f.lowTempoRatio);
    m["highTempoPercent"] = percent(f.highTempoRatio);
    m["ratioPercent"] = percent(f.ratio);
    m["offbeatLoudness"] = f.offbeatLoudness;
    m["offbeatLength"] = f.offbeatLength;
    m["isStraight"] = f.id == u"straight";
    return m;
}

void SwingModel::setFeelValue(const QString& key, const QVariant& value)
{
    std::vector<SwingFeel> feels = m_settings.allFeels();
    if (m_editedFeelIndex >= int(feels.size())) {
        return;
    }
    SwingFeel f = feels[m_editedFeelIndex];
    if (key == "name" && !f.builtIn) {
        const QString name = value.toString().trimmed();
        if (name.isEmpty()) {
            return;
        }
        f.name = muse::String::fromQString(name);
    } else if (key == "unit") {
        f.unit = value.toInt();
    } else if (key == "tempoDependent") {
        f.tempoDependent = value.toBool();
    } else if (key == "lowTempo") {
        f.lowTempo = value.toDouble();
    } else if (key == "highTempo") {
        f.highTempo = value.toDouble();
    } else if (key == "lowTempoRatio") {
        f.lowTempoRatio = value.toDouble();
    } else if (key == "highTempoRatio") {
        f.highTempoRatio = value.toDouble();
    } else if (key == "ratio") {
        f.ratio = value.toDouble();
    } else if (key == "offbeatLoudness") {
        f.offbeatLoudness = value.toInt();
    } else if (key == "offbeatLength") {
        f.offbeatLength = value.toInt();
    } else {
        return;
    }
    m_settings.setFeel(f);
    touch();
}

void SwingModel::newCustomFeel()
{
    std::vector<SwingFeel> feels = m_settings.allFeels();
    SwingFeel f = m_editedFeelIndex < int(feels.size()) ? feels[m_editedFeelIndex] : builtInFeels().front();
    const QString baseName = displayName(f);
    f.id = m_settings.newCustomId();
    f.builtIn = false;
    f.name = muse::String::fromQString(muse::qtrc("notation", "%1 (my version)").arg(baseName));
    m_settings.setFeel(f);

    feels = m_settings.allFeels();
    for (size_t i = 0; i < feels.size(); ++i) {
        if (feels[i].id == f.id) {
            m_editedFeelIndex = int(i);
        }
    }
    touch();
}

void SwingModel::deleteEditedFeel()
{
    const std::vector<SwingFeel> feels = m_settings.allFeels();
    if (m_editedFeelIndex >= int(feels.size()) || feels[m_editedFeelIndex].builtIn) {
        return;
    }
    const muse::String id = feels[m_editedFeelIndex].id;
    m_settings.removeFeel(id);
    // Anything that used it goes back to "not set"
    if (m_settings.scoreFeel == id) {
        m_settings.scoreFeel.clear();
    }
    for (auto it = m_settings.partFeels.begin(); it != m_settings.partFeels.end();) {
        it = it->second == id ? m_settings.partFeels.erase(it) : std::next(it);
    }
    m_editedFeelIndex = 0;
    touch();
}

void SwingModel::restoreEditedFeel()
{
    const std::vector<SwingFeel> feels = m_settings.allFeels();
    if (m_editedFeelIndex >= int(feels.size()) || !feels[m_editedFeelIndex].builtIn) {
        return;
    }
    m_settings.removeFeel(feels[m_editedFeelIndex].id);
    touch();
}

bool SwingModel::hasChanges() const
{
    return m_settings.toText() != m_original.toText();
}

QString SwingModel::resultText() const
{
    return m_resultText;
}

void SwingModel::setResult(const QString& text)
{
    if (m_resultText != text) {
        m_resultText = text;
        emit resultChanged();
    }
}

bool SwingModel::apply()
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* s = score();
    if (!notation || !s || !hasChanges()) {
        return false;
    }
    notation->undoStack()->prepareChanges(muse::TranslatableString("undoableAction", "Change swing"));
    m_settings.write(s);
    notation->undoStack()->commitChanges();

    m_original = SwingSettings::read(s);
    m_settings = m_original;
    emit changed();
    setResult(muse::qtrc("notation", "Applied. Playback uses the new swing; the written notes are unchanged. Undo with Ctrl+Z (⌘Z on Mac)."));
    return true;
}
