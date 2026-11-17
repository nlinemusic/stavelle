/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2022 MuseScore Limited and others
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

#include "loadingscreenview.h"

#include <QApplication>
#include <QPainter>
#include <QScreen>
#include <QSvgRenderer>

#include <QPixmap>

#include "translation.h"

using namespace mu::appshell;

static const QString imagePath(":/resources/LoadingScreen.svg");

static constexpr QSize loadingScreenSize(800, 380);

static const QColor messageColor("#B4B4C0");

// Jubelle: the startup screen shows Jubelle's logo and name (MuseScore's notices stay in About)
static const QString website("Based on MuseScore Studio");
static const QString logoPath(":/resources/jubelle_splash_logo.png");
static constexpr QRectF websiteRect(loadingScreenSize.width() - 48, loadingScreenSize.height() - 48, 0, 0);

static const QColor versionNumberColor("#D8B4FE");
static constexpr qreal versionNumberSpacing = 5.0;

LoadingScreenView::LoadingScreenView(QWidget* parent)
    : QWidget(parent), muse::Contextable(muse::iocCtxForQWidget(this)),
    m_backgroundRenderer(new QSvgRenderer(imagePath, this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    resize(loadingScreenSize);
}

bool LoadingScreenView::event(QEvent* event)
{
    if (event->type() == QEvent::Paint) {
        QPainter painter(this);
        painter.setLayoutDirection(layoutDirection());
        draw(&painter);
    }

    return QWidget::event(event);
}

void LoadingScreenView::draw(QPainter* painter)
{
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);

    // Draw background
    m_backgroundRenderer->render(painter);

    // Jubelle: logo and name
    static const QPixmap logo(logoPath);
    const QRectF logoRect(64, 110, 120, 120);
    if (!logo.isNull()) {
        painter->drawPixmap(logoRect, logo, QRectF(logo.rect()));
    }
    QFont nameFont(QString::fromStdString(uiConfiguration()->fontFamily()));
    nameFont.setPixelSize(64);
    nameFont.setBold(true);
    painter->setFont(nameFont);
    painter->setPen(QColor("#EDEDF2"));
    painter->drawText(QRectF(logoRect.right() + 28, logoRect.top(), 500, logoRect.height() - 28),
                      Qt::AlignVCenter | Qt::AlignLeft | Qt::TextDontClip, QStringLiteral("Jubelle"));
    QFont tagFont(nameFont);
    tagFont.setPixelSize(16);
    tagFont.setBold(false);
    painter->setFont(tagFont);
    painter->setPen(QColor("#B4B4C0"));
    painter->drawText(QRectF(logoRect.right() + 32, logoRect.bottom() - 30, 500, 24),
                      Qt::AlignVCenter | Qt::AlignLeft | Qt::TextDontClip, muse::qtrc("appshell", "Music composition and notation"));

    // Draw message
    QFont font(QString::fromStdString(uiConfiguration()->fontFamily()));
    font.setPixelSize(uiConfiguration()->fontSize());

    painter->setFont(font);

    QPen pen(messageColor);
    painter->setPen(pen);

    Qt::AlignmentFlag alignment = layoutDirection() == Qt::RightToLeft ? Qt::AlignLeft : Qt::AlignRight;

    // Draw website URL
    QRectF websiteBoundingRect;
    painter->drawText(websiteRect, Qt::AlignBottom | alignment | Qt::TextDontClip, website, &websiteBoundingRect);

    // Draw version number
    pen.setColor(versionNumberColor);
    painter->setPen(pen);

    painter->drawText(websiteRect.translated(0.0, -websiteBoundingRect.height() - versionNumberSpacing),
                      Qt::AlignBottom | alignment | Qt::TextDontClip,
                      muse::qtrc("appshell", "Version %1").arg(application()->fullVersion().toString()));
}
