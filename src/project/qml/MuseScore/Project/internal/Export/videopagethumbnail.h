/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"

namespace mu::project {
//! Jubelle: a small picture of one printed page of the current score, on white
//! paper, for the video page-sequence builder. Drawn with the same printing code
//! as PDF and PNG export.
class VideoPageThumbnail : public QQuickPaintedItem, public muse::Contextable
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int pageNumber READ pageNumber WRITE setPageNumber NOTIFY pageNumberChanged)   // 1 = first page

    muse::ContextInject<context::IGlobalContext> context = { this };

public:
    explicit VideoPageThumbnail(QQuickItem* parent = nullptr);

    int pageNumber() const { return m_pageNumber; }
    void setPageNumber(int page);

    void paint(QPainter* painter) override;

signals:
    void pageNumberChanged();

private:
    void render();

    int m_pageNumber = 0;
    QImage m_image;
    QSize m_renderedSize;
};
}
