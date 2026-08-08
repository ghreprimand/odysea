#include "directory_list_model.hpp"

#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <ranges>
#include <utility>

#include "entry_launcher.hpp"
#include "odysea/core/file_operations.hpp"
#include "thumbnail_image_provider.hpp"

namespace {

constexpr int minimumSortMode = DirectoryListModel::SortByName;
constexpr int maximumSortMode = DirectoryListModel::SortByType;

bool entriesMatch(const odysea::core::Entry& left, const odysea::core::Entry& right) {
    return left.name == right.name && left.path == right.path && left.kind == right.kind &&
           left.size == right.size && left.identity == right.identity &&
           left.modified_seconds == right.modified_seconds &&
           left.target_is_directory == right.target_is_directory;
}

QString resolveNavigationText(const QString& input) {
    const QString trimmed = input.trimmed();
    if (trimmed == QStringLiteral("~")) {
        return QDir::homePath();
    }
    if (trimmed.startsWith(QStringLiteral("~/"))) {
        return QDir::cleanPath(QDir::homePath() + trimmed.sliced(1));
    }
    if (trimmed.startsWith(QLatin1Char('~')) || !QDir::isAbsolutePath(trimmed)) {
        return {};
    }
    return QDir::cleanPath(trimmed);
}

QString commonPrefix(const QStringList& values) {
    if (values.isEmpty()) {
        return {};
    }
    QString prefix = values.front();
    for (const QString& value : values) {
        while (!value.startsWith(prefix) && !prefix.isEmpty()) {
            prefix.chop(1);
        }
    }
    return prefix;
}

} // namespace

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
    case IsSymlinkRole:
        return entry.kind == odysea::core::EntryKind::Symlink;
    case SizeRole:
        return QVariant::fromValue<qulonglong>(entry.size);
    case PathRole:
        return QString::fromStdString(entry.path.string());
    case SelectedRole:
        return selectedRows_.contains(index.row());
    case RecoveryEntryRole:
        return odysea::core::classify_working_entry(entry.name) !=
               odysea::core::WorkingEntryRole::None;
    case ThumbnailSourceRole: {
        const QString id = thumbnailIds_.value(keyForRow(index.row()));
        return id.isEmpty() ? QString{} : ThumbnailImageProvider::sourceUrl(id);
    }
    case ThumbnailLoadingRole:
        return thumbnailLoadingKeys_.contains(keyForRow(index.row()));
    default:
        return {};
    }
}

QHash<int, QByteArray> DirectoryListModel::roleNames() const {
    return {{NameRole, "name"},
            {IsDirRole, "isDir"},
            {IsSymlinkRole, "isSymlink"},
            {SizeRole, "size"},
            {PathRole, "entryPath"},
            {SelectedRole, "selected"},
            {RecoveryEntryRole, "recoveryEntry"},
            {ThumbnailSourceRole, "thumbnailSource"},
            {ThumbnailLoadingRole, "thumbnailLoading"}};
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
    applyPresentationSettings();
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
    std::error_code error;
    if (entryLauncher_ == nullptr || !entryLauncher_->open(entry.path, error)) {
        setStatusMessage(tr("Could not open %1 with the default application.").arg(entryPath));
        return;
    }
    setStatusMessage(tr("Opened %1.").arg(entryPath));
}

void DirectoryListModel::activateCurrent() {
    activate(currentIndex_);
}

void DirectoryListModel::navigateToPath(const QString& path) {
    navigateTo(path, true);
}

bool DirectoryListModel::navigateFromInput(const QString& input) {
    const QString target = resolveNavigationInput(input);
    if (target.isEmpty()) {
        setStatusMessage(tr("Enter an absolute path or a path beginning with ~/."));
        return false;
    }
    const QFileInfo information(target);
    if (!information.exists() || !information.isDir() || !information.isReadable()) {
        setStatusMessage(tr("Location is not a reachable directory: %1").arg(input.trimmed()));
        return false;
    }
    navigateTo(target, true);
    setStatusMessage(tr("Opened %1.").arg(target));
    return true;
}

QString DirectoryListModel::resolveNavigationInput(const QString& input) {
    return resolveNavigationText(input);
}

QVariantMap DirectoryListModel::navigationCompletion(const QString& input) {
    QVariantMap result{{QStringLiteral("completed"), input},
                       {QStringLiteral("suffix"), QString()},
                       {QStringLiteral("candidates"), QStringList{}}};
    const QString trimmed = input.trimmed();
    if (trimmed == QStringLiteral("~")) {
        result.insert(QStringLiteral("completed"), QStringLiteral("~/"));
        result.insert(QStringLiteral("suffix"), QStringLiteral("/"));
        return result;
    }
    const QString expanded = resolveNavigationInput(trimmed);
    if (expanded.isEmpty()) {
        return result;
    }

    const qsizetype separator = expanded.lastIndexOf(QLatin1Char('/'));
    const QString parentPath = separator <= 0 ? QDir::rootPath() : expanded.left(separator);
    const QString leaf = expanded.sliced(separator + 1);
    QDir parent(parentPath);
    if (!parent.exists() || !parent.isReadable()) {
        return result;
    }
    const QStringList entries =
        parent.entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    QStringList matches;
    for (const QString& entry : entries) {
        if (entry.startsWith(leaf, Qt::CaseSensitive)) {
            matches.push_back(entry);
        }
    }
    if (matches.isEmpty()) {
        return result;
    }

    const qsizetype displaySeparator = trimmed.lastIndexOf(QLatin1Char('/'));
    QString completed = trimmed.left(displaySeparator + 1) + commonPrefix(matches);
    if (matches.size() == 1) {
        completed += QLatin1Char('/');
    }
    result.insert(QStringLiteral("completed"), completed);
    result.insert(QStringLiteral("suffix"), completed.sliced(trimmed.size()));
    result.insert(QStringLiteral("candidates"), matches);
    return result;
}

QVariantList DirectoryListModel::breadcrumbSegments() const {
    QVariantList segments;
    const QString clean = QDir::cleanPath(path_);
    if (clean.isEmpty() || !QDir::isAbsolutePath(clean)) {
        return segments;
    }

    QVariantMap root;
    root.insert(QStringLiteral("label"), QStringLiteral("/"));
    root.insert(QStringLiteral("path"), QStringLiteral("/"));
    root.insert(QStringLiteral("url"), QUrl::fromLocalFile(QStringLiteral("/")).toString());
    segments.push_back(root);

    QString accumulated;
    const QStringList names = clean.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& name : names) {
        accumulated += QLatin1Char('/') + name;
        QVariantMap segment;
        segment.insert(QStringLiteral("label"), name);
        segment.insert(QStringLiteral("path"), accumulated);
        segment.insert(QStringLiteral("url"), QUrl::fromLocalFile(accumulated).toString());
        segments.push_back(segment);
    }
    return segments;
}

QStringList DirectoryListModel::selectedFileUrls() const {
    QStringList urls;
    for (const QString& selectedPath : selectedPaths()) {
        urls.push_back(QUrl::fromLocalFile(selectedPath).toString(QUrl::FullyEncoded));
    }
    return urls;
}

bool DirectoryListModel::rowSelected(int row) const {
    return row >= 0 && row < rowCount() && selectedRows_.contains(row);
}

bool DirectoryListModel::rowIsDirectory(int row) const {
    return row >= 0 && row < rowCount() && entries_[static_cast<std::size_t>(row)].is_directory();
}

void DirectoryListModel::selectRow(int row, Qt::KeyboardModifiers modifiers) {
    if (row < 0 || row >= rowCount()) {
        return;
    }

    if (modifiers.testFlag(Qt::ShiftModifier)) {
        selectRangeTo(row);
    } else if (modifiers.testFlag(Qt::ControlModifier)) {
        QSet<int> selection = selectedRows_;
        if (selection.contains(row)) {
            selection.remove(row);
        } else {
            selection.insert(row);
        }
        selectionAnchorKey_ = keyForRow(row);
        replaceSelection(std::move(selection));
    } else {
        selectionAnchorKey_ = keyForRow(row);
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
        selectionAnchorKey_ = keyForRow(row);
        replaceSelection(QSet<int>{row});
    }
    setCurrentIndex(row);
}

bool DirectoryListModel::selectByPrefix(const QString& prefix, bool cycle) {
    const int count = rowCount();
    if (prefix.isEmpty() || count <= 0) {
        return false;
    }

    const int current = currentIndex_ >= 0 && currentIndex_ < count ? currentIndex_ : 0;
    const int first = cycle ? (current + 1) % count : current;
    for (int offset = 0; offset < count; ++offset) {
        const int row = (first + offset) % count;
        const QString name =
            QString::fromStdString(entries_.at(static_cast<std::size_t>(row)).name);
        if (name.startsWith(prefix, Qt::CaseInsensitive)) {
            moveCursorTo(row, false, false);
            return true;
        }
    }
    return false;
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
    selectionAnchorKey_ = keyForRow(currentIndex_);
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
    rubberBandBaseKeys_ = additive ? selectedEntryKeys_ : QSet<QString>{};
    if (!additive) {
        replaceSelection({});
    }
}

void DirectoryListModel::updateRubberBandSelection(const QVariantList& rows, int currentRow) {
    if (!rubberBandActive_) {
        return;
    }

    QSet<QString> keys = rubberBandBaseKeys_;
    for (const QVariant& value : rows) {
        bool converted = false;
        const int row = value.toInt(&converted);
        if (converted && row >= 0 && row < rowCount()) {
            keys.insert(keyForRow(row));
        }
    }
    replaceSelectionKeys(std::move(keys));
    if (currentRow >= 0 && currentRow < rowCount()) {
        setCurrentIndex(currentRow);
    }
}

void DirectoryListModel::endRubberBand() {
    rubberBandActive_ = false;
    rubberBandBaseKeys_.clear();
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

QString DirectoryListModel::entryKey(const odysea::core::Entry& entry) const {
    ++entryKeyBuilds_;
    return QString::fromStdString(entry.path.lexically_normal().string());
}

QString DirectoryListModel::entryIdentity(const odysea::core::Entry& entry) const {
    // A hashable spelling of the core identity, so reconciliation can group by
    // it. Every field the core identity carries has to appear here: dropping
    // the creation time would let a recycled device and inode pair match the
    // entry that previously held it, and reconciliation would move selection
    // onto an unrelated entry. An unknown identity spells to an empty string,
    // which callers treat as "cannot follow this entry".
    const odysea::core::EntryIdentity& identity = entry.identity;
    if (!identity.known()) {
        return {};
    }
    if (!identity.birth_known) {
        return QStringLiteral("inode:%1:%2")
            .arg(static_cast<qulonglong>(identity.device))
            .arg(static_cast<qulonglong>(identity.inode));
    }
    return QStringLiteral("inode:%1:%2:%3.%4")
        .arg(static_cast<qulonglong>(identity.device))
        .arg(static_cast<qulonglong>(identity.inode))
        .arg(static_cast<qlonglong>(identity.birth_seconds))
        .arg(static_cast<qulonglong>(identity.birth_nanoseconds));
}

QString DirectoryListModel::keyForRow(int row) const {
    if (row < 0) {
        return {};
    }
    const auto offset = static_cast<std::size_t>(row);
    if (offset >= entryKeys_.size()) {
        return {};
    }
    return entryKeys_.at(offset);
}

int DirectoryListModel::rowForEntryKey(const QString& key) const {
    if (key.isEmpty()) {
        return -1;
    }
    return entryRowsByKey_.value(key, -1);
}

void DirectoryListModel::setScannedEntries(std::vector<odysea::core::Entry> entries) {
    scannedEntries_ = std::move(entries);
    rebuildScannedRowIndex();
}

void DirectoryListModel::mergeScannedEntry(odysea::core::Entry entry) {
    const QString name = QString::fromStdString(entry.name);
    const auto existing = scannedRowsByName_.constFind(name);
    if (existing == scannedRowsByName_.constEnd()) {
        scannedRowsByName_.insert(name, scannedEntries_.size());
        scannedEntries_.push_back(std::move(entry));
        return;
    }
    // A name already in the listing describes the same entry seen again, so
    // the later reading replaces the earlier one. Appending instead would
    // present the entry twice for as long as the listing stood.
    scannedEntries_.at(existing.value()) = std::move(entry);
}

void DirectoryListModel::eraseScannedEntries(const QSet<QString>& names) {
    if (names.isEmpty()) {
        return;
    }
    // One pass over the listing for the whole removal set, rather than one
    // pass per removed name. Removing renumbers every later row, so the index
    // is rebuilt once afterwards rather than repaired per removal.
    const std::size_t before = scannedEntries_.size();
    std::erase_if(scannedEntries_, [&names](const odysea::core::Entry& entry) {
        return names.contains(QString::fromStdString(entry.name));
    });
    if (scannedEntries_.size() != before) {
        rebuildScannedRowIndex();
    }
}

void DirectoryListModel::rebuildScannedRowIndex() {
    scannedRowsByName_.clear();
    scannedRowsByName_.reserve(static_cast<qsizetype>(scannedEntries_.size()));
    for (std::size_t row = 0; row < scannedEntries_.size(); ++row) {
        const QString name = QString::fromStdString(scannedEntries_.at(row).name);
        if (!scannedRowsByName_.contains(name)) {
            scannedRowsByName_.insert(name, row);
        }
    }
}

void DirectoryListModel::setEntryRows(std::vector<odysea::core::Entry> entries,
                                      std::vector<QString> keys) {
    entries_ = std::move(entries);
    entryKeys_ = std::move(keys);
    rebuildEntryRowIndex();
}

void DirectoryListModel::eraseEntryRows(int first, int last) {
    const auto begin = static_cast<std::ptrdiff_t>(first);
    const auto end = static_cast<std::ptrdiff_t>(last) + 1;
    entries_.erase(entries_.begin() + begin, entries_.begin() + end);
    entryKeys_.erase(entryKeys_.begin() + begin, entryKeys_.begin() + end);
    rebuildEntryRowIndex();
}

void DirectoryListModel::appendEntryRows(std::vector<odysea::core::Entry> entries,
                                         std::vector<QString> keys) {
    entries_.insert(entries_.end(), std::make_move_iterator(entries.begin()),
                    std::make_move_iterator(entries.end()));
    entryKeys_.insert(entryKeys_.end(), std::make_move_iterator(keys.begin()),
                      std::make_move_iterator(keys.end()));
    rebuildEntryRowIndex();
}

void DirectoryListModel::reorderEntryRows(const std::vector<std::size_t>& order) {
    std::vector<odysea::core::Entry> reorderedEntries;
    std::vector<QString> reorderedKeys;
    reorderedEntries.reserve(order.size());
    reorderedKeys.reserve(order.size());
    for (const std::size_t row : order) {
        reorderedEntries.push_back(std::move(entries_.at(row)));
        reorderedKeys.push_back(std::move(entryKeys_.at(row)));
    }
    entries_ = std::move(reorderedEntries);
    entryKeys_ = std::move(reorderedKeys);
    rebuildEntryRowIndex();
}

void DirectoryListModel::rebuildEntryRowIndex() {
    // Counted for the same reason key construction is: this pass touches
    // every row, so how many times an update performs it carries the shape of
    // the update's cost, and a count is machine-independent where wall clock
    // is not.
    ++entryRowIndexBuilds_;
    // First occurrence wins, because that is the row the linear searches this
    // index replaced would have returned. Two rows can carry one key while a
    // rename remap is pending, and a last-wins index would quietly point
    // reconciliation and thumbnail delivery at the wrong row.
    entryRowsByKey_.clear();
    entryRowsByKey_.reserve(static_cast<qsizetype>(entryKeys_.size()));
    for (std::size_t row = 0; row < entryKeys_.size(); ++row) {
        const QString& key = entryKeys_.at(row);
        if (!entryRowsByKey_.contains(key)) {
            entryRowsByKey_.insert(key, static_cast<int>(row));
        }
    }
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
    if (path_ != path) {
        replaceSelection({});
        setCurrentIndex(-1);
        selectionAnchorKey_.clear();
        beginThumbnailGeneration();
    }
    path_ = path;
    currentTab().path = path;
    emit pathChanged();
    emit navigationChanged();
    emit tabsChanged();
    startScan();
}

void DirectoryListModel::applyPresentationSettings(bool finalScanBatch) {
    const QStringList previousSelectedFileUrls = selectedFileUrls();
    const QString needle = filterText_.trimmed();
    std::vector<odysea::core::Entry> presented;
    presented.reserve(scannedEntries_.size());
    for (const odysea::core::Entry& entry : scannedEntries_) {
        const QString name = QString::fromStdString(entry.name);
        if (!showHidden_ && odysea::core::is_hidden_name(entry.name)) {
            continue;
        }
        if (needle.isEmpty() || name.contains(needle, Qt::CaseInsensitive)) {
            presented.push_back(entry);
        }
    }

    if (sortMode_ == SortByName) {
        odysea::core::sort_entries(presented);
    } else if (sortMode_ == SortBySize) {
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

    if (finalScanBatch) {
        QSet<QString> availableKeys;
        availableKeys.reserve(static_cast<qsizetype>(scannedEntries_.size()));
        for (const odysea::core::Entry& entry : scannedEntries_) {
            availableKeys.insert(entryKey(entry));
        }
        selectedEntryKeys_.intersect(availableKeys);
        if (!currentEntryKey_.isEmpty() && !availableKeys.contains(currentEntryKey_)) {
            currentEntryKey_.clear();
        }
        if (!selectionAnchorKey_.isEmpty() && !availableKeys.contains(selectionAnchorKey_)) {
            selectionAnchorKey_.clear();
        }
        rubberBandBaseKeys_.intersect(availableKeys);
    }

    const int previousCurrent = currentIndex_;
    const int previousSelectedCount = selectedCount();
    const QSet<int> previousSelectedRows = selectedRows_;

    // Every key below is built exactly once per entry per update and then
    // reused. Building one costs a path normalization and a string
    // allocation, so recomputing it inside a comparison made reconciliation
    // quadratic in key construction, not merely in comparisons.
    std::vector<QString> presentedKeys;
    presentedKeys.reserve(presented.size());
    for (const odysea::core::Entry& entry : presented) {
        presentedKeys.push_back(entryKey(entry));
    }

    QSet<QString> targetKeys;
    targetKeys.reserve(static_cast<qsizetype>(presentedKeys.size()));
    for (const QString& key : presentedKeys) {
        targetKeys.insert(key);
    }

    const QHash<QString, QString> resolvedKeyRemaps = pendingEntryKeyRemaps_;
    const auto resolveKey = [&resolvedKeyRemaps](const QString& key) {
        return resolvedKeyRemaps.value(key, key);
    };

    // Resolved keys for the rows currently presented, held parallel to
    // entries_ across the removals and insertions below.
    std::vector<QString> currentKeys;
    currentKeys.reserve(entryKeys_.size());
    for (const QString& key : entryKeys_) {
        currentKeys.push_back(resolveKey(key));
    }

    // Departing rows are published as one contiguous removal.
    //
    // Publishing each contiguous run separately cost a full key-index rebuild
    // and a full selection rebuild per run, because both are derived from
    // every row and a removal renumbers every row after it. A filter removes
    // a scattered set, so the number of runs grows with the listing and those
    // rebuilds turned filtering a large directory into work proportional to
    // its size squared — while removing the same number of rows in one block
    // stayed linear.
    //
    // Moving the departing rows to the end first makes the removal a single
    // suffix, so the derived state is rebuilt a fixed number of times per
    // update rather than once per run. The move is published as a reorder,
    // which is what it is, and which is a signal this model already emits
    // when sorting changes.
    std::vector<std::size_t> survivingRows;
    std::vector<std::size_t> departingRows;
    survivingRows.reserve(currentKeys.size());
    for (std::size_t row = 0; row < currentKeys.size(); ++row) {
        if (targetKeys.contains(currentKeys.at(row))) {
            survivingRows.push_back(row);
        } else {
            departingRows.push_back(row);
        }
    }

    if (!departingRows.empty()) {
        // A removal that is already a suffix needs no reorder, so a removal
        // at the end of the listing still publishes exactly one signal.
        if (departingRows.front() != survivingRows.size()) {
            std::vector<std::size_t> order = survivingRows;
            order.insert(order.end(), departingRows.begin(), departingRows.end());

            const QModelIndexList movedPersistentIndexes = persistentIndexList();
            std::vector<int> rowAfterMove(order.size(), -1);
            for (std::size_t position = 0; position < order.size(); ++position) {
                rowAfterMove.at(order.at(position)) = static_cast<int>(position);
            }

            emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
            reorderEntryRows(order);
            std::vector<QString> reorderedKeys;
            reorderedKeys.reserve(order.size());
            for (const std::size_t row : order) {
                reorderedKeys.push_back(currentKeys.at(row));
            }
            currentKeys = std::move(reorderedKeys);
            rebuildSelectionRows();

            QModelIndexList relocatedPersistentIndexes;
            relocatedPersistentIndexes.reserve(movedPersistentIndexes.size());
            for (const QModelIndex& persistent : movedPersistentIndexes) {
                relocatedPersistentIndexes.push_back(
                    index(rowAfterMove.at(static_cast<std::size_t>(persistent.row())),
                          persistent.column()));
            }
            changePersistentIndexList(movedPersistentIndexes, relocatedPersistentIndexes);
            emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
        }

        const int first = static_cast<int>(survivingRows.size());
        const int last = rowCount() - 1;
        beginRemoveRows({}, first, last);
        eraseEntryRows(first, last);
        currentKeys.erase(currentKeys.begin() + static_cast<std::ptrdiff_t>(first),
                          currentKeys.end());
        rebuildSelectionRows();
        endRemoveRows();
    }

    QSet<QString> existingKeys;
    existingKeys.reserve(static_cast<qsizetype>(currentKeys.size()));
    for (const QString& key : currentKeys) {
        existingKeys.insert(key);
    }

    std::vector<odysea::core::Entry> insertedEntries;
    std::vector<QString> insertedKeys;
    for (std::size_t offset = 0; offset < presented.size(); ++offset) {
        const QString& key = presentedKeys.at(offset);
        if (!existingKeys.contains(key)) {
            insertedEntries.push_back(presented.at(offset));
            insertedKeys.push_back(key);
            existingKeys.insert(key);
        }
    }
    if (!insertedEntries.empty()) {
        const int first = rowCount();
        const int last = first + static_cast<int>(insertedEntries.size()) - 1;
        beginInsertRows({}, first, last);
        for (const QString& key : insertedKeys) {
            currentKeys.push_back(resolveKey(key));
        }
        appendEntryRows(std::move(insertedEntries), std::move(insertedKeys));
        rebuildSelectionRows();
        endInsertRows();
    }

    QHash<QString, int> targetRows;
    targetRows.reserve(static_cast<qsizetype>(presentedKeys.size()));
    for (std::size_t row = 0; row < presentedKeys.size(); ++row) {
        targetRows.insert(presentedKeys.at(row), static_cast<int>(row));
    }

    // First occurrence wins, matching the linear search this index replaced.
    // A pending rename remap can make two rows resolve to one key, and taking
    // the later row would compare the presented entry against the wrong one.
    QHash<QString, int> currentRows;
    currentRows.reserve(static_cast<qsizetype>(currentKeys.size()));
    for (std::size_t row = 0; row < currentKeys.size(); ++row) {
        const QString& key = currentKeys.at(row);
        if (!currentRows.contains(key)) {
            currentRows.insert(key, static_cast<int>(row));
        }
    }

    QSet<int> changedRows;
    for (std::size_t row = 0; row < presented.size(); ++row) {
        const int current = currentRows.value(presentedKeys.at(row), -1);
        if (current >= 0 &&
            !entriesMatch(entries_.at(static_cast<std::size_t>(current)), presented.at(row))) {
            changedRows.insert(static_cast<int>(row));
        }
    }

    const bool orderChanged = currentKeys != presentedKeys;
    if (orderChanged) {
        const QModelIndexList oldPersistentIndexes = persistentIndexList();
        QStringList persistentKeys;
        persistentKeys.reserve(oldPersistentIndexes.size());
        for (const QModelIndex& persistent : oldPersistentIndexes) {
            persistentKeys.push_back(currentKeys.at(static_cast<std::size_t>(persistent.row())));
        }

        emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
        setEntryRows(std::move(presented), std::move(presentedKeys));
        rebuildSelectionRows();

        QModelIndexList newPersistentIndexes;
        newPersistentIndexes.reserve(oldPersistentIndexes.size());
        for (qsizetype offset = 0; offset < oldPersistentIndexes.size(); ++offset) {
            const int row = targetRows.value(persistentKeys[offset], -1);
            newPersistentIndexes.push_back(
                row >= 0 ? index(row, oldPersistentIndexes[offset].column()) : QModelIndex{});
        }
        changePersistentIndexList(oldPersistentIndexes, newPersistentIndexes);
        emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
    } else {
        setEntryRows(std::move(presented), std::move(presentedKeys));
        rebuildSelectionRows();
    }

    currentIndex_ = currentEntryKey_.isEmpty() ? -1 : targetRows.value(currentEntryKey_, -1);
    if (currentIndex_ < 0 && finalScanBatch && !entries_.empty()) {
        currentIndex_ = 0;
        currentEntryKey_ = keyForRow(0);
    }
    pendingEntryKeyRemaps_.clear();
    reconcileThumbnails();

    for (const int row : changedRows) {
        emit dataChanged(index(row), index(row),
                         {NameRole, IsDirRole, SizeRole, PathRole, RecoveryEntryRole,
                          ThumbnailSourceRole, ThumbnailLoadingRole});
    }
    if (previousSelectedRows != selectedRows_) {
        notifySelectionRoles();
    }
    if (previousCurrent != currentIndex_) {
        emit currentIndexChanged();
    }
    if (previousSelectedCount != selectedCount()) {
        emit selectedCountChanged();
    }
    if (previousSelectedFileUrls != selectedFileUrls()) {
        emit selectedFileUrlsChanged();
    }
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
    currentEntryKey_ = keyForRow(row);
    emit currentIndexChanged();
}

void DirectoryListModel::remapEntryKey(const QString& oldKey, const QString& newKey) {
    if (oldKey == newKey) {
        return;
    }
    if (selectedEntryKeys_.remove(oldKey)) {
        selectedEntryKeys_.insert(newKey);
    }
    if (currentEntryKey_ == oldKey) {
        currentEntryKey_ = newKey;
    }
    if (selectionAnchorKey_ == oldKey) {
        selectionAnchorKey_ = newKey;
    }
    if (rubberBandBaseKeys_.remove(oldKey)) {
        rubberBandBaseKeys_.insert(newKey);
    }
    removeThumbnailState(oldKey);
    pendingEntryKeyRemaps_.insert(oldKey, newKey);
}

void DirectoryListModel::replaceSelection(QSet<int> selection) {
    QSet<QString> keys;
    for (const int row : selection) {
        if (row >= 0 && row < rowCount()) {
            keys.insert(keyForRow(row));
        }
    }
    replaceSelectionKeys(std::move(keys));
}

void DirectoryListModel::replaceSelectionKeys(QSet<QString> keys) {
    QSet<int> rows;
    for (int row = 0; row < rowCount(); ++row) {
        if (keys.contains(entryKeys_.at(static_cast<std::size_t>(row)))) {
            rows.insert(row);
        }
    }
    if (selectedRows_ == rows && selectedEntryKeys_ == keys) {
        return;
    }
    selectedRows_ = std::move(rows);
    selectedEntryKeys_ = std::move(keys);
    notifySelectionRoles();
    emit selectedCountChanged();
    emit selectedFileUrlsChanged();
}

void DirectoryListModel::rebuildSelectionRows() {
    selectedRows_.clear();
    for (int row = 0; row < rowCount(); ++row) {
        if (selectedEntryKeys_.contains(entryKeys_.at(static_cast<std::size_t>(row)))) {
            selectedRows_.insert(row);
        }
    }
}

void DirectoryListModel::selectRangeTo(int row) {
    int anchorRow = rowForEntryKey(selectionAnchorKey_);
    if (anchorRow < 0) {
        anchorRow = currentIndex_ >= 0 ? currentIndex_ : row;
        selectionAnchorKey_ = keyForRow(anchorRow);
    }
    const int first = std::min(anchorRow, row);
    const int last = std::max(anchorRow, row);
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
