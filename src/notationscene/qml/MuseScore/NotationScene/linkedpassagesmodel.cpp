/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "linkedpassagesmodel.h"

#include <QVariantMap>

#include "translation.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/dom/select.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/textline.h"

#include "notation/inotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationinteraction.h"
#include "notation/internal/inotationundostack.h"

using namespace mu::notation;
using namespace mu::engraving;
namespace jlp = mu::notation::jubelle;
using mu::notation::passagelink::SyncOptions;

LinkedPassagesModel::LinkedPassagesModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

Score* LinkedPassagesModel::score() const
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    return notation && notation->elements() ? notation->elements()->msScore() : nullptr;
}

void LinkedPassagesModel::load()
{
    Score* s = score();
    m_problem.clear();
    m_hasSelection = false;
    m_candidates.clear();
    m_groups.clear();
    if (!s) {
        m_problem = muse::qtrc("notation", "Open a score first.");
        emit changed();
        return;
    }
    if (!s->isMaster()) {
        // Links live in the full score; a part has its own staff numbering
        m_problem = muse::qtrc("notation", "Linked passages are set up in the full score. Switch to the full score to link or unlink.");
    }
    m_groups = jlp::linkGroups(s);

    const Selection& sel = s->selection();
    if (s->isMaster() && sel.isRange()) {
        m_hasSelection = true;
        m_sourceStaff = sel.staffStart();
        m_startTick = sel.tickStart().ticks();
        m_endTick = sel.tickEnd().ticks();
        for (size_t i = 0; i < s->nstaves(); ++i) {
            if (i == m_sourceStaff) {
                continue;
            }
            // Other staves inside the highlighted range are ticked to begin with
            m_candidates.emplace_back(i, i < sel.staffEnd());
        }
    }
    m_messages.clear();
    for (const muse::String& m : jlp::lastLinkMessages()) {
        m_messages << m.toQString();
    }
    emit changed();
}

QString LinkedPassagesModel::staffName(size_t staffIdx) const
{
    const Score* s = score();
    const Staff* st = s ? s->staff(staffIdx) : nullptr;
    if (!st) {
        return QString();
    }
    QString name = st->partName().toQString();
    if (st->part() && st->part()->nstaves() > 1) {
        name += " " + muse::qtrc("notation", "(staff %1)").arg(st->rstaff() + 1);
    }
    return name;
}

QString LinkedPassagesModel::barsText(int startTick, int endTick) const
{
    const Score* s = score();
    const MasterScore* master = s ? s->masterScore() : nullptr;
    const Measure* a = master ? master->tick2measure(Fraction::fromTicks(startTick)) : nullptr;
    const Measure* b = master ? master->tick2measure(Fraction::fromTicks(endTick - 1)) : nullptr;
    if (a && b && a != b) {
        return muse::qtrc("notation", "bars %1–%2").arg(a->no() + 1).arg(b->no() + 1);
    }
    return muse::qtrc("notation", "bar %1").arg(a ? a->no() + 1 : 1);
}

QString LinkedPassagesModel::selectionTitle() const
{
    return m_hasSelection ? staffName(m_sourceStaff) + " · " + barsText(m_startTick, m_endTick) : QString();
}

QVariantList LinkedPassagesModel::candidateStaves() const
{
    QVariantList list;
    for (const auto& [staff, checked] : m_candidates) {
        QVariantMap m;
        m["staff"] = int(staff);
        m["name"] = staffName(staff);
        m["checked"] = checked;
        list << m;
    }
    return list;
}

QVariantList LinkedPassagesModel::groups() const
{
    QVariantList list;
    for (const jlp::LinkGroup& g : m_groups) {
        QVariantMap gm;
        gm["id"] = g.id;
        gm["title"] = muse::qtrc("notation", "Linked to %1 · %2").arg(staffName(g.source.staffIdx), barsText(g.source.startTick,
                                                                                                            g.source.endTick));
        QVariantMap sync;
        sync["rhythm"] = g.sync.rhythm;
        sync["durations"] = g.sync.durations;
        sync["articulations"] = g.sync.articulations;
        sync["dynamics"] = g.sync.dynamics;
        sync["markings"] = g.sync.markings;
        gm["sync"] = sync;

        QVariantList members;
        auto add = [&](const jlp::LinkMember& m, int index) {
            QVariantMap mm;
            mm["index"] = index;
            mm["name"] = staffName(m.staffIdx);
            mm["bars"] = barsText(m.startTick, m.endTick);
            mm["isSource"] = m.source;
            mm["ownNotes"] = int(m.handEdited.size());
            members << mm;
        };
        add(g.source, 0);
        for (size_t i = 0; i < g.targets.size(); ++i) {
            add(g.targets[i], int(i) + 1);
        }
        gm["members"] = members;
        list << gm;
    }
    return list;
}

QStringList LinkedPassagesModel::messages() const
{
    return m_messages;
}

QString LinkedPassagesModel::problem() const
{
    return m_problem;
}

void LinkedPassagesModel::setCandidateChecked(int staff, bool checked)
{
    for (auto& c : m_candidates) {
        if (int(c.first) == staff) {
            c.second = checked;
        }
    }
    emit changed();
}

const jlp::LinkMember* LinkedPassagesModel::member(int groupId, int memberIndex) const
{
    for (const jlp::LinkGroup& g : m_groups) {
        if (g.id != groupId) {
            continue;
        }
        if (memberIndex == 0) {
            return &g.source;
        }
        if (memberIndex > 0 && size_t(memberIndex) <= g.targets.size()) {
            return &g.targets[memberIndex - 1];
        }
    }
    return nullptr;
}

void LinkedPassagesModel::apply(const char* undoText, const std::function<void(Score*)>& f)
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* s = score();
    if (!notation || !s || !s->isMaster()) {
        return;
    }
    notation->undoStack()->prepareChanges(muse::TranslatableString("undoableAction", undoText));
    f(s);
    notation->undoStack()->commitChanges();
    const bool keepSelection = m_hasSelection;
    load();
    m_hasSelection = keepSelection && s->selection().isRange();
    emit changed();
}

void LinkedPassagesModel::link(bool rhythm, bool durations, bool articulations, bool dynamics, bool markings)
{
    if (!m_hasSelection) {
        return;
    }
    std::vector<size_t> targets;
    for (const auto& [staff, checked] : m_candidates) {
        if (checked) {
            targets.push_back(staff);
        }
    }
    if (targets.empty()) {
        return;
    }
    SyncOptions o;
    o.rhythm = rhythm;
    o.durations = durations;
    o.articulations = articulations;
    o.dynamics = dynamics;
    o.markings = markings;
    const size_t source = m_sourceStaff;
    const int a = m_startTick;
    const int b = m_endTick;
    int id = 0;
    apply("Link passages", [&](Score* s) { id = jlp::linkPassages(s, source, a, b, targets, o); });
    if (id == 0 && m_messages.isEmpty()) {
        m_messages << muse::qtrc("notation", "Nothing was linked. A staff may already be linked in these bars, "
                                             "or the bars don't line up.");
        emit changed();
    }
}

void LinkedPassagesModel::setSync(int groupId, const QString& option, bool on)
{
    for (const jlp::LinkGroup& g : m_groups) {
        if (g.id != groupId) {
            continue;
        }
        SyncOptions o = g.sync;
        if (option == "rhythm") {
            o.rhythm = on;
        } else if (option == "durations") {
            o.durations = on;
        } else if (option == "articulations") {
            o.articulations = on;
        } else if (option == "dynamics") {
            o.dynamics = on;
        } else if (option == "markings") {
            o.markings = on;
        }
        apply("Change what linked passages follow", [&](Score* s) { jlp::setSyncOptions(s, groupId, o); });
        return;
    }
}

void LinkedPassagesModel::resync(int groupId, int memberIndex)
{
    const jlp::LinkMember* m = member(groupId, memberIndex);
    if (!m || m->source) {
        return;
    }
    TextLine* line = m->line;
    apply("Re-sync linked passage", [&](Score* s) { jlp::resyncTarget(s, line); });
}

void LinkedPassagesModel::unlink(int groupId, int memberIndex)
{
    const jlp::LinkMember* m = member(groupId, memberIndex);
    if (!m) {
        return;
    }
    TextLine* line = m->line;
    apply("Unlink passage", [&](Score* s) { jlp::unlinkMember(s, line); });
}

void LinkedPassagesModel::show(int groupId, int memberIndex)
{
    const jlp::LinkMember* m = member(groupId, memberIndex);
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* s = score();
    if (!m || !notation || !s) {
        return;
    }
    Measure* a = s->tick2measure(Fraction::fromTicks(m->startTick));
    Measure* b = s->tick2measure(Fraction::fromTicks(m->endTick - 1));
    if (!a || !b) {
        return;
    }
    notation->interaction()->select({ a }, SelectType::RANGE, m->staffIdx);
    notation->interaction()->select({ b }, SelectType::RANGE, m->staffIdx);
    notation->interaction()->showItem(a, int(m->staffIdx));
}
