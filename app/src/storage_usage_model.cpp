#include "storage_usage_model.hpp"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QVariant>

#include <algorithm>
#include <limits>
#include <utility>

namespace odysea::app {
namespace {

[[nodiscard]] bool sameTotals(const core::UsageTotals& first, const core::UsageTotals& second) {
    return first.apparent_bytes == second.apparent_bytes &&
           first.allocated_bytes == second.allocated_bytes &&
           first.file_count == second.file_count &&
           first.directory_count == second.directory_count &&
           first.unreadable_directories == second.unreadable_directories &&
           first.deduplicated_entries == second.deduplicated_entries &&
           first.skipped_boundaries == second.skipped_boundaries;
}

[[nodiscard]] bool samePresentedChild(const core::UsageChild& first,
                                      const core::UsageChild& second) {
    return first.name == second.name && first.path == second.path && first.kind == second.kind &&
           first.finished == second.finished && sameTotals(first.totals, second.totals);
}

[[nodiscard]] QString kindLabel(core::EntryKind kind) {
    switch (kind) {
    case core::EntryKind::Directory:
        return QStringLiteral("folder");
    case core::EntryKind::File:
        return QStringLiteral("file");
    case core::EntryKind::Symlink:
        return QStringLiteral("symbolic link");
    case core::EntryKind::Other:
        return QStringLiteral("entry");
    }
    return QStringLiteral("entry");
}

[[nodiscard]] qulonglong asUnsigned(std::uintmax_t value) noexcept {
    constexpr auto maximum = static_cast<std::uintmax_t>(std::numeric_limits<qulonglong>::max());
    return static_cast<qulonglong>(std::min(value, maximum));
}

} // namespace

StorageUsageModel::StorageUsageModel(QObject* parent) : QAbstractListModel(parent) {}

int StorageUsageModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant StorageUsageModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const core::UsageChild& child = rows_.at(static_cast<std::size_t>(index.row()));
    switch (role) {
    case NameRole:
        return QString::fromStdString(child.name);
    case PathRole:
        return QString::fromStdString(child.path.string());
    case IsDirectoryRole:
        return child.kind == core::EntryKind::Directory;
    case KindLabelRole:
        return kindLabel(child.kind);
    case ApparentBytesRole:
        return asUnsigned(child.totals.apparent_bytes);
    case AllocatedBytesRole:
        return asUnsigned(child.totals.allocated_bytes);
    case ApparentTextRole:
        return formatBytes(asUnsigned(child.totals.apparent_bytes));
    case AllocatedTextRole:
        return formatBytes(asUnsigned(child.totals.allocated_bytes));
    case FileCountRole:
        return static_cast<qulonglong>(child.totals.file_count);
    case DirectoryCountRole:
        return static_cast<qulonglong>(child.totals.directory_count);
    case DeduplicatedEntriesRole:
        return static_cast<qulonglong>(child.totals.deduplicated_entries);
    case FinishedRole:
        return child.finished;
    case SelectedRole:
        return index.row() == currentIndex_;
    default:
        return {};
    }
}

QHash<int, QByteArray> StorageUsageModel::roleNames() const {
    return {{NameRole, "name"},
            {PathRole, "entryPath"},
            {IsDirectoryRole, "isDirectory"},
            {KindLabelRole, "kindLabel"},
            {ApparentBytesRole, "apparentBytes"},
            {AllocatedBytesRole, "allocatedBytes"},
            {ApparentTextRole, "apparentText"},
            {AllocatedTextRole, "allocatedText"},
            {FileCountRole, "fileCount"},
            {DirectoryCountRole, "directoryCount"},
            {DeduplicatedEntriesRole, "deduplicatedEntries"},
            {FinishedRole, "finished"},
            {SelectedRole, "selected"}};
}

Qt::ItemFlags StorageUsageModel::flags(const QModelIndex& index) const {
    return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
}

QString StorageUsageModel::rootPath() const {
    return rootPath_;
}

bool StorageUsageModel::busy() const noexcept {
    return busy_;
}

bool StorageUsageModel::cancelling() const noexcept {
    return cancelling_;
}

bool StorageUsageModel::cancelled() const noexcept {
    return cancelled_;
}

QString StorageUsageModel::errorString() const {
    return errorString_;
}

int StorageUsageModel::currentIndex() const noexcept {
    return currentIndex_;
}

bool StorageUsageModel::canGoUp() const {
    if (rootPath_.isEmpty()) {
        return false;
    }
    QDir directory(rootPath_);
    return directory.cdUp();
}

qulonglong StorageUsageModel::entriesVisited() const noexcept {
    return static_cast<qulonglong>(entriesVisited_);
}

qulonglong StorageUsageModel::apparentBytes() const noexcept {
    return asUnsigned(totals_.apparent_bytes);
}

qulonglong StorageUsageModel::allocatedBytes() const noexcept {
    return asUnsigned(totals_.allocated_bytes);
}

qulonglong StorageUsageModel::fileCount() const noexcept {
    return static_cast<qulonglong>(totals_.file_count);
}

qulonglong StorageUsageModel::directoryCount() const noexcept {
    return static_cast<qulonglong>(totals_.directory_count);
}

qulonglong StorageUsageModel::unreadableDirectories() const noexcept {
    return static_cast<qulonglong>(totals_.unreadable_directories);
}

qulonglong StorageUsageModel::deduplicatedEntries() const noexcept {
    return static_cast<qulonglong>(totals_.deduplicated_entries);
}

qulonglong StorageUsageModel::skippedBoundaries() const noexcept {
    return static_cast<qulonglong>(totals_.skipped_boundaries);
}

void StorageUsageModel::resetScanState(const QString& path) {
    if (rootPath_ != path) {
        rootPath_ = path;
        emit rootPathChanged();
    }
    if (!rows_.empty()) {
        beginResetModel();
        rows_.clear();
        endResetModel();
    }
    totals_ = {};
    entriesVisited_ = 0;
    errorString_.clear();
    cancelling_ = false;
    cancelled_ = false;
    activeProgressRow_ = -1;
    setCurrentIndex(-1);
    emit progressChanged();
    emit stateChanged();
}

void StorageUsageModel::start(const QString& path) {
    const QString cleanPath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    resetScanState(cleanPath);
    if (!busy_) {
        busy_ = true;
        emit busyChanged();
    }

    core::UsageOptions options;
    options.progress_interval = 256;
    activeToken_ = scanner_.start({.root = std::filesystem::path(cleanPath.toStdString()),
                                   .options = options,
                                   .on_progress =
                                       [this](std::uint64_t, core::UsageProgress progress) {
                                           enqueueProgress(std::move(progress));
                                       },
                                   .on_complete =
                                       [this](core::UsageSummary summary) {
                                           QMetaObject::invokeMethod(
                                               this,
                                               [this, summary = std::move(summary)]() mutable {
                                                   applyCompletion(std::move(summary));
                                               },
                                               Qt::QueuedConnection);
                                       }});
}

void StorageUsageModel::enqueueProgress(core::UsageProgress progress) {
    bool scheduleDelivery = false;
    {
        const std::scoped_lock lock(progressMutex_);
        pendingProgress_ = std::move(progress);
        if (!progressDeliveryScheduled_) {
            progressDeliveryScheduled_ = true;
            scheduleDelivery = true;
        }
    }
    if (scheduleDelivery) {
        QMetaObject::invokeMethod(this, [this] { deliverPendingProgress(); }, Qt::QueuedConnection);
    }
}

void StorageUsageModel::deliverPendingProgress() {
    std::optional<core::UsageProgress> progress;
    {
        const std::scoped_lock lock(progressMutex_);
        progress = std::move(pendingProgress_);
        pendingProgress_.reset();
        progressDeliveryScheduled_ = false;
    }
    if (progress) {
        applyProgress(*progress);
    }
}

void StorageUsageModel::cancel() {
    if (!busy_ || cancelling_) {
        return;
    }
    cancelling_ = true;
    emit stateChanged();
    scanner_.cancel();
}

void StorageUsageModel::selectRow(int row) {
    if (row < 0 || row >= rowCount()) {
        return;
    }
    setCurrentIndex(row);
}

void StorageUsageModel::moveCursor(int delta) {
    if (rows_.empty() || delta == 0) {
        return;
    }
    int origin = currentIndex_;
    if (origin < 0) {
        origin = delta > 0 ? -1 : rowCount();
    }
    selectRow(std::clamp(origin + delta, 0, rowCount() - 1));
}

void StorageUsageModel::activate(int row) {
    if (row < 0 || row >= rowCount()) {
        return;
    }
    const core::UsageChild& child = rows_.at(static_cast<std::size_t>(row));
    if (child.kind == core::EntryKind::Directory) {
        start(QString::fromStdString(child.path.string()));
    }
}

void StorageUsageModel::activateCurrent() {
    activate(currentIndex_);
}

void StorageUsageModel::goUp() {
    QDir directory(rootPath_);
    if (directory.cdUp()) {
        start(directory.absolutePath());
    }
}

bool StorageUsageModel::rowSelected(int row) const noexcept {
    return row >= 0 && row == currentIndex_;
}

QString StorageUsageModel::formatBytes(qulonglong bytes) {
    constexpr qulonglong kibibyte = 1024;
    constexpr qulonglong mebibyte = kibibyte * 1024;
    constexpr qulonglong gibibyte = mebibyte * 1024;
    constexpr qulonglong tebibyte = gibibyte * 1024;
    if (bytes < kibibyte) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    const auto formatted = [bytes](qulonglong unit, const QString& suffix) {
        return QStringLiteral("%1 %2")
            .arg(static_cast<double>(bytes) / static_cast<double>(unit), 0, 'f', 1)
            .arg(suffix);
    };
    if (bytes < mebibyte) {
        return formatted(kibibyte, QStringLiteral("KiB"));
    }
    if (bytes < gibibyte) {
        return formatted(mebibyte, QStringLiteral("MiB"));
    }
    if (bytes < tebibyte) {
        return formatted(gibibyte, QStringLiteral("GiB"));
    }
    return formatted(tebibyte, QStringLiteral("TiB"));
}

void StorageUsageModel::updateTotals(const core::UsageTotals& totals,
                                     std::uint64_t entriesVisited) {
    if (sameTotals(totals_, totals) && entriesVisited_ == entriesVisited) {
        return;
    }
    totals_ = totals;
    entriesVisited_ = entriesVisited;
    emit progressChanged();
}

void StorageUsageModel::advanceActiveProgressRow() {
    const int startRow = std::max(0, activeProgressRow_);
    activeProgressRow_ = -1;
    for (int row = startRow; row < rowCount(); ++row) {
        if (!rows_.at(static_cast<std::size_t>(row)).finished) {
            activeProgressRow_ = row;
            return;
        }
    }
}

void StorageUsageModel::applySnapshot(const std::vector<core::UsageChild>& children) {
    if (children.size() < rows_.size()) {
        replaceRows(children);
        return;
    }

    const int oldCount = rowCount();
    const int newCount = static_cast<int>(children.size());
    if (newCount > oldCount) {
        beginInsertRows({}, oldCount, newCount - 1);
        rows_.insert(rows_.end(), children.cbegin() + oldCount, children.cend());
        endInsertRows();
    }

    if (activeProgressRow_ < 0) {
        advanceActiveProgressRow();
    }
    if (activeProgressRow_ < 0 || activeProgressRow_ >= oldCount) {
        return;
    }

    const auto row = static_cast<std::size_t>(activeProgressRow_);
    if (!samePresentedChild(rows_.at(row), children.at(row))) {
        rows_.at(row) = children.at(row);
        emit dataChanged(index(activeProgressRow_), index(activeProgressRow_));
    }
    if (rows_.at(row).finished) {
        ++activeProgressRow_;
        advanceActiveProgressRow();
    }
}

void StorageUsageModel::replaceRows(std::vector<core::UsageChild> children) {
    const QString selectedPath =
        currentIndex_ >= 0 && currentIndex_ < rowCount()
            ? QString::fromStdString(
                  rows_.at(static_cast<std::size_t>(currentIndex_)).path.string())
            : QString();
    beginResetModel();
    rows_ = std::move(children);
    endResetModel();
    activeProgressRow_ = -1;

    int selectedRow = rows_.empty() ? -1 : 0;
    if (!selectedPath.isEmpty()) {
        const auto found =
            std::ranges::find_if(rows_, [&selectedPath](const core::UsageChild& child) {
                return QString::fromStdString(child.path.string()) == selectedPath;
            });
        if (found != rows_.end()) {
            selectedRow = static_cast<int>(std::distance(rows_.begin(), found));
        }
    }
    setCurrentIndex(selectedRow);
}

void StorageUsageModel::applyProgress(const core::UsageProgress& progress) {
    if (progress.token != activeToken_) {
        return;
    }
    applySnapshot(progress.children);
    updateTotals(progress.totals, progress.entries_visited);
    if (currentIndex_ < 0 && !rows_.empty()) {
        setCurrentIndex(0);
    }
}

void StorageUsageModel::applyCompletion(core::UsageSummary summary) {
    const QString completedPath = QString::fromStdString(summary.root.string());
    if (summary.cancelled) {
        emit scanCancelled(completedPath, static_cast<qulonglong>(summary.entries_visited));
    }
    if (summary.token != activeToken_) {
        return;
    }

    if (summary.cancelled) {
        applySnapshot(summary.children);
    } else {
        core::sort_usage_children(summary.children);
        replaceRows(std::move(summary.children));
    }
    updateTotals(summary.totals, summary.entries_visited);
    errorString_ = summary.error ? QString::fromStdString(summary.error.message()) : QString();
    cancelled_ = summary.cancelled;
    cancelling_ = false;
    busy_ = false;
    emit stateChanged();
    emit busyChanged();
    emit scanCompleted(completedPath, cancelled_);
}

void StorageUsageModel::setCurrentIndex(int row) {
    if (currentIndex_ == row) {
        return;
    }
    const int previous = currentIndex_;
    currentIndex_ = row;
    if (previous >= 0 && previous < rowCount()) {
        emit dataChanged(index(previous), index(previous), {SelectedRole});
    }
    if (currentIndex_ >= 0 && currentIndex_ < rowCount()) {
        emit dataChanged(index(currentIndex_), index(currentIndex_), {SelectedRole});
    }
    emit currentIndexChanged();
}

} // namespace odysea::app
