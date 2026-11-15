/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "jubellelayoutpagemodel.h"

using namespace mu::notation;

JubelleLayoutPageModel::JubelleLayoutPageModel(QObject* parent)
    : AbstractStyleDialogModel(parent, {
    StyleId::jubelleMagneticLayout,
    StyleId::jubelleMagneticRows,
    StyleId::jubelleSidewaysNudge,
    StyleId::jubelleNudgeLimit,
    StyleId::jubelleCollisionAvoidance,
})
{
}
