/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "glissandosettingsmodel.h"
#include "notation/inotationelements.h"
#include "engraving/dom/spanner.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/note.h"
#include "types/linetypes.h"

#include "translation.h"

using namespace mu::inspector;

GlissandoSettingsModel::GlissandoSettingsModel(QObject* parent, const muse::modularity::ContextPtr& iocCtx,
                                               IElementRepositoryService* repository)
    : AbstractInspectorModel(parent, iocCtx, repository, mu::engraving::ElementType::GLISSANDO)
{
    setModelType(InspectorModelType::TYPE_GLISSANDO);
    setTitle(muse::qtrc("inspector", "Glissando"));
    setIcon(muse::ui::IconCode::Code::GLISSANDO);

    createProperties();
}

PropertyItem* GlissandoSettingsModel::lineType() const
{
    return m_lineType;
}

PropertyItem* GlissandoSettingsModel::showText() const
{
    return m_showText;
}

PropertyItem* GlissandoSettingsModel::text() const
{
    return m_text;
}

PropertyItem* GlissandoSettingsModel::thickness() const
{
    return m_thickness;
}

PropertyItem* GlissandoSettingsModel::lineStyle() const
{
    return m_lineStyle;
}

PropertyItem* GlissandoSettingsModel::dashLineLength() const
{
    return m_dashLineLength;
}

PropertyItem* GlissandoSettingsModel::dashGapLength() const
{
    return m_dashGapLength;
}

QVariantList GlissandoSettingsModel::possibleLineTypes() const
{
    QMap<mu::engraving::GlissandoType, QString> types {
        { mu::engraving::GlissandoType::STRAIGHT, muse::qtrc("inspector", "Straight") },
        { mu::engraving::GlissandoType::WAVY, muse::qtrc("inspector", "Wavy") }
    };

    QVariantList result;

    for (mu::engraving::GlissandoType type : types.keys()) {
        QVariantMap obj;

        obj["text"] = types[type];
        obj["value"] = static_cast<int>(type);

        result << obj;
    }

    return result;
}

void GlissandoSettingsModel::createProperties()
{
    auto applyPropertyValueAndUpdateAvailability = [this](const mu::engraving::Pid pid, const QVariant& newValue) {
        onPropertyValueChanged(pid, newValue);
        onUpdateGlissPropertiesAvailability();
    };

    m_lineType = buildPropertyItem(mu::engraving::Pid::GLISS_TYPE, applyPropertyValueAndUpdateAvailability);
    m_showText = buildPropertyItem(mu::engraving::Pid::GLISS_SHOW_TEXT);
    m_text = buildPropertyItem(mu::engraving::Pid::GLISS_TEXT, applyPropertyValueAndUpdateAvailability);

    m_thickness = buildPropertyItem(mu::engraving::Pid::LINE_WIDTH);

    m_lineStyle = buildPropertyItem(mu::engraving::Pid::LINE_STYLE, applyPropertyValueAndUpdateAvailability);
    m_dashLineLength = buildPropertyItem(mu::engraving::Pid::DASH_LINE_LEN);
    m_dashGapLength = buildPropertyItem(mu::engraving::Pid::DASH_GAP_LEN);
}

void GlissandoSettingsModel::loadProperties()
{
    loadPropertyItem(m_lineType);
    loadPropertyItem(m_showText);
    loadPropertyItem(m_text);

    loadPropertyItem(m_thickness);
    loadPropertyItem(m_lineStyle);
    loadPropertyItem(m_dashLineLength);
    loadPropertyItem(m_dashGapLength);
    updatePlayback();

    onUpdateGlissPropertiesAvailability();
}

void GlissandoSettingsModel::resetProperties()
{
    m_lineType->resetToDefault();
    m_showText->resetToDefault();
    m_text->resetToDefault();

    m_thickness->resetToDefault();
    m_lineStyle->resetToDefault();
    m_dashLineLength->resetToDefault();
    m_dashGapLength->resetToDefault();
}

void GlissandoSettingsModel::onUpdateGlissPropertiesAvailability()
{
    bool isStraightLine = m_lineType->value().value<mu::engraving::GlissandoType>() == mu::engraving::GlissandoType::STRAIGHT;
    m_lineStyle->setIsEnabled(isStraightLine);
    m_thickness->setIsEnabled(isStraightLine);

    auto currentStyle = static_cast<LineTypes::LineStyle>(m_lineStyle->value().toInt());
    bool areDashPropertiesAvailable = currentStyle == LineTypes::LineStyle::LINE_STYLE_DASHED;

    m_dashLineLength->setIsEnabled(isStraightLine && areDashPropertiesAvailable);
    m_dashGapLength->setIsEnabled(isStraightLine && areDashPropertiesAvailable);
}

// ---------------------------------------------------------------------------
// Jubelle: glissando playback for the selected glissandos
// ---------------------------------------------------------------------------

void GlissandoSettingsModel::onNotationChanged(const mu::engraving::PropertyIdSet& changed, const mu::engraving::StyleIdSet& styles)
{
    AbstractInspectorModel::onNotationChanged(changed, styles);
    updatePlayback();
}

void GlissandoSettingsModel::updatePlayback()
{
    const notation::INotationPtr notation = currentNotation();
    const mu::engraving::Score* score = notation && notation->elements() ? notation->elements()->msScore() : nullptr;
    const std::vector<mu::engraving::EngravingItem*> items(m_elementList.begin(), m_elementList.end());
    m_playback.update(score, items);
    m_startNoteTicks = 0;
    for (const mu::engraving::EngravingItem* e : items) {
        const mu::engraving::Spanner* sp = e && e->isGlissando() ? mu::engraving::toSpanner(e) : nullptr;
        if (sp && sp->startElement() && sp->startElement()->isNote()) {
            m_startNoteTicks = mu::engraving::toNote(sp->startElement())->chord()->actualTicks().ticks();
            break;
        }
    }
    emit playbackChanged();
}

void GlissandoSettingsModel::setPlaybackLevel(int level)
{
    if (level < 0 || level > 2 || level == m_playback.level) {
        return;
    }
    m_playback.level = level;
    updatePlayback();
}

void GlissandoSettingsModel::changePlayback(const QString& key, std::optional<int> value)
{
    const notation::INotationPtr notation = currentNotation();
    mu::engraving::Score* score = notation && notation->elements() ? notation->elements()->msScore() : nullptr;
    const std::vector<mu::engraving::EngravingItem*> items(m_elementList.begin(), m_elementList.end());
    m_playback.change(score, items, key, value,
                      [this]() { beginCommand(muse::TranslatableString("undoableAction", "Change glissando playback")); },
                      [this]() { endCommand(); });
    updatePlayback();
}

void GlissandoSettingsModel::setPlaybackValue(const QString& key, int value)
{
    changePlayback(key, value);
}

void GlissandoSettingsModel::clearPlaybackValue(const QString& key)
{
    changePlayback(key, std::nullopt);
}
