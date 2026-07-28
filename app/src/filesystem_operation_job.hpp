#pragma once

#include "odysea/core/file_operations.hpp"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

enum class FilesystemOperationKind { Copy, Move, Rename, Trash };

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

    [[nodiscard]] bool succeeded() const;
};

[[nodiscard]] FilesystemOperationResult
executeFilesystemOperation(const FilesystemOperationRequest& request);
