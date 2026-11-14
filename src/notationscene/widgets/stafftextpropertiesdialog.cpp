/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
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
#include "stafftextpropertiesdialog.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/score.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftextbase.h"
#include "engraving/dom/tempo.h"
#include "engraving/playback/jubelle/swingfeel.h"

#include <cmath>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>

#include "ui/view/widgetstatestore.h"

#include "translation.h"

using namespace mu::notation;
using namespace mu::engraving;

static const QString STAFF_TEXT_PROPERTIES_DIALOG_NAME("StaffTextPropertiesDialog");

StaffTextPropertiesDialog::StaffTextPropertiesDialog(QWidget* parent)
    : QDialog(parent), muse::Contextable(muse::iocCtxForQWidget(this))
{
    setObjectName(STAFF_TEXT_PROPERTIES_DIALOG_NAME);
    setupUi(this);

    const INotationPtr notation = globalContext()->currentNotation();
    const INotationInteractionPtr interaction = notation ? notation->interaction() : nullptr;
    EngravingItem* element = interaction ? interaction->hitElementContext().element : nullptr;
    StaffTextBase* st = element && element->isStaffTextBase() ? toStaffTextBase(element) : nullptr;

    if (!st) {
        return;
    }

    m_originStaffText = st;

    if (st->systemFlag()) {
        setWindowTitle(muse::qtrc("notation/stafftextproperties", "System text properties"));
    } else {
        setWindowTitle(muse::qtrc("notation/stafftextproperties", "Staff text properties"));
    }

    setWindowFlags(this->windowFlags() & ~Qt::WindowContextHelpButtonHint);
    m_staffText = static_cast<StaffTextBase*>(st->clone());

    if (m_staffText->swing()) {
        setSwingBox->setChecked(true);
        if (m_staffText->swingParameters().swingUnit == Constants::DIVISION / 2) {
            swingBox->setEnabled(true);
            swingEighth->setChecked(true);
            swingBox->setValue(m_staffText->swingParameters().swingRatio);
        } else if (m_staffText->swingParameters().swingUnit == Constants::DIVISION / 4) {
            swingBox->setEnabled(true);
            swingSixteenth->setChecked(true);
            swingBox->setValue(m_staffText->swingParameters().swingRatio);
        } else if (m_staffText->swingParameters().swingUnit == 0) {
            swingBox->setEnabled(false);
            swingOff->setChecked(true);
            swingBox->setValue(m_staffText->swingParameters().swingRatio);
        }
    }

    // Jubelle: choose a named rhythmic feel (Format → Swing…) for this text
    {
        auto* row = new QHBoxLayout();
        auto* label = new QLabel(muse::qtrc("notation", "Feel:"), this);
        m_jubelleFeel = new QComboBox(this);
        m_jubelleFeel->addItem(muse::qtrc("notation", "None (use the swing ratio above)"), QString());
        const jubelle::SwingSettings settings = jubelle::SwingSettings::read(st->score());
        int current = 0;
        for (const jubelle::SwingFeel& f : settings.allFeels()) {
            const QString name = f.builtIn ? muse::qtrc("notation", f.name.toStdString().c_str()) : f.name.toQString();
            m_jubelleFeel->addItem(name, f.id.toQString());
            if (m_staffText->swing() && f.id == m_staffText->swingParameters().jubelleFeel) {
                current = m_jubelleFeel->count() - 1;
            }
        }
        m_jubelleFeel->setCurrentIndex(current);
        m_jubelleFeel->setToolTip(muse::qtrc("notation", "A feel can depend on tempo and soften offbeats. "
                                                         "MuseScore itself plays the ratio above instead."));
        label->setBuddy(m_jubelleFeel);
        row->addWidget(label);
        row->addWidget(m_jubelleFeel, 1);
        verticalLayout_4->addLayout(row);
        connect(m_jubelleFeel, &QComboBox::currentIndexChanged, this, [this](int) { setSwingControls(true); });
    }

    connect(swingOff, &QRadioButton::toggled, this, &StaffTextPropertiesDialog::setSwingControls);
    connect(swingEighth, &QRadioButton::toggled, this, &StaffTextPropertiesDialog::setSwingControls);
    connect(swingSixteenth, &QRadioButton::toggled, this, &StaffTextPropertiesDialog::setSwingControls);

    connect(this, &QDialog::accepted, this, &StaffTextPropertiesDialog::saveValues);
}

StaffTextPropertiesDialog::~StaffTextPropertiesDialog()
{
    delete m_staffText;
}

void StaffTextPropertiesDialog::showEvent(QShowEvent* event)
{
    muse::ui::WidgetStateStore::restoreGeometry(this);
    QDialog::showEvent(event);
}

void StaffTextPropertiesDialog::hideEvent(QHideEvent* event)
{
    muse::ui::WidgetStateStore::saveGeometry(this);
    QDialog::hideEvent(event);
}

//---------------------------------------------------------
//   setSwingControls
//---------------------------------------------------------

void StaffTextPropertiesDialog::setSwingControls(bool checked)
{
    if (!checked) {
        return;
    }
    const bool feelChosen = m_jubelleFeel && m_jubelleFeel->currentIndex() > 0;
    if (swingOff->isChecked() || feelChosen) {
        swingBox->setEnabled(false);
    } else if (swingEighth->isChecked()) {
        swingBox->setEnabled(true);
    } else if (swingSixteenth->isChecked()) {
        swingBox->setEnabled(true);
    }
}

void StaffTextPropertiesDialog::saveValues()
{
    if (setSwingBox->isChecked()) {
        m_staffText->setSwing(true);
        if (swingOff->isChecked()) {
            m_staffText->setSwingParameters(0, swingBox->value());
            swingBox->setEnabled(false);
        } else if (swingEighth->isChecked()) {
            m_staffText->setSwingParameters(Constants::DIVISION / 2, swingBox->value());
            swingBox->setEnabled(true);
        } else if (swingSixteenth->isChecked()) {
            m_staffText->setSwingParameters(Constants::DIVISION / 4, swingBox->value());
            swingBox->setEnabled(true);
        }
        // Jubelle: the chosen feel; the ratio MuseScore plays is set to match
        // it at this point's tempo
        const QString feelId = m_jubelleFeel ? m_jubelleFeel->currentData().toString() : QString();
        m_staffText->setJubelleSwingFeel(muse::String::fromQString(feelId));
        if (!feelId.isEmpty() && !swingOff->isChecked()) {
            const jubelle::SwingSettings settings = jubelle::SwingSettings::read(m_originStaffText->score());
            if (const jubelle::SwingFeel* feel = settings.feel(muse::String::fromQString(feelId))) {
                const double bpm = m_originStaffText->score()->tempomap()->tempo(m_originStaffText->tick().ticks()).val * 60.0;
                const int ratio = int(std::lround(jubelle::SwingFeel::ratioToPercent(feel->ratioAt(bpm))));
                const int unit = feel->unit == 16 ? Constants::DIVISION / 4 : Constants::DIVISION / 2;
                m_staffText->setSwingParameters(unit, std::max(ratio, 50));
            }
        }
    } else {
        m_staffText->setSwing(false);
        m_staffText->setJubelleSwingFeel(muse::String());
    }

    INotationUndoStackPtr stack = undoStack();
    IF_ASSERT_FAILED(stack) {
        return;
    }

    Score* score = m_originStaffText->score();
    StaffTextBase* nt = toStaffTextBase(m_staffText->clone());
    nt->setScore(score);

    stack->prepareChanges(muse::TranslatableString("undoableAction", "Edit staff text properties"));
    score->undoChangeElement(m_originStaffText, nt);
    score->masterScore()->updateChannel();
    score->updateSwing();
    score->setPlaylistDirty();
    stack->commitChanges();
}

INotationUndoStackPtr StaffTextPropertiesDialog::undoStack() const
{
    INotationPtr notation = globalContext()->currentNotation();
    return notation ? notation->undoStack() : nullptr;
}
