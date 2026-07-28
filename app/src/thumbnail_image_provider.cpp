#include "thumbnail_image_provider.hpp"

#include <QMutexLocker>

ThumbnailImageProvider::ThumbnailImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage ThumbnailImageProvider::requestImage(const QString& id, QSize* size,
                                            const QSize& requestedSize) {
    QImage image;
    {
        const QMutexLocker lock(&mutex_);
        image = images_.value(id);
    }

    if (size != nullptr) {
        *size = image.size();
    }
    if (!image.isNull() && requestedSize.isValid() &&
        (image.width() > requestedSize.width() || image.height() > requestedSize.height())) {
        return image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

void ThumbnailImageProvider::insert(const QString& id, QImage image) {
    const QMutexLocker lock(&mutex_);
    images_.insert(id, std::move(image));
}

void ThumbnailImageProvider::remove(const QString& id) {
    const QMutexLocker lock(&mutex_);
    images_.remove(id);
}

void ThumbnailImageProvider::clear() {
    const QMutexLocker lock(&mutex_);
    images_.clear();
}
