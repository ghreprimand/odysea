#include "directory_list_model.hpp"

#include <QMetaObject>

#include <algorithm>
#include <utility>

DirectoryListModel::DirectoryListModel(QObject* parent)
    : QAbstractListModel(parent),
      watchService_([this](DirectoryWatchUpdate update) { postWatchUpdate(std::move(update)); }) {
    connect(&operationWatcher_, &QFutureWatcher<FilesystemOperationResult>::finished, this, [this] {
        auto future = operationWatcher_.future();
        finishOperation(future.takeResult());
    });
}

DirectoryListModel::~DirectoryListModel() {
    deliverCallbacks_.store(false, std::memory_order_release);
    disconnect(&operationWatcher_, nullptr, this, nullptr);
    scanner_.cancel();
    watchService_.stop();
    scanner_.wait_idle();
}

void DirectoryListModel::startScan() {
    if (path_.isEmpty()) {
        scanner_.cancel();
        watchService_.replace({}, ++watchToken_);
        scannedEntries_.clear();
        scanEntries_.clear();
        applyPresentationSettings(true);
        setBusy(false);
        return;
    }

    setBusy(true);
    setErrorString({});
    scanEntries_.clear();
    scanReceivedBatch_ = false;

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
    scanReceivedBatch_ = true;
    scanEntries_.insert(scanEntries_.end(), std::make_move_iterator(entries.begin()),
                        std::make_move_iterator(entries.end()));
    scannedEntries_ = scanEntries_;
    applyPresentationSettings();
}

void DirectoryListModel::receiveScanComplete(odysea::core::ScanSummary summary) {
    if (summary.token != activeScanToken_ || summary.cancelled) {
        return;
    }

    if (!scanReceivedBatch_) {
        scanEntries_.clear();
    }
    scannedEntries_ = std::move(scanEntries_);
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
        if (selectedEntryKeys_.remove(oldKey)) {
            selectedEntryKeys_.insert(newKey);
        }
        if (currentEntryKey_ == oldKey) {
            currentEntryKey_ = newKey;
        }
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

        const QString identity = entryIdentity(*oldEntry);
        if (identity.isEmpty()) {
            continue;
        }
        const auto oldIdentityCount =
            std::ranges::count_if(scannedEntries_, [this, &identity](const auto& entry) {
                return entryIdentity(entry) == identity;
            });
        const auto newIdentityCount =
            std::ranges::count_if(update.updatedEntries, [this, &identity](const auto& entry) {
                return entryIdentity(entry) == identity;
            });
        if (oldIdentityCount == 1 && newIdentityCount == 1) {
            const auto newEntry =
                std::ranges::find_if(update.updatedEntries, [this, &identity](const auto& entry) {
                    return entryIdentity(entry) == identity;
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
