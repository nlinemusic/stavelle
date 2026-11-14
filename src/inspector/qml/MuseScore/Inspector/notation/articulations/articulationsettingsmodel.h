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

#include <optional>

#include <QVariantList>
#include <qqmlintegration.h>

#include "abstractinspectormodel.h"

namespace mu::inspector {
class ArticulationSettingsModel : public AbstractInspectorModel
{
    Q_OBJECT
    QML_ELEMENT;
    QML_UNCREATABLE("Not creatable from QML")

    Q_PROPERTY(mu::inspector::PropertyItem * placement READ placement CONSTANT)
    Q_PROPERTY(bool isPlacementAvailable READ isPlacementAvailable NOTIFY isPlacementAvailableChanged FINAL)

    // Jubelle: articulation playback for the selected articulations
    Q_PROPERTY(QVariantList playbackRows READ playbackRows NOTIFY playbackChanged FINAL)
    Q_PROPERTY(int playbackLevel READ playbackLevel WRITE setPlaybackLevel NOTIFY playbackChanged FINAL)
    Q_PROPERTY(QVariantList playbackLevels READ playbackLevels NOTIFY playbackChanged FINAL)
    Q_PROPERTY(QString playbackNote READ playbackNote NOTIFY playbackChanged FINAL)

public:
    explicit ArticulationSettingsModel(QObject* parent, const muse::modularity::ContextPtr& iocCtx, IElementRepositoryService* repository,
                                       InspectorModelType type = InspectorModelType::TYPE_ARTICULATION);

    PropertyItem* placement() const;
    bool isPlacementAvailable() const;

    QVariantList playbackRows() const;
    int playbackLevel() const;
    void setPlaybackLevel(int level);
    QVariantList playbackLevels() const;
    QString playbackNote() const;
    Q_INVOKABLE void setPlaybackValue(const QString& key, int value);
    Q_INVOKABLE void clearPlaybackValue(const QString& key);

signals:
    void isPlacementAvailableChanged(bool available);
    void playbackChanged();

private:
    void createProperties() override;
    void requestElements() override;
    void loadProperties() override;
    void resetProperties() override;

    void updateIsPlacementAvailable();
    void onNotationChanged(const mu::engraving::PropertyIdSet& changedPropertyIdSet,
                           const mu::engraving::StyleIdSet& changedStyleIdSet) override;
    void updatePlayback();
    void changePlayback(const QString& key, std::optional<int> value);
    QVariantList m_playbackRows;
    QVariantList m_playbackLevels;
    QString m_playbackNote;
    int m_playbackLevel = 0;   // jubelle::EditLevel

    PropertyItem* m_direction = nullptr;
    PropertyItem* m_placement = nullptr;
    bool m_isPlacementAvailable = true;
};
}
