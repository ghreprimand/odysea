#include "preview_image_item.hpp"

#include <QPainter>

PreviewImageItem::PreviewImageItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setOpaquePainting(false);
}

QImage PreviewImageItem::source() const {
    return source_;
}

void PreviewImageItem::setSource(const QImage& source) {
    if (source_.cacheKey() == source.cacheKey()) {
        return;
    }
    source_ = source;
    emit sourceChanged();
    update();
}

void PreviewImageItem::paint(QPainter* painter) {
    if (source_.isNull() || width() <= 0 || height() <= 0) {
        return;
    }

    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QSizeF fitted =
        QSizeF(source_.size()).scaled(QSizeF(width(), height()), Qt::KeepAspectRatio);
    const QRectF target((width() - fitted.width()) / 2.0, (height() - fitted.height()) / 2.0,
                        fitted.width(), fitted.height());
    painter->drawImage(target, source_);
}
