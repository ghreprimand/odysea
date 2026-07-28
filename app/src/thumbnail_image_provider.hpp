// Thread-safe Qt Quick image provider for decoded directory thumbnails.
#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QQuickImageProvider>
#include <QString>
#include <QStringList>

#include <cstddef>

class ThumbnailImageProvider final : public QQuickImageProvider {
  public:
    struct InsertResult {
        QStringList evictedIds;
        bool retained = false;
    };

    explicit ThumbnailImageProvider(std::size_t byteBudget = 64UL * 1024UL * 1024UL);

    [[nodiscard]] QImage requestImage(const QString& id, QSize* size,
                                      const QSize& requestedSize) override;

    [[nodiscard]] InsertResult insert(const QString& id, QImage image);
    void remove(const QString& id);
    void clear();

  private:
    [[nodiscard]] static std::size_t byteCost(const QImage& image) noexcept;
    void removeLocked(const QString& id);
    void touchLocked(const QString& id);

    QMutex mutex_;
    QHash<QString, QImage> images_;
    QList<QString> leastToMostRecent_;
    std::size_t byteBudget_ = 0;
    std::size_t usedBytes_ = 0;
};
