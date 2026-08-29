#pragma once

#include "odysea/core/file_operations.hpp"
#include "odysea/core/operation_journal.hpp"

#include <filesystem>
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
