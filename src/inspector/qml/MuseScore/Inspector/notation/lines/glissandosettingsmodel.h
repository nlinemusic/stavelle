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
#pragma once

#include <qqmlintegration.h>

#include "abstractinspectormodel.h"
#include "../jubelleplaybacksection.h"

namespace mu::inspector {
class GlissandoSettingsModel : public AbstractInspectorModel
{
    Q_OBJECT
    QML_ELEMENT;
    QML_UNCREATABLE("Not creatable from QML")

    Q_PROPERTY(mu::inspector::PropertyItem * lineType READ lineType CONSTANT)
    Q_PROPERTY(mu::inspector::PropertyItem * showText READ showText CONSTANT)
    Q_PROPERTY(mu::inspector::PropertyItem * text READ text CONSTANT)

    Q_PROPERTY(mu::inspector::PropertyItem * thickness READ thickness CONSTANT)

    Q_PROPERTY(mu::inspector::PropertyItem * lineStyle READ lineStyle CONSTANT)
    Q_PROPERTY(mu::inspector::PropertyItem * dashLineLength READ dashLineLength CONSTANT)
    Q_PROPERTY(mu::inspector::PropertyItem * dashGapLength READ dashGapLength CONSTANT)

    // Jubelle: how this glissando sounds ("Slide starts at" and speed), shared with the articulation Properties
    Q_PROPERTY(QVariantList playbackRows READ playbackRows NOTIFY playbackChanged FINAL)
    Q_PROPERTY(int playbackLevel READ playbackLevel WRITE setPlaybackLevel NOTIFY playbackChanged FINAL)
    Q_PROPERTY(QVariantList playbackLevels READ playbackLevels NOTIFY playbackChanged FINAL)
    Q_PROPERTY(QString playbackNote READ playbackNote NOTIFY playbackChanged FINAL)
    //! The start note's length in ticks (480 = a quarter), to set the start as a musical position
    Q_PROPERTY(int startNoteTicks READ startNoteTicks NOTIFY playbackChanged FINAL)

public:
    explicit GlissandoSettingsModel(QObject* parent, const muse::modularity::ContextPtr& iocCtx, IElementRepositoryService* repository);

    PropertyItem* lineType() const;
    PropertyItem* showText() const;
    PropertyItem* text() const;

    PropertyItem* thickness() const;

    PropertyItem* lineStyle() const;
    PropertyItem* dashLineLength() const;
    PropertyItem* dashGapLength() const;

    Q_INVOKABLE QVariantList possibleLineTypes() const;

    QVariantList playbackRows() const { return m_playback.rows; }
    int playbackLevel() const { return m_playback.level; }
    void setPlaybackLevel(int level);
    QVariantList playbackLevels() const { return m_playback.levels; }
    QString playbackNote() const { return m_playback.note; }
    int startNoteTicks() const { return m_startNoteTicks; }
    Q_INVOKABLE void setPlaybackValue(const QString& key, int value);
    Q_INVOKABLE void clearPlaybackValue(const QString& key);

signals:
    void playbackChanged();

private:
    void createProperties() override;
    void loadProperties() override;
    void resetProperties() override;
    void onUpdateGlissPropertiesAvailability();
    void onNotationChanged(const mu::engraving::PropertyIdSet&, const mu::engraving::StyleIdSet&) override;
    void updatePlayback();
    void changePlayback(const QString& key, std::optional<int> value);

    JubellePlaybackSection m_playback;
    int m_startNoteTicks = 0;

    PropertyItem* m_lineType = nullptr;
    PropertyItem* m_showText = nullptr;
    PropertyItem* m_text = nullptr;

    PropertyItem* m_thickness = nullptr;

    PropertyItem* m_lineStyle = nullptr;
    PropertyItem* m_dashLineLength = nullptr;
    PropertyItem* m_dashGapLength = nullptr;
};
}
