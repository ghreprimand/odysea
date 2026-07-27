#include "directory_list_model.hpp"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrentRun>

#include <algorithm>
#include <system_error>
#include <utility>

namespace {

constexpr int minimumSortMode = DirectoryListModel::SortByName;
constexpr int maximumSortMode = DirectoryListModel::SortByType;

} // namespace

DirectoryListModel::DirectoryListModel(QObject* parent) : QAbstractListModel(parent) {}

int DirectoryListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(entries_.size());
}

QVariant DirectoryListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(entries_.size())) {
        return {};
    }

    const auto row = static_cast<std::size_t>(index.row());
    const odysea::core::Entry& entry = entries_[row];
    switch (role) {
    case NameRole:
        return QString::fromStdString(entry.name);
    case IsDirRole:
        return entry.is_directory();
    case SizeRole:
        return QVariant::fromValue<qulonglong>(entry.size);
    case PathRole:
        return QString::fromStdString(entry.path.string());
    case SelectedRole:
        return selectedRows_.contains(index.row());
    default:
        return {};
    }
}

QHash<int, QByteArray> DirectoryListModel::roleNames() const {
    return {{NameRole, "name"},
            {IsDirRole, "isDir"},
            {SizeRole, "size"},
            {PathRole, "entryPath"},
            {SelectedRole, "selected"}};
}

QString DirectoryListModel::path() const {
    return path_;
}

void DirectoryListModel::setPath(const QString& path) {
    navigateTo(path, true);
}

bool DirectoryListModel::busy() const noexcept {
    return busy_;
}

QString DirectoryListModel::errorString() const {
    return errorString_;
}

bool DirectoryListModel::showHidden() const noexcept {
    return showHidden_;
}

void DirectoryListModel::setShowHidden(bool showHidden) {
    if (showHidden_ == showHidden) {
        return;
    }
    showHidden_ = showHidden;
    emit showHiddenChanged();
    startScan();
}

QString DirectoryListModel::filterText() const {
    return filterText_;
}

void DirectoryListModel::setFilterText(const QString& filterText) {
    if (filterText_ == filterText) {
        return;
    }
    filterText_ = filterText;
    emit filterTextChanged();
    applyPresentationSettings();
}

int DirectoryListModel::sortMode() const noexcept {
    return sortMode_;
}

void DirectoryListModel::setSortMode(int sortMode) {
    const int boundedMode = std::clamp(sortMode, minimumSortMode, maximumSortMode);
    if (sortMode_ == boundedMode) {
        return;
    }
    sortMode_ = boundedMode;
    emit sortModeChanged();
    applyPresentationSettings();
}

int DirectoryListModel::currentIndex() const noexcept {
    return currentIndex_;
}

int DirectoryListModel::selectedCount() const noexcept {
    return static_cast<int>(selectedRows_.size());
}

bool DirectoryListModel::canGoBack() const {
    return !currentTab().backHistory.isEmpty();
}

bool DirectoryListModel::canGoForward() const {
    return !currentTab().forwardHistory.isEmpty();
}

bool DirectoryListModel::canGoUp() const {
    if (path_.isEmpty()) {
        return false;
    }
    const QDir directory(path_);
    return directory.absolutePath() != QDir::rootPath();
}

int DirectoryListModel::tabCount() const noexcept {
    return static_cast<int>(currentPane().tabs.size());
}

int DirectoryListModel::activeTab() const noexcept {
    return currentPane().activeTab;
}

int DirectoryListModel::paneCount() const noexcept {
    return paneCount_;
}

int DirectoryListModel::activePane() const noexcept {
    return activePane_;
}

QString DirectoryListModel::statusMessage() const {
    return statusMessage_;
}

void DirectoryListModel::refresh() {
    startScan();
}

void DirectoryListModel::goBack() {
    TabState& tab = currentTab();
    if (tab.backHistory.isEmpty()) {
        return;
    }
    tab.forwardHistory.push_back(tab.path);
    const QString target = tab.backHistory.takeLast();
    setCurrentPath(target);
}

void DirectoryListModel::goForward() {
    TabState& tab = currentTab();
    if (tab.forwardHistory.isEmpty()) {
        return;
    }
    tab.backHistory.push_back(tab.path);
    const QString target = tab.forwardHistory.takeLast();
    setCurrentPath(target);
}

void DirectoryListModel::goUp() {
    QDir parent(path_);
    if (!parent.cdUp()) {
        return;
    }
    navigateTo(parent.absolutePath(), true);
}

void DirectoryListModel::activate(int row) {
    if (row < 0 || row >= rowCount()) {
        return;
    }
    const odysea::core::Entry& entry = entries_[static_cast<std::size_t>(row)];
    const QString entryPath = QString::fromStdString(entry.path.string());
    if (entry.is_directory()) {
        navigateTo(entryPath, true);
        return;
    }
    emit openRequested(entryPath);
    setStatusMessage(tr("Opening files is waiting for the platform launcher hookup."));
}

void DirectoryListModel::selectRow(int row, int modifiers) {
    if (row < 0 || row >= rowCount()) {
        return;
    }

    const auto keyboardModifiers = static_cast<Qt::KeyboardModifiers>(modifiers);
    if (keyboardModifiers.testFlag(Qt::ShiftModifier)) {
        selectRangeTo(row);
    } else if (keyboardModifiers.testFlag(Qt::ControlModifier)) {
        QSet<int> selection = selectedRows_;
        if (selection.contains(row)) {
            selection.remove(row);
        } else {
            selection.insert(row);
        }
        selectionAnchor_ = row;
        replaceSelection(std::move(selection));
    } else {
        selectionAnchor_ = row;
        replaceSelection(QSet<int>{row});
    }
    setCurrentIndex(row);
}

void DirectoryListModel::moveCursor(int delta, bool extendSelection, bool preserveSelection) {
    if (rowCount() == 0) {
        return;
    }
    const int start = currentIndex_ >= 0 ? currentIndex_ : 0;
    moveCursorTo(std::clamp(start + delta, 0, rowCount() - 1), extendSelection, preserveSelection);
}

void DirectoryListModel::moveCursorTo(int row, bool extendSelection, bool preserveSelection) {
    if (row < 0 || row >= rowCount()) {
        return;
    }
    if (extendSelection) {
        selectRangeTo(row);
    } else if (!preserveSelection) {
        selectionAnchor_ = row;
        replaceSelection(QSet<int>{row});
    }
    setCurrentIndex(row);
}

void DirectoryListModel::toggleCurrent() {
    if (currentIndex_ < 0 || currentIndex_ >= rowCount()) {
        return;
    }
    QSet<int> selection = selectedRows_;
    if (selection.contains(currentIndex_)) {
        selection.remove(currentIndex_);
    } else {
        selection.insert(currentIndex_);
    }
    selectionAnchor_ = currentIndex_;
    replaceSelection(std::move(selection));
}

void DirectoryListModel::selectAll() {
    QSet<int> selection;
    for (int row = 0; row < rowCount(); ++row) {
        selection.insert(row);
    }
    replaceSelection(std::move(selection));
}

void DirectoryListModel::clearSelection() {
    replaceSelection({});
}

void DirectoryListModel::beginRubberBand(bool additive) {
    rubberBandActive_ = true;
    rubberBandBase_ = additive ? selectedRows_ : QSet<int>{};
    if (!additive) {
        replaceSelection({});
    }
}

void DirectoryListModel::updateRubberBand(int firstRow, int lastRow) {
    if (!rubberBandActive_ || rowCount() == 0) {
        return;
    }
    const int first = std::clamp(std::min(firstRow, lastRow), 0, rowCount() - 1);
    const int last = std::clamp(std::max(firstRow, lastRow), 0, rowCount() - 1);
    QSet<int> selection = rubberBandBase_;
    for (int row = first; row <= last; ++row) {
        selection.insert(row);
    }
    replaceSelection(std::move(selection));
    setCurrentIndex(lastRow >= firstRow ? last : first);
}

void DirectoryListModel::endRubberBand() {
    rubberBandActive_ = false;
    rubberBandBase_.clear();
}

QString DirectoryListModel::tabLabel(int tabIndex) const {
    const PaneState& pane = currentPane();
    if (tabIndex < 0 || tabIndex >= static_cast<int>(pane.tabs.size())) {
        return {};
    }
    const QString tabPath = pane.tabs[static_cast<std::size_t>(tabIndex)].path;
    const QString name = QFileInfo(tabPath).fileName();
    return name.isEmpty() ? QDir::rootPath() : name;
}

void DirectoryListModel::addTab() {
    PaneState& pane = currentPane();
    pane.tabs.push_back(TabState{.path = path_, .backHistory = {}, .forwardHistory = {}});
    pane.activeTab = static_cast<int>(pane.tabs.size()) - 1;
    emit tabsChanged();
    setCurrentPath(path_);
}

void DirectoryListModel::closeTab(int tabIndex) {
    PaneState& pane = currentPane();
    if (pane.tabs.size() <= 1 || tabIndex < 0 || tabIndex >= static_cast<int>(pane.tabs.size())) {
        return;
    }
    pane.tabs.erase(pane.tabs.begin() + tabIndex);
    if (pane.activeTab >= static_cast<int>(pane.tabs.size())) {
        pane.activeTab = static_cast<int>(pane.tabs.size()) - 1;
    } else if (tabIndex < pane.activeTab) {
        --pane.activeTab;
    }
    emit tabsChanged();
    setCurrentPath(currentTab().path);
}

void DirectoryListModel::activateTab(int tabIndex) {
    PaneState& pane = currentPane();
    if (tabIndex < 0 || tabIndex >= static_cast<int>(pane.tabs.size()) ||
        pane.activeTab == tabIndex) {
        return;
    }
    pane.activeTab = tabIndex;
    emit tabsChanged();
    setCurrentPath(currentTab().path);
}

void DirectoryListModel::setDualPaneEnabled(bool enabled) {
    const int requestedCount = enabled ? 2 : 1;
    if (paneCount_ == requestedCount) {
        return;
    }
    paneCount_ = requestedCount;
    if (paneCount_ == 2 && panes_[1].tabs.front().path.isEmpty()) {
        panes_[1].tabs.front().path = path_;
    }
    if (activePane_ >= paneCount_) {
        activePane_ = 0;
        setCurrentPath(currentTab().path);
    }
    emit panesChanged();
}

void DirectoryListModel::activatePane(int paneIndex) {
    if (paneIndex < 0 || paneIndex >= paneCount_ || activePane_ == paneIndex) {
        return;
    }
    activePane_ = paneIndex;
    emit panesChanged();
    emit tabsChanged();
    setCurrentPath(currentTab().path);
}

void DirectoryListModel::requestCopy() {
    requestOperation(QStringLiteral("copy"));
}

void DirectoryListModel::requestMove() {
    requestOperation(QStringLiteral("move"));
}

void DirectoryListModel::requestRename() {
    requestOperation(QStringLiteral("rename"));
}

void DirectoryListModel::requestTrash() {
    requestOperation(QStringLiteral("trash"));
}

DirectoryListModel::TabState& DirectoryListModel::currentTab() {
    PaneState& pane = currentPane();
    return pane.tabs[static_cast<std::size_t>(pane.activeTab)];
}

const DirectoryListModel::TabState& DirectoryListModel::currentTab() const {
    const PaneState& pane = currentPane();
    return pane.tabs[static_cast<std::size_t>(pane.activeTab)];
}

DirectoryListModel::PaneState& DirectoryListModel::currentPane() {
    return panes_[static_cast<std::size_t>(activePane_)];
}

const DirectoryListModel::PaneState& DirectoryListModel::currentPane() const {
    return panes_[static_cast<std::size_t>(activePane_)];
}

QString DirectoryListModel::normalizedPath(const QString& path) const {
    if (path.trimmed().isEmpty()) {
        return {};
    }
    return QDir::cleanPath(QDir(path).absolutePath());
}

QStringList DirectoryListModel::selectedPaths() const {
    QList<int> rows = selectedRows_.values();
    std::ranges::sort(rows);
    QStringList paths;
    paths.reserve(rows.size());
    for (const int row : rows) {
        if (row >= 0 && row < rowCount()) {
            paths.push_back(
                QString::fromStdString(entries_[static_cast<std::size_t>(row)].path.string()));
        }
    }
    return paths;
}

void DirectoryListModel::navigateTo(const QString& path, bool recordHistory) {
    const QString target = normalizedPath(path);
    if (target.isEmpty() || target == path_) {
        return;
    }

    TabState& tab = currentTab();
    if (recordHistory && !tab.path.isEmpty()) {
        tab.backHistory.push_back(tab.path);
        tab.forwardHistory.clear();
    }
    setCurrentPath(target);
}

void DirectoryListModel::setCurrentPath(const QString& path) {
    path_ = path;
    currentTab().path = path;
    emit pathChanged();
    emit navigationChanged();
    emit tabsChanged();
    startScan();
}

void DirectoryListModel::startScan() {
    if (path_.isEmpty()) {
        return;
    }

    const std::uint64_t generation = ++scanGeneration_;
    setBusy(true);
    setErrorString({});

    auto* const watcher = new QFutureWatcher<ScanResult>(this);
    connect(watcher, &QFutureWatcher<ScanResult>::finished, this, [this, watcher]() {
        auto future = watcher->future();
        applyScanResult(future.takeResult());
        watcher->deleteLater();
    });

    const std::string scanPath = path_.toStdString();
    const odysea::core::ListOptions options{.show_hidden = showHidden_};
    watcher->setFuture(QtConcurrent::run([scanPath, options, generation]() {
        std::error_code error;
        ScanResult result;
        result.generation = generation;
        result.entries = odysea::core::read_directory(scanPath, options, error);
        if (error) {
            result.errorMessage = error.message();
        }
        return result;
    }));
}

void DirectoryListModel::applyScanResult(ScanResult result) {
    if (result.generation != scanGeneration_) {
        return;
    }
    scannedEntries_ = std::move(result.entries);
    setErrorString(QString::fromStdString(result.errorMessage));
    applyPresentationSettings();
    setBusy(false);
}

void DirectoryListModel::applyPresentationSettings() {
    const QString needle = filterText_.trimmed();
    std::vector<odysea::core::Entry> presented;
    presented.reserve(scannedEntries_.size());
    for (const odysea::core::Entry& entry : scannedEntries_) {
        const QString name = QString::fromStdString(entry.name);
        if (needle.isEmpty() || name.contains(needle, Qt::CaseInsensitive)) {
            presented.push_back(entry);
        }
    }

    if (sortMode_ == SortBySize) {
        std::ranges::stable_sort(presented, [](const auto& left, const auto& right) {
            if (left.is_directory() != right.is_directory()) {
                return left.is_directory();
            }
            if (left.size != right.size) {
                return left.size < right.size;
            }
            return left.name < right.name;
        });
    } else if (sortMode_ == SortByType) {
        std::ranges::stable_sort(presented, [](const auto& left, const auto& right) {
            if (left.kind != right.kind) {
                return left.kind < right.kind;
            }
            return left.name < right.name;
        });
    }

    beginResetModel();
    entries_ = std::move(presented);
    selectedRows_.clear();
    rubberBandBase_.clear();
    currentIndex_ = entries_.empty() ? -1 : 0;
    selectionAnchor_ = currentIndex_;
    endResetModel();
    emit currentIndexChanged();
    emit selectedCountChanged();
}

void DirectoryListModel::setBusy(bool busy) {
    if (busy_ == busy) {
        return;
    }
    busy_ = busy;
    emit busyChanged();
}

void DirectoryListModel::setErrorString(const QString& errorString) {
    if (errorString_ == errorString) {
        return;
    }
    errorString_ = errorString;
    emit errorStringChanged();
}

void DirectoryListModel::setStatusMessage(const QString& statusMessage) {
    if (statusMessage_ == statusMessage) {
        return;
    }
    statusMessage_ = statusMessage;
    emit statusMessageChanged();
}

void DirectoryListModel::setCurrentIndex(int row) {
    if (currentIndex_ == row) {
        return;
    }
    currentIndex_ = row;
    emit currentIndexChanged();
}

void DirectoryListModel::replaceSelection(QSet<int> selection) {
    if (selectedRows_ == selection) {
        return;
    }
    selectedRows_ = std::move(selection);
    notifySelectionRoles();
    emit selectedCountChanged();
}

void DirectoryListModel::selectRangeTo(int row) {
    if (selectionAnchor_ < 0) {
        selectionAnchor_ = currentIndex_ >= 0 ? currentIndex_ : row;
    }
    const int first = std::min(selectionAnchor_, row);
    const int last = std::max(selectionAnchor_, row);
    QSet<int> selection;
    for (int selectedRow = first; selectedRow <= last; ++selectedRow) {
        selection.insert(selectedRow);
    }
    replaceSelection(std::move(selection));
}

void DirectoryListModel::notifySelectionRoles() {
    if (rowCount() == 0) {
        return;
    }
    emit dataChanged(index(0), index(rowCount() - 1), {SelectedRole});
}

void DirectoryListModel::requestOperation(const QString& operation) {
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) {
        setStatusMessage(tr("Select at least one item first."));
        return;
    }
    if (operation == QStringLiteral("rename") && paths.size() != 1) {
        setStatusMessage(tr("Rename requires exactly one selected item."));
        return;
    }
    emit filesystemOperationRequested(operation, paths);
    setStatusMessage(tr("%1 request is waiting for the core operation hookup.").arg(operation));
}
