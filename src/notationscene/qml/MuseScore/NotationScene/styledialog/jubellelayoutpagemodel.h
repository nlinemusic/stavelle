/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
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

#include "abstractstyledialogmodel.h"

namespace mu::notation {
//! Jubelle G7: Format › Style › Jubelle layout (magnetic layout and collision avoidance)
class JubelleLayoutPageModel : public AbstractStyleDialogModel
{
    Q_OBJECT

    Q_PROPERTY(mu::notation::StyleItem * magneticLayout READ magneticLayout CONSTANT)
    Q_PROPERTY(mu::notation::StyleItem * magneticRows READ magneticRows CONSTANT)
    Q_PROPERTY(mu::notation::StyleItem * sidewaysNudge READ sidewaysNudge CONSTANT)
    Q_PROPERTY(mu::notation::StyleItem * nudgeLimit READ nudgeLimit CONSTANT)
    Q_PROPERTY(mu::notation::StyleItem * collisionAvoidance READ collisionAvoidance CONSTANT)

    QML_ELEMENT

public:
    explicit JubelleLayoutPageModel(QObject* parent = nullptr);

    StyleItem* magneticLayout() const { return styleItem(StyleId::jubelleMagneticLayout); }
    StyleItem* magneticRows() const { return styleItem(StyleId::jubelleMagneticRows); }
    StyleItem* sidewaysNudge() const { return styleItem(StyleId::jubelleSidewaysNudge); }
    StyleItem* nudgeLimit() const { return styleItem(StyleId::jubelleNudgeLimit); }
    StyleItem* collisionAvoidance() const { return styleItem(StyleId::jubelleCollisionAvoidance); }
};
}
