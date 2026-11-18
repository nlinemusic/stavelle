/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "videopagethumbnail.h"

#include <cmath>

#include <QPainter>
#include <QQuickWindow>

#include "draw/painter.h"
#include "engraving/dom/page.h"
#include "engraving/dom/mscore.h"
#include "notation/inotationelements.h"
#include "notation/inotationpainting.h"

using namespace mu::project;
using namespace mu::notation;

VideoPageThumbnail::VideoPageThumbnail(QQuickItem* parent)
    : QQuickPaintedItem(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void VideoPageThumbnail::setPageNumber(int page)
{
    if (page == m_pageNumber) {
        return;
    }
    m_pageNumber = page;
    m_image = QImage();
    emit pageNumberChanged();
    update();
}

void VideoPageThumbnail::render()
{
    m_renderedSize = size().toSize();
    m_image = QImage();
    INotationPtr notation = context() ? context()->currentNotation() : nullptr;
    if (!notation || !notation->elements() || m_pageNumber < 1 || m_renderedSize.isEmpty()) {
        return;
    }
    const PageList pages = notation->elements()->pages();
    if (size_t(m_pageNumber) > pages.size()) {
        return;
    }
    const mu::engraving::Page* page = pages[size_t(m_pageNumber - 1)];
    const qreal ratio = window() ? window()->devicePixelRatio() : 1.0;
    const double w = m_renderedSize.width() * ratio;
    const double h = m_renderedSize.height() * ratio;
    const double scale = std::min(w / page->width(), h / page->height());
    const double dpi = scale * mu::engraving::DPI;

    m_image = QImage(int(std::lround(page->width() * scale)), int(std::lround(page->height() * scale)), QImage::Format_ARGB32_Premultiplied);
    m_image.setDotsPerMeterX(std::lrint((dpi * 1000) / mu::engraving::INCH));
    m_image.setDotsPerMeterY(std::lrint((dpi * 1000) / mu::engraving::INCH));
    m_image.fill(Qt::white);
    m_image.setDevicePixelRatio(ratio);

    INotationPainting::Options opt;
    opt.fromPage = m_pageNumber - 1;
    opt.toPage = opt.fromPage;
    opt.deviceDpi = dpi;
    opt.printPageBackground = false;

    muse::draw::Painter painter(&m_image, "jubelle_video_thumbnail");
    notation->painting()->paintPng(&painter, opt);
}

void VideoPageThumbnail::paint(QPainter* painter)
{
    if (m_image.isNull() || m_renderedSize != size().toSize()) {
        render();
    }
    painter->fillRect(boundingRect(), Qt::white);
    if (m_image.isNull()) {
        return;
    }
    const QSizeF shown = m_image.deviceIndependentSize();
    const QPointF at((width() - shown.width()) / 2.0, (height() - shown.height()) / 2.0);
    painter->drawImage(QRectF(at, shown), m_image);
}
