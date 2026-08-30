#pragma once

#include "odysea/core/file_operations.hpp"
#include "odysea/core/operation_journal.hpp"
#include "odysea/core/transfer.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

enum class FilesystemOperationKind { Copy, Move, Rename, Trash, Undo };

struct FilesystemOperationRequest {
    FilesystemOperationKind kind = FilesystemOperationKind::Copy;
    std::vector<std::filesystem::path> sources;
    std::filesystem::path destinationDirectory;
    std::string newName;
    odysea::core::OperationOptions options;

    /// Shared with the surface holding the pause and cancel controls. Absent
    /// for the operations that move no data, which have nothing to interrupt.
    std::shared_ptr<odysea::core::TransferControl> control;

    /// Where reports go. Invoked on the worker thread, so a handler that
    /// touches a model marshals onto its own thread first.
    odysea::core::TransferObserver onProgress;
};

struct FilesystemOperationItem {
    std::filesystem::path source;
    std::filesystem::path destination;
    std::error_code error;
};

struct FilesystemOperationResult {
    FilesystemOperationKind kind = FilesystemOperationKind::Copy;
    std::vector<FilesystemOperationItem> items;
    std::optional<odysea::core::UndoOutcome> undoOutcome;

    [[nodiscard]] bool succeeded() const;
};

[[nodiscard]] FilesystemOperationResult
executeFilesystemOperation(const FilesystemOperationRequest& request,
                           odysea::core::OperationJournal& journal);
[[nodiscard]] FilesystemOperationResult
executeFilesystemUndo(odysea::core::OperationJournal& journal);
