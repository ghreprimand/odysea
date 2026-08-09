#include "miller_columns_model.hpp"

#include <QDir>
#include <QFileInfo>
#include <QQmlEngine>
#include <QVariant>

#include <algorithm>
#include <utility>

#include "directory_list_model.hpp"
#include "entry_launcher.hpp"

namespace odysea::app {

MillerColumnsModel::MillerColumnsModel(QObject* parent)
    : QAbstractListModel(parent), ownedEntryLauncher_(std::make_unique<DesktopEntryLauncher>()),
      entryLauncher_(ownedEntryLauncher_.get()) {}

MillerColumnsModel::MillerColumnsModel(EntryLauncher& entryLauncher, QObject* parent)
    : QAbstractListModel(parent), entryLauncher_(&entryLauncher) {}

MillerColumnsModel::~MillerColumnsModel() = default;

int MillerColumnsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(columns_.size());
}

QVariant MillerColumnsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) {
        return {};
    }
    DirectoryListModel* const listing = modelAt(index.row());
    if (listing == nullptr) {
        return {};
    }
    switch (role) {
    case ListingModelRole:
        return QVariant::fromValue(static_cast<QObject*>(listing));
    case PathRole:
        return listing->path();
    case TitleRole: {
        const QFileInfo information(listing->path());
        const QString name = information.fileName();
        return name.isEmpty() ? QDir::rootPath() : name;
    }
    case ActiveRole:
        return index.row() == activeColumn_;
    case DepthRole:
        return index.row();
    default:
        return {};
    }
}

QHash<int, QByteArray> MillerColumnsModel::roleNames() const {
    return {{ListingModelRole, "listingModel"},
            {PathRole, "columnPath"},
            {TitleRole, "columnTitle"},
            {ActiveRole, "active"},
            {DepthRole, "depth"}};
}

QString MillerColumnsModel::rootPath() const {
    return rootPath_;
}

QAbstractItemModel* MillerColumnsModel::columns() noexcept {
    return this;
}

void MillerColumnsModel::setRootPath(const QString& path) {
    const QString normalized = path.isEmpty() ? QString{} : QDir::cleanPath(path);
    if (rootPath_ == normalized && !columns_.empty()) {
        return;
    }
    rootPath_ = normalized;
    emit rootPathChanged();
    resetColumns();
}

int MillerColumnsModel::liveColumnCount() const noexcept {
    return static_cast<int>(columns_.size());
}

int MillerColumnsModel::activeColumn() const noexcept {
    return activeColumn_;
}

QObject* MillerColumnsModel::activeListing() const {
    return modelAt(activeColumn_);
}

QString MillerColumnsModel::currentPath() const {
    DirectoryListModel* const listing = modelAt(activeColumn_);
    return listing == nullptr ? QString{} : listing->path();
}

bool MillerColumnsModel::showHidden() const noexcept {
    return showHidden_;
}

void MillerColumnsModel::setShowHidden(bool showHidden) {
    if (showHidden_ == showHidden) {
        return;
    }
    showHidden_ = showHidden;
    for (const auto& column : columns_) {
        column->setShowHidden(showHidden_);
    }
    emit showHiddenChanged();
}

QString MillerColumnsModel::filterText() const {
    return filterText_;
}

void MillerColumnsModel::setFilterText(const QString& filterText) {
    if (filterText_ == filterText) {
        return;
    }
    filterText_ = filterText;
    // A text filter describes the folder currently being examined, not every
    // ancestor in the path. Applying it to ancestors can hide the selected row
    // that anchors a descendant column while leaving that descendant visible.
    // Descendants are therefore released first and only the active listing is
    // filtered.
    truncateAfter(activeColumn_);
    if (DirectoryListModel* const listing = modelAt(activeColumn_); listing != nullptr) {
        listing->setFilterText(filterText_);
    }
    emit filterTextChanged();
}

int MillerColumnsModel::sortMode() const noexcept {
    return sortMode_;
}

void MillerColumnsModel::setSortMode(int sortMode) {
    const int bounded = std::clamp(sortMode, static_cast<int>(DirectoryListModel::SortByName),
                                   static_cast<int>(DirectoryListModel::SortByType));
    if (sortMode_ == bounded) {
        return;
    }
    sortMode_ = bounded;
    for (const auto& column : columns_) {
        column->setSortMode(sortMode_);
    }
    emit sortModeChanged();
}

QObject* MillerColumnsModel::columnModel(int column) const {
    return modelAt(column);
}

bool MillerColumnsModel::columnBusy(int column) const {
    DirectoryListModel* const listing = modelAt(column);
    return listing != nullptr && listing->busy();
}

int MillerColumnsModel::columnCurrentIndex(int column) const {
    DirectoryListModel* const listing = modelAt(column);
    return listing == nullptr ? -1 : listing->currentIndex();
}

int MillerColumnsModel::entryCount(int column) const {
    DirectoryListModel* const listing = modelAt(column);
    return listing == nullptr ? 0 : listing->rowCount();
}

// Both coordinates are part of the public QML model index.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
QString MillerColumnsModel::entryName(int column, int row) const {
    DirectoryListModel* const listing = modelAt(column);
    return listing == nullptr
               ? QString{}
               : listing->data(listing->index(row), DirectoryListModel::NameRole).toString();
}

// Both coordinates are part of the public QML model index.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
QString MillerColumnsModel::entryPath(int column, int row) const {
    DirectoryListModel* const listing = modelAt(column);
    return listing == nullptr
               ? QString{}
               : listing->data(listing->index(row), DirectoryListModel::PathRole).toString();
}

// Both coordinates are part of the public QML model index.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool MillerColumnsModel::entryIsDirectory(int column, int row) const {
    DirectoryListModel* const listing = modelAt(column);
    return listing != nullptr && listing->rowIsDirectory(row);
}

bool MillerColumnsModel::currentEntryIsDirectory() const {
    DirectoryListModel* const listing = modelAt(activeColumn_);
    return listing != nullptr && listing->rowIsDirectory(listing->currentIndex());
}

void MillerColumnsModel::select(int column, int row) {
    DirectoryListModel* const listing = modelAt(column);
    if (listing == nullptr || row < 0 || row >= listing->rowCount()) {
        return;
    }
    const bool directory = listing->rowIsDirectory(row);
    const QString path = entryPath(column, row);
    DirectoryListModel* const existingChild = modelAt(column + 1);
    const bool sameBranch = directory && existingChild != nullptr && existingChild->path() == path;
    if (!sameBranch) {
        truncateAfter(column);
    }
    setActiveColumn(column);
    listing->selectRow(row, Qt::NoModifier);
    if (directory && !sameBranch) {
        appendColumn(path);
    }
}

void MillerColumnsModel::moveWithin(int delta) {
    DirectoryListModel* const listing = modelAt(activeColumn_);
    if (listing == nullptr || listing->rowCount() == 0) {
        return;
    }
    const int current = listing->currentIndex();
    int target = std::clamp(current + delta, 0, listing->rowCount() - 1);
    if (current < 0) {
        target = delta < 0 ? listing->rowCount() - 1 : 0;
    }
    select(activeColumn_, target);
}

void MillerColumnsModel::moveToRow(int row) {
    select(activeColumn_, row);
}

void MillerColumnsModel::moveAcross(int delta) {
    if (liveColumnCount() == 0) {
        return;
    }
    setActiveColumn(std::clamp(activeColumn_ + delta, 0, liveColumnCount() - 1));
}

void MillerColumnsModel::activate(int column, int row) {
    DirectoryListModel* const listing = modelAt(column);
    if (listing == nullptr || row < 0 || row >= listing->rowCount()) {
        return;
    }
    const bool directory = listing->rowIsDirectory(row);
    const QString path = entryPath(column, row);
    select(column, row);
    if (directory) {
        setActiveColumn(std::min(column + 1, liveColumnCount() - 1));
        return;
    }
    emit fileActivated(path);
    listing->activate(row);
}

void MillerColumnsModel::activateCurrent() {
    DirectoryListModel* const listing = modelAt(activeColumn_);
    if (listing != nullptr) {
        activate(activeColumn_, listing->currentIndex());
    }
}

void MillerColumnsModel::collapseBack() {
    if (liveColumnCount() > 1) {
        collapseTo(liveColumnCount() - 2);
    }
}

void MillerColumnsModel::collapseTo(int column) {
    if (column < 0 || column >= liveColumnCount()) {
        return;
    }
    truncateAfter(column);
    setActiveColumn(column);
}

void MillerColumnsModel::setActiveColumn(int column) {
    if (column < 0 || column >= liveColumnCount() || activeColumn_ == column) {
        return;
    }
    const int previous = activeColumn_;
    if (DirectoryListModel* const previousListing = modelAt(previous); previousListing != nullptr) {
        previousListing->setFilterText({});
    }
    activeColumn_ = column;
    if (DirectoryListModel* const listing = modelAt(activeColumn_); listing != nullptr) {
        listing->setFilterText(filterText_);
    }
    if (previous >= 0 && previous < liveColumnCount()) {
        emit dataChanged(index(previous), index(previous), {ActiveRole});
    }
    emit dataChanged(index(activeColumn_), index(activeColumn_), {ActiveRole});
    emit activeColumnChanged();
    emit activeListingChanged();
    emit currentPathChanged();
}

DirectoryListModel* MillerColumnsModel::modelAt(int column) const {
    if (column < 0 || column >= liveColumnCount()) {
        return nullptr;
    }
    return columns_.at(static_cast<std::size_t>(column)).get();
}

std::unique_ptr<DirectoryListModel> MillerColumnsModel::makeListing() const {
    auto listing = std::make_unique<DirectoryListModel>(*entryLauncher_);
    // The vector is the sole owner. A parentless QObject exposed through QML
    // otherwise defaults to JavaScriptOwnership, making the engine a second
    // owner and causing a double deletion when a live columns scene tears down.
    QQmlEngine::setObjectOwnership(listing.get(), QQmlEngine::CppOwnership);
    applySettings(*listing);
    return listing;
}

void MillerColumnsModel::appendColumn(const QString& path) {
    const int row = liveColumnCount();
    auto listing = makeListing();
    if (row == activeColumn_) {
        listing->setFilterText(filterText_);
    }
    listing->setPath(path);
    beginInsertRows({}, row, row);
    columns_.push_back(std::move(listing));
    endInsertRows();
    emit columnCountChanged();
}

void MillerColumnsModel::truncateAfter(int column) {
    const int first = column + 1;
    const int last = liveColumnCount() - 1;
    if (first > last) {
        return;
    }
    beginRemoveRows({}, first, last);
    columns_.erase(columns_.begin() + first, columns_.end());
    endRemoveRows();
    emit columnCountChanged();
    if (activeColumn_ >= liveColumnCount()) {
        activeColumn_ = std::max(0, liveColumnCount() - 1);
        if (liveColumnCount() > 0) {
            modelAt(activeColumn_)->setFilterText(filterText_);
            emit dataChanged(index(activeColumn_), index(activeColumn_), {ActiveRole});
        }
        emit activeColumnChanged();
        emit activeListingChanged();
        emit currentPathChanged();
    }
}

void MillerColumnsModel::resetColumns() {
    beginResetModel();
    columns_.clear();
    activeColumn_ = 0;
    if (!rootPath_.isEmpty()) {
        auto listing = makeListing();
        listing->setFilterText(filterText_);
        listing->setPath(rootPath_);
        columns_.push_back(std::move(listing));
    }
    endResetModel();
    emit columnCountChanged();
    emit activeColumnChanged();
    emit activeListingChanged();
    emit currentPathChanged();
}

void MillerColumnsModel::applySettings(DirectoryListModel& model) const {
    model.setShowHidden(showHidden_);
    model.setSortMode(sortMode_);
}

} // namespace odysea::app
