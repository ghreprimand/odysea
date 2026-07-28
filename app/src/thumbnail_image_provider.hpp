// Thread-safe Qt Quick image provider for decoded directory thumbnails.
#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QString>

class ThumbnailImageProvider final : public QQuickImageProvider {
  public:
    ThumbnailImageProvider();

    [[nodiscard]] QImage requestImage(const QString& id, QSize* size,
                                      const QSize& requestedSize) override;

    void insert(const QString& id, QImage image);
    void remove(const QString& id);
    void clear();

  private:
    QMutex mutex_;
    QHash<QString, QImage> images_;
};
