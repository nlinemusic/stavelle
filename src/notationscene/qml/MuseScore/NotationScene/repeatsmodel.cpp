/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "repeatsmodel.h"

#include <algorithm>

#include <QVariantMap>

#include "translation.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/rehearsalmark.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/spanner.h"
#include "engraving/dom/volta.h"

#include "notation/inotation.h"
#include "notation/inotationelements.h"
#include "notation/internal/inotationundostack.h"

using namespace mu::notation;
using namespace mu::engraving;

RepeatsModel::RepeatsModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

Score* RepeatsModel::score() const
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* s = notation && notation->elements() ? notation->elements()->msScore() : nullptr;
    // Playback always follows the main score
    return s ? s->masterScore() : nullptr;
}

static QString rehearsalMarkAt(const Measure* m)
{
    for (const Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
        for (const EngravingItem* a : s->annotations()) {
            if (a->isRehearsalMark()) {
                return toRehearsalMark(a)->plainText().toQString();
            }
        }
    }
    return QString();
}

void RepeatsModel::load()
{
    m_repeats.clear();
    Score* s = score();
    if (!s) {
        emit changed();
        return;
    }

    Measure* sectionStart = s->firstMeasure();
    Measure* repeatStart = sectionStart;
    for (Measure* m = s->firstMeasure(); m; m = m->nextMeasure()) {
        if (m->repeatStart()) {
            repeatStart = m;
        }
        if (m->repeatEnd()) {
            Repeat r;
            r.endMeasure = m;
            r.originalPlay = r.play = m->jubellePlayRepeat();

            const int from = repeatStart->no() + 1;
            const int to = m->no() + 1;
            r.title = from == to ? muse::qtrc("notation", "Bar %1").arg(from)
                      : muse::qtrc("notation", "Bars %1–%2").arg(from).arg(to);
            const QString mark = rehearsalMarkAt(repeatStart);
            if (!mark.isEmpty()) {
                r.title = muse::qtrc("notation", "%1 (at %2)").arg(r.title, mark);
            }

            QStringList details;
            details << muse::qtrc("notation", "played %n time(s)", nullptr, m->repeatCount());
            for (const auto& [tick, sp] : s->spanner()) {
                if (sp->isVolta() && toVolta(sp)->jubelleRepeatMeasure() == m) {
                    details << muse::qtrc("notation", "with endings");
                    break;
                }
            }
            r.detail = details.join(", ");
            m_repeats.push_back(r);
            // The next repeat starts after this one unless it has its own start
            repeatStart = m->nextMeasure() ? m->nextMeasure() : m;
        }
        if (m->sectionBreak() && m->nextMeasure()) {
            sectionStart = m->nextMeasure();
            repeatStart = sectionStart;
        }
    }
    setResult(QString());
    emit changed();
}

QVariantList RepeatsModel::repeats() const
{
    QVariantList list;
    for (const Repeat& r : m_repeats) {
        QVariantMap m;
        m["title"] = r.title;
        m["detail"] = r.detail;
        m["play"] = r.play;
        list << m;
    }
    return list;
}

void RepeatsModel::setPlay(int row, bool play)
{
    if (row < 0 || row >= int(m_repeats.size())) {
        return;
    }
    m_repeats[row].play = play;
    setResult(QString());
    emit changed();
}

void RepeatsModel::setAll(bool play)
{
    for (Repeat& r : m_repeats) {
        r.play = play;
    }
    setResult(QString());
    emit changed();
}

bool RepeatsModel::hasChanges() const
{
    return std::any_of(m_repeats.begin(), m_repeats.end(), [](const Repeat& r) { return r.play != r.originalPlay; });
}

QString RepeatsModel::resultText() const
{
    return m_resultText;
}

void RepeatsModel::setResult(const QString& text)
{
    if (m_resultText != text) {
        m_resultText = text;
        emit resultChanged();
    }
}

bool RepeatsModel::apply()
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    if (!notation || !score() || !hasChanges()) {
        return false;
    }
    notation->undoStack()->prepareChanges(muse::TranslatableString("undoableAction", "Change which repeats play"));
    for (Repeat& r : m_repeats) {
        if (r.play != r.originalPlay) {
            r.endMeasure->undoChangeProperty(Pid::JUBELLE_PLAY_REPEAT, r.play);
            r.originalPlay = r.play;
        }
    }
    notation->undoStack()->commitChanges();
    emit changed();
    setResult(muse::qtrc("notation", "Applied. The repeat signs stay in the score; only playback changes. Undo with Ctrl+Z (⌘Z on Mac)."));
    return true;
}
