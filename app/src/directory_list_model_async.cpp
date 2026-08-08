#include "directory_list_model.hpp"

#include <QHash>
#include <QMetaObject>

#include <algorithm>
#include <atomic>
#include <utility>

#include "entry_launcher.hpp"
#include "thumbnail_image_provider.hpp"

namespace {

std::uint64_t nextThumbnailOwnerId() noexcept {
    static std::atomic<std::uint64_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

} // namespace

DirectoryListModel::DirectoryListModel(QObject* parent)
    : QAbstractListModel(parent),
      watchService_([this](DirectoryWatchUpdate update) { postWatchUpdate(std::move(update)); }),
      thumbnailOwnerId_(nextThumbnailOwnerId()) {
    ownedEntryLauncher_ = std::make_unique<DesktopEntryLauncher>();
    entryLauncher_ = ownedEntryLauncher_.get();
    connect(&operationWatcher_, &QFutureWatcher<FilesystemOperationResult>::finished, this, [this] {
        auto future = operationWatcher_.future();
        finishOperation(future.takeResult());
    });
}

DirectoryListModel::DirectoryListModel(EntryLauncher& entryLauncher, QObject* parent)
    : DirectoryListModel(parent) {
    ownedEntryLauncher_.reset();
    entryLauncher_ = &entryLauncher;
}

DirectoryListModel::~DirectoryListModel() {
    deliverCallbacks_.store(false, std::memory_order_release);
    disconnect(&operationWatcher_, nullptr, this, nullptr);
    scanner_.cancel();
    watchService_.stop();
    scanner_.wait_idle();
    thumbnailService_.reset();
    for (const QString& id : std::as_const(thumbnailIds_)) {
        if (thumbnailProvider_ != nullptr) {
            thumbnailProvider_->remove(id);
        }
    }
}

void DirectoryListModel::startScan() {
    if (path_.isEmpty()) {
        scanner_.cancel();
        watchService_.replace({}, ++watchToken_);
        scannedPath_.clear();
        scannedEntries_.clear();
        scanBaselineEntries_.clear();
        scanEntries_.clear();
        applyPresentationSettings(true);
        setBusy(false);
        return;
    }

    if (scannedPath_ != path_) {
        scannedPath_ = path_;
        scannedEntries_.clear();
        applyPresentationSettings();
    }
    scanBaselineEntries_ = scannedEntries_;

    setBusy(true);
    setErrorString({});
    scanEntries_.clear();

    odysea::core::DirectoryScanner::Request request;
    request.directory = path_.toStdString();
    request.options = odysea::core::ListOptions{.show_hidden = true};
    request.batch_size = 128;
    request.on_batch = [this](std::uint64_t token, std::vector<odysea::core::Entry> entries) {
        postScanBatch(token, std::move(entries));
    };
    request.on_complete = [this](odysea::core::ScanSummary summary) {
        postScanComplete(std::move(summary));
    };
    activeScanToken_ = scanner_.start(std::move(request));
}

void DirectoryListModel::postScanBatch(std::uint64_t token,
                                       std::vector<odysea::core::Entry> entries) {
    if (!deliverCallbacks_.load(std::memory_order_acquire)) {
        return;
    }
    QMetaObject::invokeMethod(
        this,
        [this, token, entries = std::move(entries)]() mutable {
            if (deliverCallbacks_.load(std::memory_order_acquire)) {
                receiveScanBatch(token, std::move(entries));
            }
        },
        Qt::QueuedConnection);
}

void DirectoryListModel::postScanComplete(odysea::core::ScanSummary summary) {
    if (!deliverCallbacks_.load(std::memory_order_acquire)) {
        return;
    }
    QMetaObject::invokeMethod(
        this,
        [this, summary = std::move(summary)]() mutable {
            if (deliverCallbacks_.load(std::memory_order_acquire)) {
                receiveScanComplete(std::move(summary));
            }
        },
        Qt::QueuedConnection);
}

void DirectoryListModel::receiveScanBatch(std::uint64_t token,
                                          std::vector<odysea::core::Entry> entries) {
    if (token != activeScanToken_) {
        return;
    }
    // One key index per batch rather than a linear rescan per delivered
    // entry. The previous search rebuilt every candidate's key on every
    // comparison, which made a scan quadratic in the directory size on its
    // own. First occurrence wins, matching the search it replaces.
    QHash<QString, std::size_t> scannedRows;
    scannedRows.reserve(static_cast<qsizetype>(scannedEntries_.size()));
    for (std::size_t row = 0; row < scannedEntries_.size(); ++row) {
        const QString key = entryKey(scannedEntries_.at(row));
        if (!scannedRows.contains(key)) {
            scannedRows.insert(key, row);
        }
    }

    for (odysea::core::Entry& entry : entries) {
        const QString key = entryKey(entry);
        const auto existing = scannedRows.constFind(key);
        if (existing == scannedRows.constEnd()) {
            scannedRows.insert(key, scannedEntries_.size());
            scannedEntries_.push_back(entry);
        } else {
            scannedEntries_.at(existing.value()) = entry;
        }
        scanEntries_.push_back(std::move(entry));
    }
    applyPresentationSettings();
}

void DirectoryListModel::receiveScanComplete(odysea::core::ScanSummary summary) {
    if (summary.token != activeScanToken_ || summary.cancelled) {
        return;
    }

    QHash<QString, int> baselineIdentityCounts;
    QHash<QString, int> completedIdentityCounts;
    QHash<QString, QString> completedIdentityKeys;
    QSet<QString> completedKeys;
    for (const odysea::core::Entry& entry : scanBaselineEntries_) {
        const QString identity = entryIdentity(entry);
        if (!identity.isEmpty()) {
            ++baselineIdentityCounts[identity];
        }
    }
    for (const odysea::core::Entry& entry : scanEntries_) {
        const QString key = entryKey(entry);
        completedKeys.insert(key);
        const QString identity = entryIdentity(entry);
        if (!identity.isEmpty()) {
            ++completedIdentityCounts[identity];
            completedIdentityKeys[identity] = key;
        }
    }
    for (const odysea::core::Entry& entry : scanBaselineEntries_) {
        const QString oldKey = entryKey(entry);
        const QString identity = entryIdentity(entry);
        if (!completedKeys.contains(oldKey) && !identity.isEmpty() &&
            baselineIdentityCounts.value(identity) == 1 &&
            completedIdentityCounts.value(identity) == 1) {
            remapEntryKey(oldKey, completedIdentityKeys.value(identity));
        }
    }

    scannedEntries_ = std::move(scanEntries_);
    scanBaselineEntries_.clear();
    setErrorString(summary.error ? QString::fromStdString(summary.error.message()) : QString{});
    applyPresentationSettings(true);
    setBusy(false);

    if (summary.error) {
        watchService_.replace({}, ++watchToken_);
    } else {
        replaceWatch();
    }
}

void DirectoryListModel::replaceWatch() {
    watchService_.replace(path_.toStdString(), ++watchToken_);
}

void DirectoryListModel::postWatchUpdate(DirectoryWatchUpdate update) {
    if (!deliverCallbacks_.load(std::memory_order_acquire)) {
        return;
    }
    QMetaObject::invokeMethod(
        this,
        [this, update = std::move(update)]() mutable {
            if (deliverCallbacks_.load(std::memory_order_acquire)) {
                applyWatchUpdate(std::move(update));
            }
        },
        Qt::QueuedConnection);
}

void DirectoryListModel::applyWatchUpdate(DirectoryWatchUpdate update) {
    if (update.token != watchToken_ || QString::fromStdString(update.directory.string()) != path_) {
        return;
    }
    if (operationBusy_) {
        watchRefreshPending_ = true;
        return;
    }
    if (update.error) {
        setStatusMessage(
            tr("Folder watch error: %1").arg(QString::fromStdString(update.error.message())));
    }
    if (update.rescanRequired) {
        startScan();
        return;
    }

    QSet<QString> remappedOldKeys;
    const auto remapKey = [this, &remappedOldKeys](const odysea::core::Entry& oldEntry,
                                                   const odysea::core::Entry& newEntry) {
        const QString oldKey = entryKey(oldEntry);
        const QString newKey = entryKey(newEntry);
        remappedOldKeys.insert(oldKey);
        remapEntryKey(oldKey, newKey);
    };

    for (const DirectoryEntryRename& rename : update.renamedEntries) {
        const auto oldEntry =
            std::ranges::find(scannedEntries_, rename.oldName, &odysea::core::Entry::name);
        const auto newEntry =
            std::ranges::find(update.updatedEntries, rename.newName, &odysea::core::Entry::name);
        if (oldEntry != scannedEntries_.end() && newEntry != update.updatedEntries.end()) {
            remapKey(*oldEntry, *newEntry);
        }
    }

    for (const std::string& removedName : update.removedNames) {
        const auto oldEntry =
            std::ranges::find(scannedEntries_, removedName, &odysea::core::Entry::name);
        if (oldEntry == scannedEntries_.end() || remappedOldKeys.contains(entryKey(*oldEntry))) {
            continue;
        }

        // Follow a departed entry to its new name only when the identity picks
        // out exactly one entry on each side. Counting and matching both go
        // through the core, so the rule that an unknown identity matches
        // nothing is stated once rather than restated per call site.
        //
        // Requiring exactly one match on the updated side is also what makes
        // the search below safe to dereference. Relaxing either count to allow
        // more than one would leave the search able to return the end
        // iterator.
        const odysea::core::EntryIdentity& identity = oldEntry->identity;
        if (!identity.known()) {
            continue;
        }
        if (odysea::core::count_identity(scannedEntries_, identity) == 1 &&
            odysea::core::count_identity(update.updatedEntries, identity) == 1) {
            const auto newEntry =
                std::ranges::find_if(update.updatedEntries, [&identity](const auto& entry) {
                    return odysea::core::same_identity(entry.identity, identity);
                });
            remapKey(*oldEntry, *newEntry);
        }
    }

    for (const std::string& removedName : update.removedNames) {
        std::erase_if(scannedEntries_, [this, &removedName, &remappedOldKeys](const auto& entry) {
            if (entry.name != removedName) {
                return false;
            }
            const QString key = entryKey(entry);
            if (!remappedOldKeys.contains(key)) {
                selectedEntryKeys_.remove(key);
                if (currentEntryKey_ == key) {
                    currentEntryKey_.clear();
                }
                if (selectionAnchorKey_ == key) {
                    selectionAnchorKey_.clear();
                }
                rubberBandBaseKeys_.remove(key);
            }
            return true;
        });
    }

    for (odysea::core::Entry& updated : update.updatedEntries) {
        const QString key = entryKey(updated);
        const auto existing =
            std::ranges::find_if(scannedEntries_, [this, &updated, &key](const auto& entry) {
                return entryKey(entry) == key || entry.name == updated.name;
            });
        if (existing == scannedEntries_.end()) {
            scannedEntries_.push_back(std::move(updated));
        } else {
            *existing = std::move(updated);
        }
    }

    applyPresentationSettings();
}
