/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "condensedscoremodel.h"

#include <QUrl>

#include "translation.h"

#include "engraving/engravingproject.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"

#include "notation/inotation.h"
#include "notation/inotationelements.h"
#include "project/inotationproject.h"

#include "settings.h"

using namespace mu::notation;
using namespace mu::engraving;
namespace jc = mu::notation::jubelle;

CondensedScoreModel::CondensedScoreModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

MasterScore* CondensedScoreModel::masterScore() const
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* score = notation && notation->elements() ? notation->elements()->msScore() : nullptr;
    return score ? score->masterScore() : nullptr;
}

QString CondensedScoreModel::describe(const std::vector<jc::CondenseOverride>& list) const
{
    MasterScore* score = masterScore();
    QStringList out;
    for (const jc::CondenseOverride& o : list) {
        const Staff* st = score ? score->staff(o.staffIdx) : nullptr;
        const QString who = st ? st->part()->partName().toQString() : muse::qtrc("notation", "staff %1").arg(o.staffIdx + 1);
        const QString bars = o.fromBar == o.toBar ? muse::qtrc("notation", "bar %1").arg(o.fromBar + 1)
                             : muse::qtrc("notation", "bars %1–%2").arg(o.fromBar + 1).arg(o.toBar + 1);
        const QString what = o.mode == jc::CondenseOverride::Mode::Separate ? muse::qtrc("notation", "kept separate")
                             : muse::qtrc("notation", "condensed");
        out << who + " · " + bars + " · " + what;
    }
    return out.join("\n");
}

void CondensedScoreModel::load()
{
    m_problem.clear();
    m_pairs.clear();
    m_overrides.clear();
    m_existingOverrides.clear();
    m_copyPath = muse::io::path_t();
    m_copyExists = false;

    MasterScore* score = masterScore();
    project::INotationProjectPtr project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!score || !project) {
        m_problem = muse::qtrc("notation", "Open a score first.");
        emit changed();
        return;
    }
    m_isCopy = jc::isCondensedCopy(score);
    m_scoreName = score->metaTag(u"workTitle").toQString();
    if (m_scoreName.isEmpty()) {
        m_scoreName = muse::io::completeBasename(project->path()).toQString();
    }

    if (m_isCopy) {
        // In the copy: show your overrides; updating happens from the original
        const QString d = describe(jc::readOverrides(score));
        if (!d.isEmpty()) {
            m_overrides = d.split('\n');
        }
        emit changed();
        return;
    }

    for (const jc::CondenseGroup& g : jc::condenseGroups(score)) {
        m_pairs << score->staff(g.first)->part()->partName().toQString() + " + "
            + score->staff(g.second)->part()->partName().toQString();
    }
    if (m_pairs.isEmpty()) {
        m_problem = muse::qtrc("notation", "There is nothing to condense in this score. Condensing needs two neighbouring single-staff "
                                           "parts of the same woodwind or brass instrument (for example Trumpet 1 and Trumpet 2). "
                                           "Strings, keyboards, voices and percussion are never condensed.");
    }
    if (project->path().empty() || project->isNewlyCreated()) {
        m_problem = muse::qtrc("notation", "Save the score first: the condensed copy is saved next to it.");
    } else {
        m_copyPath = jc::condensedPathFor(project->path());
        const jc::ExistingCopy existing = jc::readExistingCopy(m_copyPath, score->iocContext());
        m_copyExists = existing.exists;
        if (existing.exists && !existing.isCondensedCopy) {
            m_problem = muse::qtrc("notation", "A file named “%1” already exists in the score’s folder and isn’t a condensed copy. "
                                               "It won’t be replaced. Rename or move it to make a condensed copy.")
                        .arg(muse::io::filename(m_copyPath).toQString());
        }
        m_existingOverrides = existing.overrides;
        const QString d = describe(existing.overrides);
        if (!d.isEmpty()) {
            m_overrides = d.split('\n');
        }
    }
    emit changed();
}

void CondensedScoreModel::toggleEnabled()
{
    dispatcher()->dispatch("jubelle-condensing-enabled");
    emit changed();
}

void CondensedScoreModel::createOrUpdate()
{
    m_done = false;
    m_result.clear();
    MasterScore* score = masterScore();
    if (!score || m_isCopy || !condensingEnabled() || m_copyPath.empty() || !m_problem.isEmpty()) {
        emit changed();
        return;
    }
    // The copy can't be replaced while it is open (here or in another window)
    project::INotationProjectPtr current = globalContext()->currentProject();
    if ((current && current->path() == m_copyPath)
        || (multiwindowsProvider() && multiwindowsProvider()->isProjectAlreadyOpened(m_copyPath))) {
        m_problem = muse::qtrc("notation", "The condensed copy is open. Save and close it first (overrides you set in it are kept), "
                                           "then update it from here.");
        emit changed();
        return;
    }
    // Read the overrides again, in case the copy was saved since this window opened
    m_existingOverrides = jc::readExistingCopy(m_copyPath, score->iocContext()).overrides;

    jc::CondenseReport report;
    std::shared_ptr<EngravingProject> copy = jc::makeCondensedCopy(score, m_existingOverrides, &report);
    if (!copy) {
        QStringList notes;
        for (const muse::String& n : report.notes) {
            notes << n.toQString();
        }
        m_problem = notes.join("\n");
        emit changed();
        return;
    }
    muse::String error;
    if (!jc::writeCondensedFile(*copy, m_copyPath, &error)) {
        m_problem = error.toQString();
        emit changed();
        return;
    }

    QStringList lines;
    lines << (m_copyExists ? muse::qtrc("notation", "The condensed copy was updated.")
              : muse::qtrc("notation", "The condensed copy was created."));
    lines << muse::qtrc("notation", "%1 pair(s) of players; on %2 system(s) they share a staff, on %3 they keep their own staves.")
        .arg(report.groups).arg(report.systemsCondensed).arg(report.systemsSeparate);
    for (const muse::String& n : report.notes) {
        lines << n.toQString();
    }
    lines << muse::qtrc("notation", "Your score was not changed.");
    m_result = lines.join("\n");
    m_copyExists = true;
    m_done = true;
    emit changed();
}

void CondensedScoreModel::openCopy()
{
    if (!m_copyPath.empty()) {
        dispatcher()->dispatch("file-open", muse::actions::ActionData::make_arg1<QUrl>(QUrl::fromLocalFile(m_copyPath.toQString())));
    }
}

bool CondensedScoreModel::condensingEnabled() const
{
    // The same setting as Tools → Condensing → Enable Condensed Scores
    // (NotationActionController::jubelleCondensingEnabled); off unless switched on
    static const muse::Settings::Key key("notation", "jubelle/condensing/enabled");
    const muse::Val v = muse::settings()->value(key);
    return !v.isNull() && v.toBool();
}

bool CondensedScoreModel::isCopy() const
{
    return m_isCopy;
}

QString CondensedScoreModel::scoreName() const
{
    return m_scoreName;
}

QString CondensedScoreModel::copyFileName() const
{
    return muse::io::filename(m_copyPath).toQString();
}

QString CondensedScoreModel::copyFolder() const
{
    return muse::io::dirpath(m_copyPath).toQString();
}

bool CondensedScoreModel::copyExists() const
{
    return m_copyExists;
}

QStringList CondensedScoreModel::pairs() const
{
    return m_pairs;
}

QStringList CondensedScoreModel::overrides() const
{
    return m_overrides;
}

QString CondensedScoreModel::problem() const
{
    return m_problem;
}

QString CondensedScoreModel::resultText() const
{
    return m_result;
}

bool CondensedScoreModel::done() const
{
    return m_done;
}
