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

// How many entries the scanner accumulates before it delivers them, and the
// smallest interval at which the model republishes. The two are the same
// number on purpose: publishing more often than the scanner delivers cannot
// show anything new, and publishing less often than one delivery would delay
// the first content on screen behind an arbitrary share of the directory.
constexpr std::size_t kScanBatchSize = 128;

} // namespace

DirectoryListModel::DirectoryListModel(QObject* parent)
    : QAbstractListModel(parent),
      watchService_([this](DirectoryWatchUpdate update) { postWatchUpdate(std::move(update)); }),
      thumbnailOwnerId_(nextThumbnailOwnerId()) {
    operationCallbackGate_->receiver = this;
    scanElapsedMilliseconds_ = [this] { return scanClock_.isValid() ? scanClock_.elapsed() : 0; };
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
    {
        const std::scoped_lock guard(operationCallbackGate_->mutex);
        operationCallbackGate_->receiver = nullptr;
    }
    if (operationControl_) {
        operationControl_->request_cancel();
    }
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
    // Entries held back from a superseded scan describe a listing this scan is
    // about to replace, so they are dropped rather than merged into it.
    pendingScanEntries_.clear();
    // Deliveries held for a superseded scan describe the listing this scan is
    // about to replace, for the same reason its held entries do.
    heldWatchUpdates_.clear();
    scanInFlight_ = false;
    pendingScanArmToken_ = 0;
    if (path_.isEmpty()) {
        scanner_.cancel();
        watchService_.replace({}, ++watchToken_);
        scannedPath_.clear();
        setScannedEntries({});
        scanBaselineEntries_.clear();
        scanEntries_.clear();
        applyPresentationSettings(true);
        setBusy(false);
        return;
    }

    if (scannedPath_ != path_) {
        scannedPath_ = path_;
        setScannedEntries({});
        applyPresentationSettings();
    }
    scanBaselineEntries_ = scannedEntries_;

    setBusy(true);
    setErrorString({});
    scanEntries_.clear();
    scanInFlight_ = true;
    // No reading is current until this one starts. A superseded reading is
    // still delivering while the watch is being established, and the scanner
    // issues no token of zero, so clearing the token here is what stops those
    // deliveries landing in the listing this scan has just emptied.
    activeScanToken_ = 0;

    // The watch is established before the directory is read, not after.
    //
    // A watch reports what happens after it exists, and a reading reports what
    // was there while it ran. Reading first and watching afterwards therefore
    // leaves an interval belonging to neither: a change made in it is not in
    // the listing that replaces the rows, and no event describes it, so it is
    // not late but absent until something forces another reading. Establishing
    // the watch first closes the interval from both sides, at the cost of one
    // thread hop before the first entry is read.
    //
    // Requesting the watch only queues the request, so the reading waits for
    // the service to report it armed rather than for the request to return.
    // The model is already busy by then, so it cannot report itself idle
    // during any part of this.
    pendingScanArmToken_ = replaceWatch();
}

void DirectoryListModel::beginArmedScan() {
    scanClock_.start();

    odysea::core::DirectoryScanner::Request request;
    request.directory = path_.toStdString();
    request.options = odysea::core::ListOptions{.show_hidden = true};
    request.batch_size = kScanBatchSize;
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

std::size_t DirectoryListModel::scanPublishInterval() const noexcept {
    // How many delivered entries a scan holds before it publishes them.
    //
    // Merging a batch into the scanned listing and reconciling the presented
    // rows both cost work proportional to the whole listing, not to the batch.
    // Publishing once per fixed-size batch therefore performs that
    // listing-sized work once per fixed number of entries, so a load costs the
    // square of the entry count however small each pass is made: the exponent
    // comes from the number of passes, not from what a pass does.
    //
    // Growing the interval with the listing makes each pass cover a share of
    // what is already presented. Successive passes then happen at sizes that
    // grow geometrically, which bounds their number by a logarithm of the
    // entry count and their summed cost by a constant multiple of it. The
    // floor is one delivered batch, so the first content still reaches the
    // view as soon as the scanner has produced any, and a refresh of an
    // already large listing does not republish it once per delivery.
    //
    // The cost of that is stated rather than implied: a share of the listing
    // is always waiting. Delivered entries stay invisible until they amount
    // to a quarter of what is presented, so a load of 32,000 entries reveals
    // its largest group 6,016 entries at a time and still holds 2,304 when
    // the scan completes. The bound is a fifth of the directory at any size.
    //
    // In time the same rule bounds the wait at a quarter of the scan so far,
    // and it does that at any speed: a source delivering at a steady rate
    // needs a quarter of the time it has already spent to produce another
    // quarter of the listing, whether that is 40 ms or 4 s. Slowness alone
    // scales the guarantee rather than breaking it. What breaks it is a rate
    // that falls partway through, because the wait is then set by a speed the
    // held entries were not delivered at. That case is scanHoldExpired's.
    constexpr std::size_t listingShareDivisor = 4;
    return std::max(kScanBatchSize, scannedEntries_.size() / listingShareDivisor);
}

bool DirectoryListModel::scanHoldExpired(qint64 holdStartMilliseconds,
                                         qint64 nowMilliseconds) noexcept {
    // Whether held entries have waited long enough to be published on time
    // alone, given when they began waiting and when the delivery asking the
    // question arrived. Both are milliseconds since the scan started.
    //
    // The rule is that nothing waits longer than the scan has already run.
    // Entries held from 400 ms are published by 800 ms, entries held from
    // 30 s by 60 s, and the floor covers the opening moments where that would
    // otherwise mean publishing on every delivery.
    //
    // It answers one case and costs nothing in the others. A source
    // delivering at a steady rate never reaches it, because the entry
    // interval already publishes after a quarter of the elapsed scan and this
    // asks for all of it; a 10,000-entry scan at 640 entries a second
    // publishes identically with the rule and without it. A source that
    // delivers 25,600 entries a second and then drops to 640 does reach it:
    // the longest a delivered entry stayed invisible falls from 2,410 ms to
    // 1,000 ms, for one additional publication out of sixteen.
    //
    // A fixed deadline is the obvious spelling and it is wrong, because it is
    // a feedback loop rather than a bound. A publication costs work
    // proportional to the whole listing, so it takes longer as the listing
    // grows; once one publication takes longer than the deadline, the next
    // delivery is already overdue and every delivery publishes. That is
    // exactly the fixed-interval publishing whose cost grows with the square
    // of the entry count. Measured on a 32,000-entry load: a 5 ms deadline
    // turned 22 publications into 223 and 244 ms into 5,524 ms, and a 25 ms
    // deadline into 79 publications and 2,895 ms, with key construction
    // growing 4.5x and 12.1x across a doubled directory against 2.0x for the
    // shipped rule.
    //
    // A deadline that grows with the elapsed scan outruns that: each
    // publication on time alone requires having waited as long as everything
    // before it, so their number is a logarithm of the scan's duration rather
    // than a multiple of it. The same 32,000-entry load with the rule forced
    // to start at 5 ms published 23 times in 280 ms.
    //
    // The question is asked when a delivery arrives, so a scanner that stops
    // delivering entirely is not covered: what it has already handed over
    // waits for its next delivery or for completion. Bounding that case needs
    // a timer rather than a rule, and a scanner producing nothing is a
    // condition the busy state already reports.
    return nowMilliseconds - holdStartMilliseconds >=
           std::max(kScanHoldFloorMilliseconds, holdStartMilliseconds);
}

void DirectoryListModel::drainPendingScanEntries() {
    if (pendingScanEntries_.empty()) {
        return;
    }
    for (odysea::core::Entry& entry : pendingScanEntries_) {
        mergeScannedEntry(entry);
        scanEntries_.push_back(std::move(entry));
    }
    pendingScanEntries_.clear();
}

void DirectoryListModel::receiveScanBatch(std::uint64_t token,
                                          std::vector<odysea::core::Entry> entries) {
    if (token != activeScanToken_) {
        return;
    }
    // Entries begin waiting when they land in an empty buffer, so the hold
    // start is written here rather than cleared wherever the buffer empties:
    // a value that is only ever read while entries are held cannot go stale.
    const qint64 arrived = scanElapsedMilliseconds_();
    if (pendingScanEntries_.empty()) {
        scanHoldStartMilliseconds_ = arrived;
    }
    pendingScanEntries_.insert(pendingScanEntries_.end(), std::make_move_iterator(entries.begin()),
                               std::make_move_iterator(entries.end()));
    if (pendingScanEntries_.size() < scanPublishInterval() &&
        !scanHoldExpired(scanHoldStartMilliseconds_, arrived)) {
        return;
    }
    drainPendingScanEntries();
    applyPresentationSettings();
}

void DirectoryListModel::receiveScanComplete(odysea::core::ScanSummary summary) {
    if (summary.token != activeScanToken_ || summary.cancelled) {
        return;
    }
    // Entries still held back have to reach the scanned listing before it is
    // treated as complete: the completion path replaces the listing outright,
    // so anything unpublished at this point would be lost rather than late.
    drainPendingScanEntries();

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

    setScannedEntries(std::move(scanEntries_));
    scanBaselineEntries_.clear();
    setErrorString(summary.error ? QString::fromStdString(summary.error.message()) : QString{});
    applyPresentationSettings(true);
    // The listing has replaced the presented rows, so deliveries held for its
    // duration no longer risk being discarded by it and stop being held.
    scanInFlight_ = false;

    if (summary.error) {
        // A reading that failed describes nothing to keep the watch over, and
        // the held deliveries describe a listing that was never published.
        heldWatchUpdates_.clear();
        watchService_.replace({}, ++watchToken_);
        setBusy(false);
        return;
    }

    // Changes made while the directory was being read are applied on top of
    // the completed listing before the model reports itself idle, so idle
    // never means a delivered change is still waiting.
    if (drainHeldWatchUpdates()) {
        // A rescan supersedes the listing that was just published, and it
        // reports busy for its own duration, so busy is not dropped between.
        startScan();
        return;
    }
    setBusy(false);
}

bool DirectoryListModel::drainHeldWatchUpdates() {
    std::vector<DirectoryWatchUpdate> held;
    held.swap(heldWatchUpdates_);
    for (DirectoryWatchUpdate& update : held) {
        if (update.rescanRequired) {
            // A rescan reads the directory again, which subsumes this
            // delivery and every one after it.
            return true;
        }
        ++heldWatchUpdatesApplied_;
        applyWatchUpdate(std::move(update));
    }
    return false;
}

std::uint64_t DirectoryListModel::replaceWatch() {
    watchService_.replace(path_.toStdString(), ++watchToken_);
    return watchToken_;
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
    if (update.armed) {
        // The watch now exists, so the directory can be read. This update
        // carries no changes; it carries only the failure to establish the
        // watch, when there was one, and the reading proceeds either way
        // rather than waiting for a watch that will not arrive.
        if (update.error) {
            setStatusMessage(
                tr("Folder watch error: %1").arg(QString::fromStdString(update.error.message())));
        }
        if (pendingScanArmToken_ == update.token) {
            pendingScanArmToken_ = 0;
            beginArmedScan();
        }
        return;
    }
    if (scanInFlight_) {
        heldWatchUpdates_.push_back(std::move(update));
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

    // Indexes over the delivered burst, built once. A burst can carry as many
    // entries as the directory holds, so probing it once per delivered name
    // would be quadratic in the burst rather than in the listing.
    QHash<QString, std::size_t> deliveredRowsByName;
    deliveredRowsByName.reserve(static_cast<qsizetype>(update.updatedEntries.size()));
    // Identity counts stand in for a linear count over each collection. The
    // spelling carries every field the core identity compares, and the fields
    // it omits when creation time is unknown are never written in that case,
    // so two entries share a spelling exactly when the core calls them one
    // entry. An unknown identity spells to nothing and is left out, which is
    // the same rule the core applies.
    QHash<QString, int> deliveredIdentityCounts;
    QHash<QString, std::size_t> deliveredRowsByIdentity;
    for (std::size_t row = 0; row < update.updatedEntries.size(); ++row) {
        const odysea::core::Entry& delivered = update.updatedEntries.at(row);
        const QString name = QString::fromStdString(delivered.name);
        if (!deliveredRowsByName.contains(name)) {
            deliveredRowsByName.insert(name, row);
        }
        const QString identity = entryIdentity(delivered);
        if (!identity.isEmpty()) {
            ++deliveredIdentityCounts[identity];
            deliveredRowsByIdentity.insert(identity, row);
        }
    }
    QHash<QString, int> scannedIdentityCounts;
    for (const odysea::core::Entry& scanned : scannedEntries_) {
        const QString identity = entryIdentity(scanned);
        if (!identity.isEmpty()) {
            ++scannedIdentityCounts[identity];
        }
    }

    QSet<QString> remappedNames;
    const auto remapEntry = [this, &remappedNames](const odysea::core::Entry& oldEntry,
                                                   const odysea::core::Entry& newEntry) {
        remappedNames.insert(QString::fromStdString(oldEntry.name));
        remapEntryKey(entryKey(oldEntry), entryKey(newEntry));
    };

    for (const DirectoryEntryRename& rename : update.renamedEntries) {
        const auto oldRow = scannedRowsByName_.constFind(QString::fromStdString(rename.oldName));
        const auto newRow = deliveredRowsByName.constFind(QString::fromStdString(rename.newName));
        if (oldRow != scannedRowsByName_.constEnd() && newRow != deliveredRowsByName.constEnd()) {
            remapEntry(scannedEntries_.at(oldRow.value()),
                       update.updatedEntries.at(newRow.value()));
        }
    }

    for (const std::string& removedName : update.removedNames) {
        const QString name = QString::fromStdString(removedName);
        const auto oldRow = scannedRowsByName_.constFind(name);
        if (oldRow == scannedRowsByName_.constEnd() || remappedNames.contains(name)) {
            continue;
        }

        // Follow a departed entry to its new name only when the identity picks
        // out exactly one entry on each side. An unknown identity matches
        // nothing, which is why it is absent from both counts rather than
        // counted as zero matches.
        //
        // Requiring exactly one match on the delivered side is also what makes
        // the lookup below safe: a unique count is what guarantees the row it
        // returns is the only entry that identity could mean.
        const QString identity = entryIdentity(scannedEntries_.at(oldRow.value()));
        if (identity.isEmpty()) {
            continue;
        }
        if (scannedIdentityCounts.value(identity) == 1 &&
            deliveredIdentityCounts.value(identity) == 1) {
            remapEntry(scannedEntries_.at(oldRow.value()),
                       update.updatedEntries.at(deliveredRowsByIdentity.value(identity)));
        }
    }

    QSet<QString> departedNames;
    departedNames.reserve(static_cast<qsizetype>(update.removedNames.size()));
    for (const std::string& removedName : update.removedNames) {
        const QString name = QString::fromStdString(removedName);
        departedNames.insert(name);
        const auto row = scannedRowsByName_.constFind(name);
        if (row == scannedRowsByName_.constEnd() || remappedNames.contains(name)) {
            continue;
        }
        // A departed entry that was not followed to a new name takes its
        // selection, cursor, anchor, and rubber-band membership with it.
        const QString key = entryKey(scannedEntries_.at(row.value()));
        selectedEntryKeys_.remove(key);
        if (currentEntryKey_ == key) {
            currentEntryKey_.clear();
        }
        if (selectionAnchorKey_ == key) {
            selectionAnchorKey_.clear();
        }
        rubberBandBaseKeys_.remove(key);
    }
    eraseScannedEntries(departedNames);

    for (odysea::core::Entry& updated : update.updatedEntries) {
        mergeScannedEntry(std::move(updated));
    }

    applyPresentationSettings();
}
