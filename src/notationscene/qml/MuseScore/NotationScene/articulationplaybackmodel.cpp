/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "articulationplaybackmodel.h"

#include <cstring>

#include <QVariantMap>

#include "translation.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"

#include "notation/inotation.h"
#include "notation/inotationelements.h"
#include "notation/internal/inotationundostack.h"

using namespace mu::notation;
using namespace mu::engraving;
using namespace mu::engraving::jubelle;

namespace {
struct ParamText {
    ArtParam param;
    const char* group;
    const char* title;
    const char* unit;
};

// Display order and wording
const std::vector<ParamText>& paramTexts()
{
    static const std::vector<ParamText> texts = {
        { ArtParam::StaccatoLength,      QT_TRANSLATE_NOOP("notation", "Note length"),      QT_TRANSLATE_NOOP("notation", "Staccato"), "%" },
        { ArtParam::StaccatissimoLength, QT_TRANSLATE_NOOP("notation", "Note length"),      QT_TRANSLATE_NOOP("notation", "Staccatissimo"), "%" },
        { ArtParam::TenutoLength,        QT_TRANSLATE_NOOP("notation", "Note length"),      QT_TRANSLATE_NOOP("notation", "Tenuto"), "%" },
        { ArtParam::MarcatoLength,       QT_TRANSLATE_NOOP("notation", "Note length"),      QT_TRANSLATE_NOOP("notation", "Marcato (rooftop accent)"), "%" },
        { ArtParam::AccentLoudness,      QT_TRANSLATE_NOOP("notation", "Loudness"),         QT_TRANSLATE_NOOP("notation", "Accent: louder by"), "%" },
        { ArtParam::MarcatoLoudness,     QT_TRANSLATE_NOOP("notation", "Loudness"),         QT_TRANSLATE_NOOP("notation", "Marcato: louder by"), "%" },
        { ArtParam::FallStart,           QT_TRANSLATE_NOOP("notation", "Falls and doits"),  QT_TRANSLATE_NOOP("notation", "Starts at"), "%" },
        { ArtParam::FallRange,           QT_TRANSLATE_NOOP("notation", "Falls and doits"),  QT_TRANSLATE_NOOP("notation", "Range"), "semitones" },
        { ArtParam::FallCurve,           QT_TRANSLATE_NOOP("notation", "Falls and doits"),  QT_TRANSLATE_NOOP("notation", "Speed"), "curve" },
        { ArtParam::ScoopLength,         QT_TRANSLATE_NOOP("notation", "Scoops and plops"), QT_TRANSLATE_NOOP("notation", "Length before the note"), "%" },
        { ArtParam::ScoopRange,          QT_TRANSLATE_NOOP("notation", "Scoops and plops"), QT_TRANSLATE_NOOP("notation", "Range"), "semitones" },
        { ArtParam::GlissandoStart,      QT_TRANSLATE_NOOP("notation", "Glissandos"),       QT_TRANSLATE_NOOP("notation", "Slide starts at"), "%" },
        { ArtParam::GlissandoCurve,      QT_TRANSLATE_NOOP("notation", "Glissandos"),       QT_TRANSLATE_NOOP("notation", "Speed"), "curve" },
        { ArtParam::TurnSpeed,           QT_TRANSLATE_NOOP("notation", "Jazz turn"),        QT_TRANSLATE_NOOP("notation", "Speed"), "% of normal" },
    };
    return texts;
}

std::optional<ArtParam> paramForKey(const QString& key)
{
    for (const ArtParamInfo& info : artParams()) {
        if (key == QString::fromLatin1(info.key)) {
            return info.param;
        }
    }
    return std::nullopt;
}
}

ArticulationPlaybackModel::ArticulationPlaybackModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

Score* ArticulationPlaybackModel::score() const
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    if (!notation || !notation->elements()) {
        return nullptr;
    }
    return notation->elements()->msScore();
}

void ArticulationPlaybackModel::load()
{
    m_scopes.clear();
    m_scopes.push_back({ muse::qtrc("notation", "Whole score (all instruments)"), 0 });

    Score* s = score();
    if (s && s->masterScore()) {
        for (const Part* part : s->masterScore()->parts()) {
            m_scopes.push_back({ part->partName().toQString(), part->id().toUint64() });
        }
        m_original = ArticulationPlaybackSettings::read(s);
    } else {
        m_original = ArticulationPlaybackSettings();
    }
    m_settings = m_original;
    m_currentScope = 0;

    emit scopesChanged();
    emit currentScopeChanged();
    emit paramsChanged();
}

QVariantList ArticulationPlaybackModel::scopes() const
{
    QVariantList list;
    for (const Scope& sc : m_scopes) {
        const bool customised = sc.partId == 0 ? !m_settings.scoreValues.isEmpty()
                                : (m_settings.partValues.count(sc.partId) && !m_settings.partValues.at(sc.partId).isEmpty());
        QVariantMap m;
        m["text"] = customised ? sc.name + " •" : sc.name;
        list << m;
    }
    return list;
}

int ArticulationPlaybackModel::currentScope() const
{
    return m_currentScope;
}

void ArticulationPlaybackModel::setCurrentScope(int index)
{
    if (index < 0 || index >= int(m_scopes.size()) || index == m_currentScope) {
        return;
    }
    m_currentScope = index;
    emit currentScopeChanged();
    emit paramsChanged();
}

ArticulationPlaybackValues& ArticulationPlaybackModel::currentValues()
{
    const uint64_t partId = m_scopes.empty() ? 0 : m_scopes[m_currentScope].partId;
    return partId == 0 ? m_settings.scoreValues : m_settings.partValues[partId];
}

const ArticulationPlaybackValues* ArticulationPlaybackModel::currentValuesIfAny() const
{
    const uint64_t partId = m_scopes.empty() ? 0 : m_scopes[m_currentScope].partId;
    if (partId == 0) {
        return &m_settings.scoreValues;
    }
    auto it = m_settings.partValues.find(partId);
    return it == m_settings.partValues.end() ? nullptr : &it->second;
}

QVariantList ArticulationPlaybackModel::params() const
{
    const bool partScope = !m_scopes.empty() && m_scopes[m_currentScope].partId != 0;
    const ArticulationPlaybackValues* own = currentValuesIfAny();

    QVariantList list;
    const char* previousGroup = nullptr;
    for (const ParamText& t : paramTexts()) {
        const ArtParamInfo& info = artParamInfo(t.param);
        const std::optional<int> ownValue = own ? own->get(t.param) : std::nullopt;
        const std::optional<int> scoreValue = m_settings.scoreValues.get(t.param);

        int shown = info.defaultValue;
        QString source = muse::qtrc("notation", "default");
        if (ownValue) {
            shown = *ownValue;
            source = partScope ? muse::qtrc("notation", "this instrument") : muse::qtrc("notation", "whole score");
        } else if (partScope && scoreValue) {
            shown = *scoreValue;
            source = muse::qtrc("notation", "from whole score");
        }

        QVariantMap m;
        m["key"] = QString::fromLatin1(info.key);
        m["group"] = muse::qtrc("notation", t.group);
        m["title"] = muse::qtrc("notation", t.title);
        m["unit"] = QString::fromLatin1(t.unit);
        m["min"] = info.minValue;
        m["max"] = info.maxValue;
        m["defaultValue"] = info.defaultValue;
        m["value"] = shown;
        m["isSet"] = ownValue.has_value();
        m["source"] = source;
        m["showHeader"] = !previousGroup || std::strcmp(previousGroup, t.group) != 0;
        previousGroup = t.group;
        list << m;
    }
    return list;
}

void ArticulationPlaybackModel::setValue(const QString& key, int value)
{
    const std::optional<ArtParam> p = paramForKey(key);
    if (!p) {
        return;
    }
    currentValues().set(*p, value);
    setResult(QString());
    emit paramsChanged();
    emit scopesChanged();
}

void ArticulationPlaybackModel::resetValue(const QString& key)
{
    const std::optional<ArtParam> p = paramForKey(key);
    if (!p) {
        return;
    }
    currentValues().set(*p, std::nullopt);
    setResult(QString());
    emit paramsChanged();
    emit scopesChanged();
}

void ArticulationPlaybackModel::resetScope()
{
    currentValues() = ArticulationPlaybackValues();
    setResult(QString());
    emit paramsChanged();
    emit scopesChanged();
}

bool ArticulationPlaybackModel::hasChanges() const
{
    // Compared as stored, so an instrument with nothing set counts as unchanged
    return m_settings.toText() != m_original.toText();
}

QString ArticulationPlaybackModel::resultText() const
{
    return m_resultText;
}

void ArticulationPlaybackModel::setResult(const QString& text)
{
    if (m_resultText != text) {
        m_resultText = text;
        emit resultChanged();
    }
}

bool ArticulationPlaybackModel::apply()
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* s = score();
    if (!notation || !s || !hasChanges()) {
        return false;
    }

    // Drop empty instrument entries before saving
    for (auto it = m_settings.partValues.begin(); it != m_settings.partValues.end();) {
        it = it->second.isEmpty() ? m_settings.partValues.erase(it) : std::next(it);
    }

    notation->undoStack()->prepareChanges(muse::TranslatableString("undoableAction", "Change articulation playback"));
    m_settings.write(s);
    notation->undoStack()->commitChanges();

    m_original = ArticulationPlaybackSettings::read(s);
    m_settings = m_original;
    emit paramsChanged();
    emit scopesChanged();
    setResult(muse::qtrc("notation", "Applied. Playback uses the new settings; the written score is unchanged. Undo with Ctrl+Z (⌘Z on Mac)."));
    return true;
}
