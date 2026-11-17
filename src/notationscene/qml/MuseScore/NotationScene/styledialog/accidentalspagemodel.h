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
class AccidentalsPageModel : public AbstractStyleDialogModel
{
    Q_OBJECT

    Q_PROPERTY(mu::notation::StyleItem * bracketedAccidentalPadding READ bracketedAccidentalPadding CONSTANT)

    Q_PROPERTY(mu::notation::StyleItem * accidFollowNoteOffset READ accidFollowNoteOffset CONSTANT)
    Q_PROPERTY(mu::notation::StyleItem * alignAccidentalOctavesAcrossSubChords READ alignAccidentalOctavesAcrossSubChords CONSTANT)
    Q_PROPERTY(mu::notation::StyleItem * keepAccidentalSecondsTogether READ keepAccidentalSecondsTogether CONSTANT)
    Q_PROPERTY(mu::notation::StyleItem * alignOffsetOctaveAccidentals READ alignOffsetOctaveAccidentals CONSTANT)

    //! Jubelle: automatic courtesy accidentals
    Q_PROPERTY(mu::notation::StyleItem * courtesyAccidentals READ courtesyAccidentals CONSTANT)
    Q_PROPERTY(mu::notation::StyleItem * courtesyAccidentalsAllOctaves READ courtesyAccidentalsAllOctaves CONSTANT)
    Q_PROPERTY(mu::notation::StyleItem * courtesyAccidentalsBars READ courtesyAccidentalsBars CONSTANT)
    Q_PROPERTY(mu::notation::StyleItem * courtesyAccidentalsOtherStaves READ courtesyAccidentalsOtherStaves CONSTANT)
    Q_PROPERTY(mu::notation::StyleItem * courtesyAccidentalsParentheses READ courtesyAccidentalsParentheses CONSTANT)

    QML_ELEMENT

public:
    explicit AccidentalsPageModel(QObject* parent = nullptr);

    StyleItem* bracketedAccidentalPadding() const;
    StyleItem* accidFollowNoteOffset() const;
    StyleItem* alignAccidentalOctavesAcrossSubChords() const;
    StyleItem* keepAccidentalSecondsTogether() const;
    StyleItem* alignOffsetOctaveAccidentals() const;
    StyleItem* courtesyAccidentals() const;
    StyleItem* courtesyAccidentalsAllOctaves() const;
    StyleItem* courtesyAccidentalsBars() const;
    StyleItem* courtesyAccidentalsOtherStaves() const;
    StyleItem* courtesyAccidentalsParentheses() const;
};
}
