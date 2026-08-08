#include "directory_list_model.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QMetaObject>

#include <utility>

#include "thumbnail_backend.hpp"
#include "thumbnail_image_provider.hpp"

DirectoryListModel::DirectoryListModel(ThumbnailImageProvider& thumbnailProvider, QObject* parent)
    : DirectoryListModel(parent) {
    thumbnailProvider_ = &thumbnailProvider;
    ownedThumbnailProducer_ = std::make_unique<QtThumbnailProducer>();
    ownedThumbnailStore_ = std::make_unique<FreedesktopThumbnailStore>();
    initializeThumbnailService(*ownedThumbnailProducer_, *ownedThumbnailStore_, {});
}

DirectoryListModel::DirectoryListModel(ThumbnailImageProvider& thumbnailProvider,
                                       odysea::core::ThumbnailProducer& thumbnailProducer,
                                       odysea::core::ThumbnailStore& thumbnailStore,
                                       odysea::core::ThumbnailServiceOptions options,
                                       QObject* parent)
    : DirectoryListModel(parent) {
    thumbnailProvider_ = &thumbnailProvider;
    initializeThumbnailService(thumbnailProducer, thumbnailStore, options);
}

void DirectoryListModel::initializeThumbnailService(odysea::core::ThumbnailProducer& producer,
                                                    odysea::core::ThumbnailStore& store,
                                                    odysea::core::ThumbnailServiceOptions options) {
    thumbnailService_ = std::make_unique<odysea::core::ThumbnailService>(
        producer, store,
        [this](odysea::core::ThumbnailResult result) { postThumbnailResult(std::move(result)); },
        options);
    beginThumbnailGeneration();
}

std::optional<odysea::core::ThumbnailKey>
DirectoryListModel::thumbnailKeyForEntry(const odysea::core::Entry& entry) const {
    std::error_code error;
    return odysea::core::thumbnail_key_for(entry.path, odysea::core::ThumbnailSize::Large, error);
}

void DirectoryListModel::requestThumbnail(int row) {
    if (thumbnailService_ == nullptr || thumbnailProvider_ == nullptr || row < 0 ||
        row >= rowCount()) {
        return;
    }

    const odysea::core::Entry& entry = entries_[static_cast<std::size_t>(row)];
    const std::optional<odysea::core::ThumbnailKey> thumbnailKey = thumbnailKeyForEntry(entry);
    if (!thumbnailKey.has_value()) {
        return;
    }

    const QString stableKey = entryKey(entry);
    const auto existing = requestedThumbnailKeys_.constFind(stableKey);
    if (existing != requestedThumbnailKeys_.cend() && *existing == *thumbnailKey) {
        return;
    }
    if (existing != requestedThumbnailKeys_.cend()) {
        removeThumbnailState(stableKey);
    }

    requestedThumbnailKeys_.insert(stableKey, *thumbnailKey);
    thumbnailLoadingKeys_.insert(stableKey);
    emit dataChanged(index(row), index(row), {ThumbnailLoadingRole});
    thumbnailService_->request(entry.path, *thumbnailKey, thumbnailGeneration_,
                               odysea::core::ThumbnailPriority::Visible);
}

void DirectoryListModel::releaseThumbnail(const QString& entryPath) {
    if (thumbnailService_ == nullptr) {
        return;
    }
    const QString stableKey = QDir::cleanPath(entryPath);
    if (!requestedThumbnailKeys_.contains(stableKey) && !thumbnailIds_.contains(stableKey)) {
        return;
    }
    removeThumbnailState(stableKey);
    const int row = rowForEntryKey(stableKey);
    if (row >= 0) {
        emit dataChanged(index(row), index(row), {ThumbnailSourceRole, ThumbnailLoadingRole});
    }
}

void DirectoryListModel::beginThumbnailGeneration() {
    if (thumbnailService_ != nullptr) {
        thumbnailGeneration_ = thumbnailService_->begin_generation();
        thumbnailService_->cancel_before(thumbnailGeneration_);
    }
    if (thumbnailProvider_ != nullptr) {
        for (const QString& id : std::as_const(thumbnailIds_)) {
            thumbnailProvider_->remove(id);
        }
    }
    requestedThumbnailKeys_.clear();
    thumbnailLoadingKeys_.clear();
    thumbnailIds_.clear();
}

void DirectoryListModel::removeThumbnailState(const QString& key) {
    const auto requested = requestedThumbnailKeys_.constFind(key);
    if (thumbnailService_ != nullptr && requested != requestedThumbnailKeys_.cend() &&
        thumbnailLoadingKeys_.contains(key)) {
        thumbnailService_->release(*requested, thumbnailGeneration_);
    }
    requestedThumbnailKeys_.remove(key);
    thumbnailLoadingKeys_.remove(key);

    const QString id = thumbnailIds_.take(key);
    if (!id.isEmpty() && thumbnailProvider_ != nullptr) {
        thumbnailProvider_->remove(id);
    }
}

void DirectoryListModel::reconcileThumbnails() {
    const QStringList keys = requestedThumbnailKeys_.keys();
    for (const QString& stableKey : keys) {
        const int row = rowForEntryKey(stableKey);
        if (row < 0) {
            removeThumbnailState(stableKey);
            continue;
        }
        const std::optional<odysea::core::ThumbnailKey> current =
            thumbnailKeyForEntry(entries_[static_cast<std::size_t>(row)]);
        if (!current.has_value() || *current != requestedThumbnailKeys_.value(stableKey)) {
            removeThumbnailState(stableKey);
            emit dataChanged(index(row), index(row), {ThumbnailSourceRole, ThumbnailLoadingRole});
            if (current.has_value()) {
                requestThumbnail(row);
            }
        }
    }
}

void DirectoryListModel::removeEvictedProviderImages(const QStringList& ids) {
    for (const QString& id : ids) {
        auto mapping = thumbnailIds_.cbegin();
        while (mapping != thumbnailIds_.cend() && mapping.value() != id) {
            ++mapping;
        }
        if (mapping == thumbnailIds_.cend()) {
            continue;
        }

        const QString stableKey = mapping.key();
        thumbnailIds_.remove(stableKey);
        requestedThumbnailKeys_.remove(stableKey);
        const int row = rowForEntryKey(stableKey);
        if (row >= 0) {
            emit dataChanged(index(row), index(row), {ThumbnailSourceRole});
        }
    }
}

void DirectoryListModel::postThumbnailResult(odysea::core::ThumbnailResult result) {
    if (!deliverCallbacks_.load(std::memory_order_acquire)) {
        return;
    }
    QMetaObject::invokeMethod(
        this,
        [this, result = std::move(result)]() mutable {
            if (deliverCallbacks_.load(std::memory_order_acquire)) {
                receiveThumbnailResult(std::move(result));
            }
        },
        Qt::QueuedConnection);
}

void DirectoryListModel::receiveThumbnailResult(odysea::core::ThumbnailResult result) {
    if (result.generation != thumbnailGeneration_ || thumbnailProvider_ == nullptr) {
        return;
    }

    const QString stableKey = entryKey(result.source);
    const auto requested = requestedThumbnailKeys_.constFind(stableKey);
    if (requested == requestedThumbnailKeys_.cend() || *requested != result.key) {
        return;
    }

    const int row = rowForEntryKey(stableKey);
    if (row < 0) {
        removeThumbnailState(stableKey);
        return;
    }
    thumbnailLoadingKeys_.remove(stableKey);

    if (result.error || result.image == nullptr || result.image->empty()) {
        emit dataChanged(index(row), index(row), {ThumbnailLoadingRole});
        return;
    }

    std::error_code conversionError;
    QImage image = thumbnailImageToQImage(*result.image, conversionError);
    if (conversionError || image.isNull()) {
        emit dataChanged(index(row), index(row), {ThumbnailLoadingRole});
        return;
    }

    const QByteArray identity = stableKey.toUtf8() + '\n' +
                                QByteArray::number(result.key.modified_seconds) + '\n' +
                                QByteArray::number(static_cast<qulonglong>(result.key.size)) +
                                '\n' + QByteArray::number(result.generation) + '\n' +
                                QByteArray::number(static_cast<qulonglong>(thumbnailOwnerId_));
    const QString id =
        QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    const QString previousId = thumbnailIds_.value(stableKey);
    if (!previousId.isEmpty() && previousId != id) {
        thumbnailProvider_->remove(previousId);
    }
    const ThumbnailImageProvider::InsertResult insertion =
        thumbnailProvider_->insert(id, std::move(image));
    removeEvictedProviderImages(insertion.evictedIds);
    if (insertion.retained) {
        thumbnailIds_.insert(stableKey, id);
    } else {
        requestedThumbnailKeys_.remove(stableKey);
    }
    emit dataChanged(index(row), index(row), {ThumbnailSourceRole, ThumbnailLoadingRole});
}
