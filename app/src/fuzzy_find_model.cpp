#include "fuzzy_find_model.hpp"

#include <QDir>
#include <QMetaObject>
#include <QVariant>

#include <algorithm>
#include <filesystem>
#include <utility>

namespace odysea::app {

FuzzyFindModel::FuzzyFindModel(QObject* parent) : QAbstractListModel(parent) {}

int FuzzyFindModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(matches_.size());
}

QVariant FuzzyFindModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount() || !corpus_) {
        return {};
    }
    const core::FuzzyMatch& match = matches_.at(static_cast<std::size_t>(index.row()));
    const core::FuzzyCandidate& candidate = corpus_->at(match.candidate_index);
    switch (role) {
    case NameRole:
        return QString::fromStdString(candidate.name);
    case PathRole:
        return QString::fromStdString(candidate.path.string());
    case RelativePathRole:
        return QString::fromStdString(candidate.relative_path);
    case IsDirectoryRole:
        return candidate.is_directory();
    case ScoreRole:
        return QVariant::fromValue<qulonglong>(match.score);
    case SelectedRole:
        return index.row() == currentIndex_;
    default:
        return {};
    }
}

QHash<int, QByteArray> FuzzyFindModel::roleNames() const {
    return {{NameRole, "name"},
            {PathRole, "entryPath"},
            {RelativePathRole, "relativePath"},
            {IsDirectoryRole, "isDirectory"},
            {ScoreRole, "score"},
            {SelectedRole, "selected"}};
}

QString FuzzyFindModel::rootPath() const {
    return rootPath_;
}

QString FuzzyFindModel::query() const {
    return query_;
}

void FuzzyFindModel::setQuery(const QString& query) {
    if (query_ == query) {
        return;
    }
    query_ = query;
    emit queryChanged();
    startRanking();
}

bool FuzzyFindModel::busy() const noexcept {
    return busy_;
}

bool FuzzyFindModel::ranking() const noexcept {
    return ranking_;
}

QString FuzzyFindModel::errorString() const {
    return errorString_;
}

int FuzzyFindModel::currentIndex() const noexcept {
    return currentIndex_;
}

qulonglong FuzzyFindModel::candidatesIndexed() const noexcept {
    return corpus_ ? static_cast<qulonglong>(corpus_->size()) : 0;
}

qulonglong FuzzyFindModel::entriesVisited() const noexcept {
    return static_cast<qulonglong>(entriesVisited_);
}

qulonglong FuzzyFindModel::directoriesVisited() const noexcept {
    return static_cast<qulonglong>(directoriesVisited_);
}

qulonglong FuzzyFindModel::unreadableDirectories() const noexcept {
    return static_cast<qulonglong>(unreadableDirectories_);
}

qulonglong FuzzyFindModel::filesystemWalks() const noexcept {
    return static_cast<qulonglong>(filesystemWalks_);
}

qulonglong FuzzyFindModel::rankRequests() const noexcept {
    return static_cast<qulonglong>(rankRequests_);
}

qulonglong FuzzyFindModel::candidatesExamined() const noexcept {
    return static_cast<qulonglong>(candidatesExamined_);
}

qulonglong FuzzyFindModel::characterComparisons() const noexcept {
    return static_cast<qulonglong>(characterComparisons_);
}

void FuzzyFindModel::resetRows() {
    if (matches_.empty()) {
        setCurrentIndex(-1);
        return;
    }
    beginResetModel();
    matches_.clear();
    endResetModel();
    setCurrentIndex(-1);
}

void FuzzyFindModel::resetForRoot(const QString& rootPath) {
    resetRows();
    corpus_.reset();
    rootPath_ = rootPath;
    errorString_.clear();
    entriesVisited_ = 0;
    directoriesVisited_ = 0;
    unreadableDirectories_ = 0;
    candidatesExamined_ = 0;
    characterComparisons_ = 0;
    emit rootPathChanged();
    emit progressChanged();
    emit instrumentationChanged();
    emit stateChanged();
}

void FuzzyFindModel::start(const QString& rootPath, bool showHidden) {
    scanner_.cancel();
    ranker_.cancel();
    const QString cleanPath = QDir::cleanPath(QDir(rootPath).absolutePath());
    resetForRoot(cleanPath);
    busy_ = true;
    ranking_ = false;
    ++filesystemWalks_;
    emit stateChanged();
    emit instrumentationChanged();

    core::FuzzyFindOptions options;
    options.show_hidden = showHidden;
    options.progress_interval = 512;
    activeScanToken_ = scanner_.start(
        {.root = std::filesystem::path(cleanPath.toStdString()),
         .options = options,
         .on_progress =
             [this](core::FuzzyFindProgress progress) {
                 QMetaObject::invokeMethod(
                     this, [this, progress = std::move(progress)] { applyProgress(progress); },
                     Qt::QueuedConnection);
             },
         .on_complete =
             [this](core::FuzzyFindSummary summary) {
                 QMetaObject::invokeMethod(
                     this,
                     [this, summary = std::move(summary)]() mutable {
                         applyScanCompletion(std::move(summary));
                     },
                     Qt::QueuedConnection);
             }});
}

void FuzzyFindModel::cancel() {
    scanner_.cancel();
    ranker_.cancel();
    activeScanToken_ = 0;
    activeRankToken_ = 0;
    if (busy_ || ranking_) {
        busy_ = false;
        ranking_ = false;
        emit stateChanged();
    }
}

void FuzzyFindModel::startRanking() {
    ranker_.cancel();
    const QString trimmed = query_.trimmed();
    if (!corpus_ || trimmed.isEmpty()) {
        resetRows();
        if (ranking_) {
            ranking_ = false;
            emit stateChanged();
        }
        return;
    }

    if (!ranking_) {
        ranking_ = true;
        emit stateChanged();
    }
    ++rankRequests_;
    emit instrumentationChanged();
    activeRankToken_ = ranker_.start({.corpus = corpus_,
                                      .query = trimmed.toStdString(),
                                      .result_limit = 100,
                                      .on_complete = [this](core::FuzzyRankSummary summary) {
                                          QMetaObject::invokeMethod(
                                              this,
                                              [this, summary = std::move(summary)]() mutable {
                                                  applyRankCompletion(std::move(summary));
                                              },
                                              Qt::QueuedConnection);
                                      }});
}

void FuzzyFindModel::applyProgress(const core::FuzzyFindProgress& progress) {
    if (progress.token != activeScanToken_) {
        return;
    }
    entriesVisited_ = progress.entries_visited;
    directoriesVisited_ = progress.directories_visited;
    unreadableDirectories_ = progress.unreadable_directories;
    emit progressChanged();
}

void FuzzyFindModel::applyScanCompletion(core::FuzzyFindSummary summary) {
    const QString completedPath = QString::fromStdString(summary.root.string());
    if (summary.cancelled) {
        emit scanCancelled(completedPath, static_cast<qulonglong>(summary.entries_visited));
    }
    if (summary.token != activeScanToken_) {
        return;
    }

    entriesVisited_ = summary.entries_visited;
    directoriesVisited_ = summary.directories_visited;
    unreadableDirectories_ = summary.unreadable_directories;
    errorString_ = summary.error ? QString::fromStdString(summary.error.message()) : QString();
    if (!summary.cancelled && !summary.error) {
        corpus_ = std::make_shared<const std::vector<core::FuzzyCandidate>>(
            std::move(summary.candidates));
    }
    busy_ = false;
    emit progressChanged();
    emit stateChanged();
    emit scanCompleted(completedPath, summary.cancelled);
    if (!summary.cancelled && !summary.error) {
        startRanking();
    }
}

void FuzzyFindModel::applyRankCompletion(core::FuzzyRankSummary summary) {
    if (summary.token != activeRankToken_ || summary.cancelled) {
        return;
    }
    beginResetModel();
    corpus_ = std::move(summary.corpus);
    matches_ = std::move(summary.matches);
    endResetModel();
    candidatesExamined_ = summary.work.candidates_examined;
    characterComparisons_ = summary.work.character_comparisons;
    ranking_ = false;
    currentIndex_ = matches_.empty() ? -1 : 0;
    emit currentIndexChanged();
    emit instrumentationChanged();
    emit stateChanged();
}

void FuzzyFindModel::selectRow(int row) {
    if (row >= 0 && row < rowCount()) {
        setCurrentIndex(row);
    }
}

void FuzzyFindModel::moveCursor(int delta) {
    if (matches_.empty() || delta == 0) {
        return;
    }
    int origin = currentIndex_;
    if (origin < 0) {
        origin = delta > 0 ? -1 : rowCount();
    }
    setCurrentIndex(std::clamp(origin + delta, 0, rowCount() - 1));
}

void FuzzyFindModel::activate(int row) {
    if (row < 0 || row >= rowCount() || !corpus_) {
        return;
    }
    const core::FuzzyCandidate& candidate =
        corpus_->at(matches_.at(static_cast<std::size_t>(row)).candidate_index);
    emit resultActivated(QString::fromStdString(candidate.path.string()), candidate.is_directory());
}

void FuzzyFindModel::activateCurrent() {
    activate(currentIndex_);
}

bool FuzzyFindModel::rowSelected(int row) const noexcept {
    return row >= 0 && row == currentIndex_;
}

void FuzzyFindModel::setCurrentIndex(int row) {
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
