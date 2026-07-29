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

class QQmlEngine;

class ThumbnailImageProvider final : public QQuickImageProvider {
  public:
    struct InsertResult {
        QStringList evictedIds;
        bool retained = false;
    };

    explicit ThumbnailImageProvider(std::size_t byteBudget = 64UL * 1024UL * 1024UL);

    /// The name thumbnail source URLs address the provider by.
    ///
    /// The model builds `image://<name>/<id>` URLs and the engine resolves them
    /// by this name, so both sides read it from here. A registration name that
    /// drifts from the URL scheme produces no diagnostic at all: images simply
    /// never load.
    [[nodiscard]] static QString providerName();

    /// The URL a scene binds to display the thumbnail stored under `id`.
    [[nodiscard]] static QString sourceUrl(const QString& id);

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

/// Creates a thumbnail provider, hands it to `engine`, and returns it.
///
/// The engine takes ownership, which is why the caller receives a reference
/// rather than a pointer it might delete. Registration happens in exactly one
/// place so the application and the tests cannot disagree about the name.
[[nodiscard]] ThumbnailImageProvider& installThumbnailProvider(QQmlEngine& engine);
