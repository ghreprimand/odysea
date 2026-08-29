#include "directory_list_model.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QtConcurrentRun>

#include <utility>

namespace fs = std::filesystem;

namespace {

std::vector<std::filesystem::path> toPaths(const QStringList& paths) {
    std::vector<std::filesystem::path> converted;
    converted.reserve(static_cast<std::size_t>(paths.size()));
    for (const QString& path : paths) {
        converted.emplace_back(path.toStdString());
    }
    return converted;
}

QString operationName(FilesystemOperationKind kind) {
    switch (kind) {
    case FilesystemOperationKind::Copy:
        return QStringLiteral("Copy");
    case FilesystemOperationKind::Move:
        return QStringLiteral("Move");
    case FilesystemOperationKind::Rename:
        return QStringLiteral("Rename");
    case FilesystemOperationKind::Trash:
        return QStringLiteral("Move to Trash");
    case FilesystemOperationKind::Undo:
        return QStringLiteral("Undo");
    }
    return QStringLiteral("Filesystem operation");
}

QString barrierReason(odysea::core::ReversalBarrier barrier) {
    using Barrier = odysea::core::ReversalBarrier;
    switch (barrier) {
    case Barrier::None:
        return {};
    case Barrier::NothingChanged:
        return QCoreApplication::translate("DirectoryListModel",
                                           "The last operation changed nothing.");
    case Barrier::ReplacedEntryDiscarded:
        return QCoreApplication::translate(
            "DirectoryListModel",
            "The last operation replaced an entry that is no longer present.");
    case Barrier::ResultNotIdentified:
        return QCoreApplication::translate(
            "DirectoryListModel",
            "The result of the last operation could not be identified safely.");
    case Barrier::CreatedTreeTooLarge:
        return QCoreApplication::translate("DirectoryListModel",
                                           "The copied tree exceeds the reversible-entry limit.");
    case Barrier::HardLinksNotRestorable:
        return QCoreApplication::translate("DirectoryListModel",
                                           "The last move cannot restore its linked entries.");
    }
    return QCoreApplication::translate("DirectoryListModel",
                                       "The last operation cannot be undone safely.");
}

QString undoFailureReason(const odysea::core::UndoOutcome& outcome) {
    using Status = odysea::core::UndoStatus;
    switch (outcome.status) {
    case Status::Reversed:
        return {};
    case Status::HistoryEmpty:
        return QCoreApplication::translate("DirectoryListModel",
                                           "No filesystem operation is available to undo.");
    case Status::Barred:
        return barrierReason(outcome.barrier);
    case Status::ResultChanged:
        return QCoreApplication::translate("DirectoryListModel",
                                           "The result changed after the operation completed.");
    case Status::OriginOccupied:
        return QCoreApplication::translate("DirectoryListModel",
                                           "The original location is now occupied.");
    case Status::Failed:
        return outcome.error
                   ? QString::fromStdString(outcome.error.message())
                   : QCoreApplication::translate("DirectoryListModel",
                                                 "The filesystem could not complete the undo.");
    }
    return QCoreApplication::translate("DirectoryListModel",
                                       "The filesystem could not complete the undo.");
}

bool isSameOrDescendant(const fs::path& candidate, const fs::path& ancestor) {
    const fs::path relative = candidate.lexically_relative(ancestor);
    if (relative.empty()) {
        return false;
    }
    const auto first = relative.begin();
    return relative == "." || (first != relative.end() && *first != "..");
}

bool hasDestinationParent(const fs::path& sourceIdentity, const fs::path& destinationIdentity,
                          const fs::path& destination) {
    return sourceIdentity.parent_path() == destinationIdentity ||
           sourceIdentity.parent_path() == destination;
}

} // namespace

bool DirectoryListModel::operationBusy() const noexcept {
    return operationBusy_;
}

QString DirectoryListModel::operationErrorString() const {
    return operationErrorString_;
}

bool DirectoryListModel::canUndo() const noexcept {
    return canUndo_;
}

QString DirectoryListModel::undoDisabledReason() const {
    return undoDisabledReason_;
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

void DirectoryListModel::requestOperation(const QString& operation) {
    if (operationBusy_) {
        setStatusMessage(tr("Wait for the current filesystem operation to finish."));
        return;
    }
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
}

odysea::core::OperationOptions DirectoryListModel::operationOptions(int conflictMode) const {
    odysea::core::ConflictPolicy policy = odysea::core::ConflictPolicy::Fail;
    if (conflictMode == ConflictOverwrite) {
        policy = odysea::core::ConflictPolicy::Overwrite;
    } else if (conflictMode == ConflictAutoRename) {
        policy = odysea::core::ConflictPolicy::AutoRename;
    }
    return odysea::core::OperationOptions{.conflict = policy};
}

void DirectoryListModel::performCopy(const QString& destinationDirectory, int conflictMode) {
    FilesystemOperationRequest request{
        .kind = FilesystemOperationKind::Copy,
        .sources = toPaths(selectedPaths()),
        .destinationDirectory = normalizedPath(destinationDirectory).toStdString(),
        .newName = {},
        .options = operationOptions(conflictMode),
    };
    startOperation(std::move(request));
}

void DirectoryListModel::performMove(const QString& destinationDirectory, int conflictMode) {
    FilesystemOperationRequest request{
        .kind = FilesystemOperationKind::Move,
        .sources = toPaths(selectedPaths()),
        .destinationDirectory = normalizedPath(destinationDirectory).toStdString(),
        .newName = {},
        .options = operationOptions(conflictMode),
    };
    startOperation(std::move(request));
}

bool DirectoryListModel::canDropSelection(const QString& destinationDirectory, bool move) const {
    if (operationBusy_) {
        return false;
    }
    const QStringList sources = selectedPaths();
    if (sources.isEmpty()) {
        return false;
    }

    std::error_code error;
    const fs::path destinationIdentity = normalizedFilesystemPath(destinationDirectory);
    const fs::path destination = fs::weakly_canonical(destinationIdentity, error);
    if (error || !fs::is_directory(destination, error) || error) {
        return false;
    }

    for (const QString& sourceText : sources) {
        const fs::path sourceIdentity = normalizedFilesystemPath(sourceText);
        if (sourceIdentity == destinationIdentity || sourceIdentity == destination ||
            (move && hasDestinationParent(sourceIdentity, destinationIdentity, destination))) {
            return false;
        }
        error.clear();
        const fs::path source = fs::weakly_canonical(sourceIdentity, error);
        if (error) {
            return false;
        }
        error.clear();
        const fs::file_status status = fs::status(source, error);
        if (error) {
            return false;
        }
        if (fs::is_directory(status) && isSameOrDescendant(destination, source)) {
            return false;
        }
    }
    return true;
}

bool DirectoryListModel::dropSelection(const QString& destinationDirectory, bool move,
                                       int conflictMode) {
    if (!canDropSelection(destinationDirectory, move)) {
        setStatusMessage(tr("The selected entries cannot be transferred to that location."));
        return false;
    }
    if (move) {
        performMove(destinationDirectory, conflictMode);
    } else {
        const fs::path destinationIdentity = normalizedFilesystemPath(destinationDirectory);
        std::error_code error;
        const fs::path destination = fs::weakly_canonical(destinationIdentity, error);
        bool copiesIntoSourceParent = false;
        if (!error) {
            for (const QString& sourceText : selectedPaths()) {
                const fs::path sourceIdentity = normalizedFilesystemPath(sourceText);
                if (hasDestinationParent(sourceIdentity, destinationIdentity, destination)) {
                    copiesIntoSourceParent = true;
                    break;
                }
            }
        }
        const int effectiveConflictMode = copiesIntoSourceParent && conflictMode == ConflictFail
                                              ? ConflictAutoRename
                                              : conflictMode;
        performCopy(destinationDirectory, effectiveConflictMode);
    }
    return true;
}

void DirectoryListModel::performRename(const QString& newName, int conflictMode) {
    FilesystemOperationRequest request{
        .kind = FilesystemOperationKind::Rename,
        .sources = toPaths(selectedPaths()),
        .destinationDirectory = {},
        .newName = newName.toStdString(),
        .options = operationOptions(conflictMode),
    };
    startOperation(std::move(request));
}

void DirectoryListModel::performTrash() {
    FilesystemOperationRequest request{
        .kind = FilesystemOperationKind::Trash,
        .sources = toPaths(selectedPaths()),
        .destinationDirectory = {},
        .newName = {},
        .options = operationOptions(ConflictFail),
    };
    startOperation(std::move(request));
}

void DirectoryListModel::performUndo() {
    if (operationBusy_) {
        setStatusMessage(tr("Wait for the current filesystem operation to finish."));
        return;
    }
    if (!canUndo_) {
        setStatusMessage(undoDisabledReason_);
        return;
    }
    startUndo();
}

void DirectoryListModel::startOperation(FilesystemOperationRequest request) {
    if (operationBusy_) {
        setStatusMessage(tr("Wait for the current filesystem operation to finish."));
        return;
    }
    if (request.sources.empty()) {
        setStatusMessage(tr("Select at least one item first."));
        return;
    }
    if (request.kind == FilesystemOperationKind::Rename && request.sources.size() != 1) {
        setStatusMessage(tr("Rename requires exactly one selected item."));
        return;
    }

    setOperationErrorString({});
    setOperationBusy(true);
    watchRefreshPending_ = false;
    const QString name = operationName(request.kind);
    setStatusMessage(tr("%1 in progress…").arg(name));
    const std::shared_ptr<odysea::core::OperationJournal> journal = operationJournal_;
    operationWatcher_.setFuture(QtConcurrent::run([request = std::move(request), journal] {
        return executeFilesystemOperation(request, *journal);
    }));
}

void DirectoryListModel::startUndo() {
    setOperationErrorString({});
    setOperationBusy(true);
    watchRefreshPending_ = false;
    setStatusMessage(tr("Undo in progress…"));
    const std::shared_ptr<odysea::core::OperationJournal> journal = operationJournal_;
    operationWatcher_.setFuture(
        QtConcurrent::run([journal] { return executeFilesystemUndo(*journal); }));
}

void DirectoryListModel::finishOperation(FilesystemOperationResult result) {
    setOperationBusy(false);

    if (result.kind == FilesystemOperationKind::Undo) {
        const odysea::core::UndoOutcome outcome = result.undoOutcome.value_or(
            odysea::core::UndoOutcome{.status = odysea::core::UndoStatus::HistoryEmpty,
                                      .barrier = odysea::core::ReversalBarrier::None,
                                      .error = {},
                                      .restored_path = {}});
        refreshUndoState();
        if (outcome.succeeded()) {
            setOperationErrorString({});
            setStatusMessage(tr("Undo completed."));
            watchRefreshPending_ = false;
            startScan();
            return;
        }
        const QString reason = undoFailureReason(outcome);
        setOperationErrorString(reason);
        setStatusMessage(tr("Undo could not be completed."));
        watchRefreshPending_ = false;
        return;
    }

    int failureCount = 0;
    QString firstFailure;
    for (const FilesystemOperationItem& item : result.items) {
        if (!item.error) {
            continue;
        }
        ++failureCount;
        if (firstFailure.isEmpty()) {
            const QString sourceName = QString::fromStdString(item.source.filename().string());
            firstFailure =
                tr("%1: %2").arg(sourceName, QString::fromStdString(item.error.message()));
        }
    }

    const QString name = operationName(result.kind);
    if (failureCount > 0) {
        setOperationErrorString(
            tr("%1 failed for %2 item(s). %3").arg(name).arg(failureCount).arg(firstFailure));
        setStatusMessage(tr("%1 completed with errors.").arg(name));
    } else {
        setOperationErrorString({});
        setStatusMessage(tr("%1 completed for %2 item(s).")
                             .arg(name)
                             .arg(static_cast<qulonglong>(result.items.size())));
    }

    refreshUndoState();
    watchRefreshPending_ = false;
    startScan();
}

void DirectoryListModel::refreshUndoState() {
    const bool nextCanUndo = operationJournal_->can_undo();
    QString nextDisabledReason;
    if (!nextCanUndo) {
        const odysea::core::OperationRecord* newest = operationJournal_->newest();
        nextDisabledReason = newest == nullptr ? tr("No filesystem operation is available to undo.")
                                               : barrierReason(newest->barrier);
    }
    if (canUndo_ == nextCanUndo && undoDisabledReason_ == nextDisabledReason) {
        return;
    }
    canUndo_ = nextCanUndo;
    undoDisabledReason_ = std::move(nextDisabledReason);
    emit undoStateChanged();
}

void DirectoryListModel::setOperationBusy(bool busy) {
    if (operationBusy_ == busy) {
        return;
    }
    operationBusy_ = busy;
    emit operationBusyChanged();
}

void DirectoryListModel::setOperationErrorString(const QString& errorString) {
    if (operationErrorString_ == errorString) {
        return;
    }
    operationErrorString_ = errorString;
    emit operationErrorStringChanged();
}
