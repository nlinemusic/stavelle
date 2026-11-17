/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G10: Learning Center. See learningcentermodel.h.

#include "learningcentermodel.h"

#include <QVariantMap>

#include "engraving/dom/instrtemplate.h"
#include "notation/internal/jubelle/instrumentreference.h"

using namespace mu::appshell;
namespace jr = mu::notation::jubelle;

LearningCenterModel::LearningCenterModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void LearningCenterModel::load()
{
    rebuild();
}

void LearningCenterModel::setSearch(const QString& text)
{
    m_search = text;
    rebuild();
}

void LearningCenterModel::setOnlyTransposing(bool on)
{
    m_onlyTransposing = on;
    rebuild();
}

void LearningCenterModel::openShortcuts()
{
    dispatcher()->dispatch("preference-dialog");
}

void LearningCenterModel::rebuild()
{
    m_instruments.clear();
    if (!instrumentsRepository()) {
        emit changed();
        return;
    }
    const QString needle = m_search.trimmed();
    QStringList seen;
    for (const mu::engraving::InstrumentTemplate* tmpl : instrumentsRepository()->instrumentTemplates()) {
        jr::InstrumentReference r;
        if (!tmpl || !jr::referenceFor(*tmpl, &r)) {
            continue;
        }
        if (m_onlyTransposing && !r.transposing) {
            continue;
        }
        const QString name = r.name.toQString();
        const QString family = r.family.toQString();
        if (!needle.isEmpty() && !name.contains(needle, Qt::CaseInsensitive) && !family.contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        // the same instrument is listed once (the data has variants that differ only in notation traits)
        const QString key = name + "|" + r.writtenRange.toQString() + "|" + r.transposition.toQString();
        if (seen.contains(key)) {
            continue;
        }
        seen << key;
        QVariantMap m;
        m["name"] = name;
        m["family"] = family;
        m["written"] = r.writtenRange.toQString();
        m["comfortable"] = r.comfortableRange.toQString();
        m["sounding"] = r.soundingRange.toQString();
        m["transposition"] = r.transposition.toQString();
        m["transposing"] = r.transposing;
        m_instruments << m;
    }
    emit changed();
}
