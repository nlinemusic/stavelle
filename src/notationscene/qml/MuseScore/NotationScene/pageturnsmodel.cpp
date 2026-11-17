/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G8: Format › Page turns… See pageturnsmodel.h.

#include "pageturnsmodel.h"

#include <algorithm>

#include <QVariantMap>

#include "translation.h"

#include "engraving/dom/excerpt.h"
#include "engraving/dom/layoutbreak.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/score.h"

#include "notation/inotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationinteraction.h"
#include "notation/internal/inotationundostack.h"

using namespace mu::notation;
using namespace mu::engraving;
namespace jp = mu::notation::jubelle;

PageTurnsModel::PageTurnsModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void PageTurnsModel::load()
{
    m_parts.clear();
    m_partIndex = 0;
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* current = notation && notation->elements() ? notation->elements()->msScore() : nullptr;
    if (current) {
        MasterScore* master = current->masterScore();
        m_parts.push_back({ muse::qtrc("notation/pageturns", "Full score"), master });
        for (Excerpt* ex : master->excerpts()) {
            if (ex->excerptScore()) {
                m_parts.push_back({ ex->name().toQString(), ex->excerptScore() });
                if (ex->excerptScore() == current) {
                    m_partIndex = int(m_parts.size()) - 1;
                }
            }
        }
    }
    showCurrent();
}

QVariantList PageTurnsModel::parts() const
{
    QVariantList out;
    for (size_t i = 0; i < m_parts.size(); ++i) {
        QVariantMap m;
        m["value"] = int(i);
        m["text"] = m_parts[i].name;
        out << m;
    }
    return out;
}

QVariantList PageTurnsModel::layoutOptions() const
{
    const QStringList names {
        muse::qtrc("notation/pageturns", "Automatic (from the page settings)"),
        muse::qtrc("notation/pageturns", "Page 1 on the right: turn after pages 1, 3, 5…"),
        muse::qtrc("notation/pageturns", "Page 1 on the left: turn after pages 2, 4, 6…"),
        muse::qtrc("notation/pageturns", "Turn after every page (single sheets or a tablet)"),
    };
    QVariantList out;
    for (int i = 0; i < names.size(); ++i) {
        QVariantMap m;
        m["value"] = i;
        m["text"] = names[i];
        out << m;
    }
    return out;
}

Score* PageTurnsModel::chosen() const
{
    return m_partIndex >= 0 && m_partIndex < int(m_parts.size()) ? m_parts[size_t(m_partIndex)].score : nullptr;
}

bool PageTurnsModel::hasJubelleTurns() const
{
    for (const PartEntry& p : m_parts) {
        for (const jp::PageTurn& t : jp::analyseTurns(p.score, m_settings)) {
            if (t.jubelleBreak) {
                return true;
            }
        }
    }
    return false;
}

void PageTurnsModel::setPartIndex(int index)
{
    m_partIndex = std::clamp(index, 0, std::max(0, int(m_parts.size()) - 1));
    showCurrent();
}

void PageTurnsModel::setNeededSeconds(double seconds)
{
    m_settings.neededSeconds = std::clamp(seconds, 0.5, 20.0);
    showCurrent();
}

void PageTurnsModel::setLayoutIndex(int index)
{
    m_settings.layout = jp::TurnLayout(std::clamp(index, 0, 3));
    showCurrent();
}

QString PageTurnsModel::qualityText(jp::TurnQuality q) const
{
    switch (q) {
    case jp::TurnQuality::Good: return muse::qtrc("notation/pageturns", "Good");
    case jp::TurnQuality::Tight: return muse::qtrc("notation/pageturns", "Tight");
    default: return muse::qtrc("notation/pageturns", "No time to turn");
    }
}

void PageTurnsModel::showCurrent()
{
    m_previewing = false;
    m_turns.clear();
    Score* s = chosen();
    int poor = 0;
    if (s) {
        for (const jp::PageTurn& t : jp::analyseTurns(s, m_settings)) {
            QVariantMap m;
            m["page"] = t.page;
            m["bar"] = t.nextBar;
            m["quality"] = int(t.quality);
            m["title"] = muse::qtrc("notation/pageturns", "After page %1: turn before bar %2").arg(t.page).arg(t.nextBar);
            QString detail = muse::qtrc("notation/pageturns", "%1 · %2 s of rest").arg(qualityText(t.quality)).arg(t.rest(), 0, 'f', 1);
            if (t.userBreak) {
                detail += muse::qtrc("notation/pageturns", " · your page break (kept)");
            } else if (t.jubelleBreak) {
                detail += muse::qtrc("notation/pageturns", " · placed by Jubelle");
            }
            m["detail"] = detail;
            m_turns << m;
            poor += t.quality != jp::TurnQuality::Good;
        }
    }
    if (!s) {
        m_summary.clear();
    } else if (m_turns.isEmpty()) {
        m_summary = muse::qtrc("notation/pageturns", "No page turns: this fits on the pages you see at once.");
    } else if (poor == 0) {
        m_summary = muse::qtrc("notation/pageturns", "Every turn has enough rest.");
    } else {
        m_summary = muse::qtrc("notation/pageturns", "%n turn(s) could be better. Press Preview to see what Jubelle would do.", nullptr, poor);
    }
    emit changed();
}

void PageTurnsModel::showResult(const jp::TurnResult& result)
{
    m_turns.clear();
    for (const jp::TurnChange& c : result.turns) {
        QVariantMap m;
        m["page"] = c.page;
        m["bar"] = c.nextBarAfter;
        m["quality"] = int(c.now);
        if (c.moved) {
            m["title"] = muse::qtrc("notation/pageturns", "After page %1: turn before bar %2 instead of bar %3")
                         .arg(c.page).arg(c.nextBarAfter).arg(c.nextBarBefore);
            m["detail"] = muse::qtrc("notation/pageturns", "%1 · %2 s of rest (was %3 s)")
                          .arg(qualityText(c.now)).arg(c.restNow, 0, 'f', 1).arg(c.restWas, 0, 'f', 1);
        } else {
            m["title"] = muse::qtrc("notation/pageturns", "After page %1: turn before bar %2 (unchanged)").arg(c.page).arg(c.nextBarAfter);
            QString detail = muse::qtrc("notation/pageturns", "%1 · %2 s of rest").arg(qualityText(c.now)).arg(c.restNow, 0, 'f', 1);
            if (c.userBreak) {
                detail += muse::qtrc("notation/pageturns", " · your page break (kept)");
            } else if (c.now != jp::TurnQuality::Good) {
                detail += muse::qtrc("notation/pageturns", " · no better place nearby");
            }
            m["detail"] = detail;
        }
        m_turns << m;
    }
}

void PageTurnsModel::preview()
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* s = chosen();
    if (!notation || !s) {
        return;
    }
    // worked out on the real layout, then taken back: nothing changes
    notation->undoStack()->prepareChanges(muse::TranslatableString("undoableAction", "Preview page turns"));
    const jp::TurnResult result = jp::optimiseTurns(s, m_settings);
    notation->undoStack()->rollbackChanges();
    s->doLayout();

    showResult(result);
    m_previewing = true;
    m_summary = result.moved == 0
                ? muse::qtrc("notation/pageturns", "Preview: Jubelle found nothing better. Nothing would change.")
                : muse::qtrc("notation/pageturns", "Preview: %n turn(s) would move. Nothing has changed yet: press Apply to do it.", nullptr,
                             result.moved);
    if (result.stillPoor > 0) {
        m_summary += QLatin1Char(' ') + muse::qtrc("notation/pageturns", "%n turn(s) would still have no time to turn.", nullptr,
                                                   result.stillPoor);
    }
    emit changed();
}

void PageTurnsModel::apply(bool allParts)
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    if (!notation || !chosen()) {
        return;
    }
    int moved = 0, stillPoor = 0, scores = 0;
    jp::TurnResult shown;
    // all parts share the main score's undo history: one undo for everything
    notation->undoStack()->prepareChanges(muse::TranslatableString("undoableAction", "Optimise page turns"));
    for (size_t i = 0; i < m_parts.size(); ++i) {
        if (!allParts && int(i) != m_partIndex) {
            continue;
        }
        if (allParts && i == 0 && m_parts.size() > 1) {
            continue;   // "all parts" means the parts, not the conductor's score
        }
        const jp::TurnResult r = jp::optimiseTurns(m_parts[i].score, m_settings);
        moved += r.moved;
        stillPoor += r.stillPoor;
        ++scores;
        if (int(i) == m_partIndex) {
            shown = r;
        }
    }
    notation->undoStack()->commitChanges();

    showCurrent();
    m_summary = allParts
                ? muse::qtrc("notation/pageturns", "%n turn(s) moved", nullptr, moved)
                + muse::qtrc("notation/pageturns", " in %n part(s).", nullptr, scores)
                : muse::qtrc("notation/pageturns", "%n turn(s) moved.", nullptr, moved);
    if (stillPoor > 0) {
        m_summary += QLatin1Char(' ') + muse::qtrc("notation/pageturns", "%n turn(s) still have no time to turn: there is no rest near them.",
                                                   nullptr, stillPoor);
    }
    m_summary += QLatin1Char(' ') + muse::qtrc("notation/pageturns", "Undo takes it back.");
    emit changed();
}

void PageTurnsModel::clear(bool allParts)
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    if (!notation || !chosen()) {
        return;
    }
    int removed = 0;
    notation->undoStack()->prepareChanges(muse::TranslatableString("undoableAction", "Remove Jubelle's page turns"));
    for (size_t i = 0; i < m_parts.size(); ++i) {
        if (allParts || int(i) == m_partIndex) {
            removed += jp::clearJubelleTurns(m_parts[i].score);
        }
    }
    notation->undoStack()->commitChanges();
    showCurrent();
    m_summary = muse::qtrc("notation/pageturns", "%n page break(s) removed. Your own page breaks are untouched.", nullptr, removed);
    emit changed();
}

void PageTurnsModel::goTo(int bar)
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* current = notation && notation->elements() ? notation->elements()->msScore() : nullptr;
    if (!current || current != chosen()) {
        return;   // the turn is in another part: open that part to see it
    }
    for (Measure* m = current->firstMeasure(); m; m = m->nextMeasure()) {
        if (m->no() + 1 == bar) {
            notation->interaction()->select({ m }, SelectType::SINGLE, 0);
            notation->interaction()->showItem(m, 0);
            return;
        }
    }
}
