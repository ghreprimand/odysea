#include "thumbnail_image_provider.hpp"

#include <QMutexLocker>

#include <utility>

ThumbnailImageProvider::ThumbnailImageProvider(std::size_t byteBudget)
    : QQuickImageProvider(QQuickImageProvider::Image), byteBudget_(byteBudget) {}

QImage ThumbnailImageProvider::requestImage(const QString& id, QSize* size,
                                            const QSize& requestedSize) {
    QImage image;
    {
        const QMutexLocker lock(&mutex_);
        image = images_.value(id);
        if (!image.isNull()) {
            touchLocked(id);
        }
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

ThumbnailImageProvider::InsertResult ThumbnailImageProvider::insert(const QString& id,
                                                                    QImage image) {
    const QMutexLocker lock(&mutex_);
    removeLocked(id);
    usedBytes_ += byteCost(image);
    images_.insert(id, std::move(image));
    leastToMostRecent_.append(id);

    InsertResult result;
    while (usedBytes_ > byteBudget_ && !leastToMostRecent_.isEmpty()) {
        const QString evictedId = leastToMostRecent_.front();
        result.evictedIds.append(evictedId);
        removeLocked(evictedId);
    }
    result.retained = images_.contains(id);
    return result;
}

void ThumbnailImageProvider::remove(const QString& id) {
    const QMutexLocker lock(&mutex_);
    removeLocked(id);
}

void ThumbnailImageProvider::clear() {
    const QMutexLocker lock(&mutex_);
    images_.clear();
    leastToMostRecent_.clear();
    usedBytes_ = 0;
}

std::size_t ThumbnailImageProvider::byteCost(const QImage& image) noexcept {
    const qsizetype size = image.sizeInBytes();
    return size > 0 ? static_cast<std::size_t>(size) : 0;
}

void ThumbnailImageProvider::removeLocked(const QString& id) {
    const auto existing = images_.constFind(id);
    if (existing != images_.cend()) {
        usedBytes_ -= byteCost(*existing);
        images_.remove(id);
    }
    leastToMostRecent_.removeAll(id);
}

void ThumbnailImageProvider::touchLocked(const QString& id) {
    leastToMostRecent_.removeAll(id);
    leastToMostRecent_.append(id);
}
