/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 * Music Composition & Notation
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "harmonisemelodymodel.h"

#include <QStringList>
#include <QVariantMap>

#include "settings.h"
#include "translation.h"

#include "notation/inotation.h"
#include "notation/inotationinteraction.h"
#include "notation/internal/inotationundostack.h"

using namespace mu::notation;
using namespace muse;

namespace jh = mu::notation::jubelle::harmony;

namespace {
Settings::Key harmoniseKey(const char* name)
{
    return Settings::Key("notation", std::string("jubelle/harmonise/") + name);
}

int readHarmoniseInt(const char* name, int def)
{
    const Val v = settings()->value(harmoniseKey(name));
    return v.isNull() ? def : v.toInt();
}

QVariant harmoniseItem(int value, const QString& text, const QString& hint = QString())
{
    QVariantMap m;
    m["value"] = value;
    m["text"] = text;
    m["hint"] = hint;
    return m;
}
}

HarmoniseMelodyModel::HarmoniseMelodyModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

INotationPtr HarmoniseMelodyModel::notation() const
{
    return globalContext() ? globalContext()->currentNotation() : nullptr;
}

QVariantList HarmoniseMelodyModel::trumpetTechniques() const
{
    using T = jh::TrumpetTechnique;
    return {
        harmoniseItem(int(T::Automatic), qtrc("notation", "Closed, or trumpet triads when the lead goes above concert A5"),
             qtrc("notation", "Lessons 1 and 4: the closed ensemble works up to about the A on top of the treble staff; higher, trumpets 1 and 4 double the lead in octaves.")),
        harmoniseItem(int(T::Closed), qtrc("notation", "Closed voicing"), qtrc("notation", "Lesson 1: four notes within an octave, at least a minor third under the lead.")),
        harmoniseItem(int(T::TrumpetTriads), qtrc("notation", "Trumpet triads"), qtrc("notation", "Lesson 4: trumpets 1 and 4 in octaves, 2 and 3 between them.")),
        harmoniseItem(int(T::Unison), qtrc("notation", "Unison")),
        harmoniseItem(int(T::LeaveOut), qtrc("notation", "Leave out")),
    };
}

QVariantList HarmoniseMelodyModel::tromboneTechniques() const
{
    using T = jh::TromboneTechnique;
    return {
        harmoniseItem(int(T::OctaveBelowTrumpets), qtrc("notation", "An octave below the trumpets"), qtrc("notation", "Lesson 1. With trumpet triads, trombone 1 sits in its strong range (B♭3–A4) with the others closed under it.")),
        harmoniseItem(int(T::Unison), qtrc("notation", "Unison"), qtrc("notation", "Lesson 2: a counter-line in the trombones.")),
        harmoniseItem(int(T::LeaveOut), qtrc("notation", "Leave out")),
    };
}

QVariantList HarmoniseMelodyModel::saxTechniques() const
{
    using T = jh::SaxTechnique;
    return {
        harmoniseItem(int(T::DoubleTrombones), qtrc("notation", "Full band: double the trombones, baritone on roots"), qtrc("notation", "Lesson 1: altos and tenors on the trombone notes; the baritone mostly plays roots.")),
        harmoniseItem(int(T::Unison), qtrc("notation", "Unison"), qtrc("notation", "Lesson 6: the baritone drops out when the line is too high for it.")),
        harmoniseItem(int(T::OctaveUnison4Up1Down), qtrc("notation", "Octave unison, 4 up and 1 down"), qtrc("notation", "Altos and tenors in unison, the baritone an octave below.")),
        harmoniseItem(int(T::OctaveUnison2Up3Down), qtrc("notation", "Octave unison, 2 up and 3 down"), qtrc("notation", "Altos above; tenors and baritone an octave below.")),
        harmoniseItem(int(T::TwoPart), qtrc("notation", "Two-part"), qtrc("notation", "Altos on the melody; tenors and baritone a 3rd or 6th below, in the same rhythm.")),
        harmoniseItem(int(T::Closed), qtrc("notation", "Closed"), qtrc("notation", "Alto 1 on the melody, the baritone an octave below it, the others closed in between.")),
        harmoniseItem(int(T::Drop2), qtrc("notation", "Drop 2"), qtrc("notation", "Medium tempo or slower.")),
        harmoniseItem(int(T::Drop2And4), qtrc("notation", "Drop 2 and 4"), qtrc("notation", "Slow music only.")),
        harmoniseItem(int(T::FourPartDrop2), qtrc("notation", "Four-part Drop 2 (tenor 2 elsewhere)"), qtrc("notation", "For when one saxophone is busy with a solo or another line.")),
        harmoniseItem(int(T::Drop2BariRoots), qtrc("notation", "Drop 2 with the baritone on roots"), qtrc("notation", "Keeps the root clear where Drop 2 and 4 would be muddy.")),
        harmoniseItem(int(T::LeaveOut), qtrc("notation", "Leave out")),
    };
}

QVariantList HarmoniseMelodyModel::nctTools() const
{
    using T = jh::NctTool;
    return {
        harmoniseItem(int(T::Automatic), qtrc("notation", "Choose for each note"), qtrc("notation", "Chromatic planing where the note moves a half step to a chord tone, else a diatonic passing chord, else diminished.")),
        harmoniseItem(int(T::ChromaticPlaning), qtrc("notation", "Chromatic planing")),
        harmoniseItem(int(T::Diatonic), qtrc("notation", "Diatonic passing chord"), qtrc("notation", "V7 of a major or minor chord, ii of a dominant.")),
        harmoniseItem(int(T::Diminished), qtrc("notation", "Diminished passing chord"), qtrc("notation", "Minor thirds down from the melody; typical of early swing, best not overused.")),
    };
}

void HarmoniseMelodyModel::load()
{
    m_options.orchestration.trumpets = jh::TrumpetTechnique(readHarmoniseInt("trumpets", int(jh::TrumpetTechnique::Automatic)));
    m_options.orchestration.trombones = jh::TromboneTechnique(readHarmoniseInt("trombones", int(jh::TromboneTechnique::OctaveBelowTrumpets)));
    m_options.orchestration.saxes = jh::SaxTechnique(readHarmoniseInt("saxes", int(jh::SaxTechnique::DoubleTrombones)));
    m_options.harmony.nct = jh::NctTool(readHarmoniseInt("nct", int(jh::NctTool::Automatic)));
    // your decision 5: the lessons' ♭9♯5 where the melody allows it
    m_options.harmony.addAlterations = readHarmoniseInt("alterations", 1) != 0;
    // optional, off unless you ask (lesson 5 places it mainly in the rhythm section)
    m_options.harmony.halfStepSlide = readHarmoniseInt("halfStepSlide", 0) != 0;
    m_options.existing = readHarmoniseInt("keepExisting", 1) != 0 ? jh::ExistingNotes::KeepExisting : jh::ExistingNotes::Replace;
    m_options.writeChordSymbols = readHarmoniseInt("writeChordSymbols", 1) != 0;
    m_options.orchestration.altosFillHighTriads = readHarmoniseInt("altosFill", 0) != 0;   // lesson 4 "sometimes": off unless you ask
    emit optionsChanged();

    m_scopeTitle.clear();
    m_scopeDetail.clear();
    m_sectionsText.clear();
    m_canRun = false;
    INotationPtr n = notation();
    if (n) {
        const jh::Scope sc = n->interaction()->harmonyScope(m_options);
        m_canRun = sc.ok;
        if (!sc.ok) {
            m_scopeTitle = qtrc("notation", "Nothing to harmonise yet");
            m_scopeDetail = QString::fromStdString(sc.problem);
        } else {
            const QString bars = sc.firstBar == sc.lastBar ? qtrc("notation", "bar %1").arg(sc.firstBar)
                                 : qtrc("notation", "bars %1–%2").arg(sc.firstBar).arg(sc.lastBar);
            m_scopeTitle = qtrc("notation", "Melody: %1, %2").arg(QString::fromStdString(sc.melodyStaffName), bars);
            QStringList lines;
            lines << qtrc("notation", "%n melody note(s) over %1 chord symbol(s).", nullptr, sc.melodyNotes).arg(sc.chordSymbols);
            if (sc.identifiedChords > 0) {
                lines << qtrc("notation", "Where there is no chord symbol, %n chord(s) are read from what the rhythm section plays; those are never altered.",
                              nullptr, sc.identifiedChords);
            }
            if (sc.chordSymbols == 0 && sc.identifiedChords == 0) {
                lines << qtrc("notation", "There are no chords for this passage: add chord symbols first (the lessons harmonise from the chords).");
                m_canRun = false;
            } else if (sc.notesWithoutChord > 0) {
                lines << qtrc("notation", "%n note(s) before the first chord symbol are played in unison.", nullptr, sc.notesWithoutChord);
            }
            m_scopeDetail = lines.join(QStringLiteral(" "));
        }
        // which staves will play
        const jh::SectionStaves st = jh::findSections(n->elements()->msScore());
        auto count = [](const auto& arr) {
            int k = 0;
            for (auto s : arr) {
                k += s != muse::nidx ? 1 : 0;
            }
            return k;
        };
        m_sectionsText = qtrc("notation", "Found %1 trumpets, %2 trombones and %3 saxophones. The melody staff itself is not changed.")
                         .arg(count(st.trumpets)).arg(count(st.trombones)).arg(count(st.saxes));
        if (st.saxes[4] == muse::nidx && count(st.saxes) > 0) {
            m_sectionsText += QStringLiteral(" ") + qtrc("notation", "No baritone: the bass trombone takes the roots.");
        }
    }
    emit scopeChanged();
    updateWarning();
    m_applied = false;
    m_versions.clear();
    m_currentVersion = -1;
    m_resultText.clear();
    emit resultChanged();
}

void HarmoniseMelodyModel::optionChanged(const char* name, int value)
{
    settings()->setSharedValue(harmoniseKey(name), Val(value));
    updateWarning();
    emit optionsChanged();
}

void HarmoniseMelodyModel::updateWarning()
{
    m_saxWarning.clear();
    m_notices.clear();
    INotationPtr n = notation();
    if (n && m_canRun) {
        const jh::Scope sc = n->interaction()->harmonyScope(m_options);
        if (sc.saxWarning) {
            m_saxWarning = QString::fromStdString(*sc.saxWarning);
        }
        for (const std::string& t : sc.notices) {
            m_notices << QString::fromStdString(t);
        }
    }
}

int HarmoniseMelodyModel::trumpets() const { return int(m_options.orchestration.trumpets); }
void HarmoniseMelodyModel::setTrumpets(int v)
{
    if (v != trumpets()) {
        m_options.orchestration.trumpets = jh::TrumpetTechnique(v);
        optionChanged("trumpets", v);
    }
}

int HarmoniseMelodyModel::trombones() const { return int(m_options.orchestration.trombones); }
void HarmoniseMelodyModel::setTrombones(int v)
{
    if (v != trombones()) {
        m_options.orchestration.trombones = jh::TromboneTechnique(v);
        optionChanged("trombones", v);
    }
}

int HarmoniseMelodyModel::saxes() const { return int(m_options.orchestration.saxes); }
void HarmoniseMelodyModel::setSaxes(int v)
{
    if (v != saxes()) {
        m_options.orchestration.saxes = jh::SaxTechnique(v);
        optionChanged("saxes", v);
    }
}

int HarmoniseMelodyModel::nctTool() const { return int(m_options.harmony.nct); }
void HarmoniseMelodyModel::setNctTool(int v)
{
    if (v != nctTool()) {
        m_options.harmony.nct = jh::NctTool(v);
        optionChanged("nct", v);
    }
}

bool HarmoniseMelodyModel::addAlterations() const { return m_options.harmony.addAlterations; }
void HarmoniseMelodyModel::setAddAlterations(bool v)
{
    if (v != addAlterations()) {
        m_options.harmony.addAlterations = v;
        optionChanged("alterations", v ? 1 : 0);
    }
}

bool HarmoniseMelodyModel::halfStepSlide() const { return m_options.harmony.halfStepSlide; }
void HarmoniseMelodyModel::setHalfStepSlide(bool v)
{
    if (v != halfStepSlide()) {
        m_options.harmony.halfStepSlide = v;
        optionChanged("halfStepSlide", v ? 1 : 0);
    }
}

bool HarmoniseMelodyModel::keepExisting() const { return m_options.existing == jh::ExistingNotes::KeepExisting; }
void HarmoniseMelodyModel::setKeepExisting(bool v)
{
    if (v != keepExisting()) {
        m_options.existing = v ? jh::ExistingNotes::KeepExisting : jh::ExistingNotes::Replace;
        optionChanged("keepExisting", v ? 1 : 0);
    }
}

bool HarmoniseMelodyModel::altosFill() const { return m_options.orchestration.altosFillHighTriads; }
void HarmoniseMelodyModel::setAltosFill(bool v)
{
    if (v != altosFill()) {
        m_options.orchestration.altosFillHighTriads = v;
        optionChanged("altosFill", v ? 1 : 0);
    }
}

bool HarmoniseMelodyModel::writeChordSymbols() const { return m_options.writeChordSymbols; }
void HarmoniseMelodyModel::setWriteChordSymbols(bool v)
{
    if (v != writeChordSymbols()) {
        m_options.writeChordSymbols = v;
        optionChanged("writeChordSymbols", v ? 1 : 0);
    }
}

bool HarmoniseMelodyModel::write(const jh::RunOptions& options)
{
    INotationPtr n = notation();
    if (!n || !m_canRun) {
        return false;
    }
    dispatcher()->dispatch("stop");
    const jh::Report r = n->interaction()->harmoniseMelody(options);
    if (!r.ok) {
        m_resultText = QString::fromStdString(r.problem);
        emit resultChanged();
        return false;
    }
    m_applied = true;
    QStringList lines;
    lines << qtrc("notation", "Wrote %n note(s) in %1 staves.", nullptr, r.notesWritten).arg(r.stavesWritten);
    if (r.nonChordTones > 0) {
        lines << qtrc("notation", "Non-chord tones: %1 planed, %2 diatonic, %3 diminished.").arg(r.planed).arg(r.diatonic).arg(r.diminished);
    }
    if (r.chordSymbolsChanged + r.chordSymbolsWritten > 0) {
        lines << qtrc("notation", "%n chord symbol(s) updated to match what the horns play.", nullptr,
                      r.chordSymbolsChanged + r.chordSymbolsWritten);
    }
    if (!r.stavesKept.empty()) {
        QStringList kept;
        for (const std::string& s : r.stavesKept) {
            kept << QString::fromStdString(s);
        }
        lines << qtrc("notation", "Left alone because they already have notes here: %1.").arg(kept.join(QStringLiteral(", ")));
    }
    if (r.outOfRange > 0) {
        QStringList where;
        for (size_t i = 0; i < r.outOfRangeWhere.size() && i < 4; ++i) {
            where << QString::fromStdString(r.outOfRangeWhere[i]);
        }
        lines << qtrc("notation", "%n note(s) are outside the player's range (%1). Try another technique, or move the passage.",
                      nullptr, r.outOfRange).arg(where.join(QStringLiteral("; ")));
    }
    if (r.awkwardSpellings > 0) {
        QStringList where;
        for (size_t i = 0; i < r.awkwardWhere.size() && i < 3; ++i) {
            where << QString::fromStdString(r.awkwardWhere[i]);
        }
        lines << qtrc("notation", "%n step(s) can't be spelled to read the way they sound, however the line is written (%1). "
                                  "The fewest such steps were chosen; Proofread points them out.", nullptr, r.awkwardSpellings)
            .arg(where.join(QStringLiteral("; ")));
    }
    if (r.unisonNotes > 0) {
        QStringList bars;
        for (size_t i = 0; i < r.unisonBars.size() && i < 8; ++i) {
            bars << QString::number(r.unisonBars[i]);
        }
        lines << qtrc("notation", "%n melody note(s) are played in unison, because there is no chord the rules can use there (bars %1).",
                      nullptr, r.unisonNotes).arg(bars.join(QStringLiteral(", ")));
    }
    for (const std::string& note : r.notes) {
        lines << QString::fromStdString(note);
    }
    m_resultText = lines.join(QStringLiteral("\n"));
    emit resultChanged();
    return true;
}

void HarmoniseMelodyModel::undoApplied()
{
    INotationPtr n = notation();
    if (!m_applied || !n) {
        return;
    }
    dispatcher()->dispatch("stop");
    if (n->undoStack()->canUndo()) {
        n->undoStack()->undo(nullptr);
    }
    m_applied = false;
}

void HarmoniseMelodyModel::preview()
{
    undoApplied();   // "regenerate": the passage is written again with the settings as they are now
    if (write(m_options)) {
        m_currentVersion = -1;
        emit resultChanged();
        dispatcher()->dispatch("play");
    }
}

void HarmoniseMelodyModel::keep()
{
    dispatcher()->dispatch("stop");
    m_applied = false;   // it stays; Undo still removes it in one step
    emit resultChanged();
}

void HarmoniseMelodyModel::discard()
{
    undoApplied();
    m_resultText = qtrc("notation", "Discarded: the score is exactly as it was.");
    m_currentVersion = -1;
    emit resultChanged();
}

QString HarmoniseMelodyModel::describe(const jh::RunOptions& o) const
{
    auto textOf = [](const QVariantList& list, int value) {
        for (const QVariant& v : list) {
            const QVariantMap m = v.toMap();
            if (m["value"].toInt() == value) {
                return m["text"].toString();
            }
        }
        return QString();
    };
    return qtrc("notation", "Saxes: %1").arg(textOf(saxTechniques(), int(o.orchestration.saxes)))
           + QStringLiteral(" · ") + qtrc("notation", "Trumpets: %1").arg(textOf(trumpetTechniques(), int(o.orchestration.trumpets)));
}

void HarmoniseMelodyModel::addVersion()
{
    if (m_versions.size() >= 4) {
        return;
    }
    m_versions.push_back(m_options);
    emit resultChanged();
}

void HarmoniseMelodyModel::showVersion(int index)
{
    if (index < 0 || size_t(index) >= m_versions.size()) {
        return;
    }
    undoApplied();
    // the engine is deterministic: the same settings write the same notes every time
    if (write(m_versions[size_t(index)])) {
        m_currentVersion = index;
        emit resultChanged();
        dispatcher()->dispatch("play");
    }
}

void HarmoniseMelodyModel::removeVersion(int index)
{
    if (index < 0 || size_t(index) >= m_versions.size()) {
        return;
    }
    m_versions.erase(m_versions.begin() + index);
    if (m_currentVersion == index) {
        m_currentVersion = -1;
    } else if (m_currentVersion > index) {
        --m_currentVersion;
    }
    emit resultChanged();
}

QVariantList HarmoniseMelodyModel::versions() const
{
    QVariantList out;
    for (size_t i = 0; i < m_versions.size(); ++i) {
        QVariantMap m;
        m["name"] = qtrc("notation", "Version %1").arg(int(i) + 1);
        m["detail"] = describe(m_versions[i]);
        out << m;
    }
    return out;
}
