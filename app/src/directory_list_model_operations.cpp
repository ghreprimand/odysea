#include "directory_list_model.hpp"

#include <QDir>
#include <QtConcurrentRun>

#include <utility>

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
    }
    return QStringLiteral("Filesystem operation");
}

} // namespace

bool DirectoryListModel::operationBusy() const noexcept {
    return operationBusy_;
}

QString DirectoryListModel::operationErrorString() const {
    return operationErrorString_;
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
    operationWatcher_.setFuture(QtConcurrent::run(
        [request = std::move(request)] { return executeFilesystemOperation(request); }));
}

void DirectoryListModel::finishOperation(FilesystemOperationResult result) {
    setOperationBusy(false);

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

    watchRefreshPending_ = false;
    startScan();
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
