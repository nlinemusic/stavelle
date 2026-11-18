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
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "actions/iactionsdispatcher.h"
#include "context/iglobalcontext.h"

#include "notation/internal/jubelle/harmony/harmonyrunner.h"

namespace mu::notation {
//! Jubelle: the Harmonise melody dialog (Tools › Arranging › Harmonise melody…), for the Big Band Harmony and
//! Orchestration System. Holds the options (remembered between uses), writes the arrangement as one undo step
//! so you can listen to it, and keeps, discards or compares versions.
class HarmoniseMelodyModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QString scopeTitle READ scopeTitle NOTIFY scopeChanged)
    Q_PROPERTY(QString scopeDetail READ scopeDetail NOTIFY scopeChanged)
    Q_PROPERTY(QString sectionsText READ sectionsText NOTIFY scopeChanged)
    Q_PROPERTY(bool canRun READ canRun NOTIFY scopeChanged)

    Q_PROPERTY(QVariantList trumpetTechniques READ trumpetTechniques CONSTANT)
    Q_PROPERTY(QVariantList tromboneTechniques READ tromboneTechniques CONSTANT)
    Q_PROPERTY(QVariantList saxTechniques READ saxTechniques CONSTANT)
    Q_PROPERTY(QVariantList nctTools READ nctTools CONSTANT)

    Q_PROPERTY(int trumpets READ trumpets WRITE setTrumpets NOTIFY optionsChanged)
    Q_PROPERTY(int trombones READ trombones WRITE setTrombones NOTIFY optionsChanged)
    Q_PROPERTY(int saxes READ saxes WRITE setSaxes NOTIFY optionsChanged)
    Q_PROPERTY(int nctTool READ nctTool WRITE setNctTool NOTIFY optionsChanged)
    Q_PROPERTY(bool addAlterations READ addAlterations WRITE setAddAlterations NOTIFY optionsChanged)
    Q_PROPERTY(bool halfStepSlide READ halfStepSlide WRITE setHalfStepSlide NOTIFY optionsChanged)
    Q_PROPERTY(bool keepExisting READ keepExisting WRITE setKeepExisting NOTIFY optionsChanged)
    Q_PROPERTY(bool writeChordSymbols READ writeChordSymbols WRITE setWriteChordSymbols NOTIFY optionsChanged)
    Q_PROPERTY(QString saxWarning READ saxWarning NOTIFY optionsChanged)

    Q_PROPERTY(bool applied READ applied NOTIFY resultChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)
    Q_PROPERTY(QVariantList versions READ versions NOTIFY resultChanged)
    Q_PROPERTY(int currentVersion READ currentVersion NOTIFY resultChanged)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };

public:
    explicit HarmoniseMelodyModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    //! writes the arrangement (one undo step) and plays it from the start of the passage
    Q_INVOKABLE void preview();
    //! the arrangement stays in the score
    Q_INVOKABLE void keep();
    //! undoes the arrangement exactly
    Q_INVOKABLE void discard();
    //! keeps the current settings as a version to compare (up to four)
    Q_INVOKABLE void addVersion();
    //! writes version `index` in place of the current arrangement
    Q_INVOKABLE void showVersion(int index);
    Q_INVOKABLE void removeVersion(int index);

    QString scopeTitle() const { return m_scopeTitle; }
    QString scopeDetail() const { return m_scopeDetail; }
    QString sectionsText() const { return m_sectionsText; }
    bool canRun() const { return m_canRun; }

    QVariantList trumpetTechniques() const;
    QVariantList tromboneTechniques() const;
    QVariantList saxTechniques() const;
    QVariantList nctTools() const;

    int trumpets() const;
    void setTrumpets(int v);
    int trombones() const;
    void setTrombones(int v);
    int saxes() const;
    void setSaxes(int v);
    int nctTool() const;
    void setNctTool(int v);
    bool addAlterations() const;
    void setAddAlterations(bool v);
    bool halfStepSlide() const;
    void setHalfStepSlide(bool v);
    bool keepExisting() const;
    void setKeepExisting(bool v);
    bool writeChordSymbols() const;
    void setWriteChordSymbols(bool v);
    QString saxWarning() const { return m_saxWarning; }

    bool applied() const { return m_applied; }
    QString resultText() const { return m_resultText; }
    QVariantList versions() const;
    int currentVersion() const { return m_currentVersion; }

signals:
    void scopeChanged();
    void optionsChanged();
    void resultChanged();

private:
    INotationPtr notation() const;
    void optionChanged(const char* key, int value);
    void updateWarning();
    bool write(const jubelle::harmony::RunOptions& options);
    void undoApplied();
    QString describe(const jubelle::harmony::RunOptions& options) const;

    jubelle::harmony::RunOptions m_options;
    QString m_scopeTitle;
    QString m_scopeDetail;
    QString m_sectionsText;
    QString m_saxWarning;
    bool m_canRun = false;

    bool m_applied = false;
    QString m_resultText;
    std::vector<jubelle::harmony::RunOptions> m_versions;
    int m_currentVersion = -1;
};
}
