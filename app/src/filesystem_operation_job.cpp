#include "filesystem_operation_job.hpp"

#include "odysea/core/trash.hpp"

namespace {

FilesystemOperationItem invalidItem(const std::filesystem::path& source) {
    return FilesystemOperationItem{.source = source,
                                   .destination = {},
                                   .error = std::make_error_code(std::errc::invalid_argument)};
}

} // namespace

bool FilesystemOperationResult::succeeded() const {
    if (items.empty()) {
        return false;
    }
    for (const FilesystemOperationItem& item : items) {
        if (item.error) {
            return false;
        }
    }
    return true;
}

FilesystemOperationResult executeFilesystemOperation(const FilesystemOperationRequest& request) {
    FilesystemOperationResult result{.kind = request.kind, .items = {}};
    if (request.sources.empty()) {
        result.items.push_back(invalidItem({}));
        return result;
    }

    if ((request.kind == FilesystemOperationKind::Copy ||
         request.kind == FilesystemOperationKind::Move) &&
        request.destinationDirectory.empty()) {
        result.items.push_back(invalidItem(request.sources.front()));
        return result;
    }

    for (const std::filesystem::path& source : request.sources) {
        FilesystemOperationItem item{.source = source, .destination = {}, .error = {}};
        if (request.kind == FilesystemOperationKind::Copy) {
            const odysea::core::OperationOutcome outcome =
                odysea::core::copy_into(source, request.destinationDirectory, request.options);
            item.destination = outcome.destination;
            item.error = outcome.error;
        } else if (request.kind == FilesystemOperationKind::Move) {
            const odysea::core::OperationOutcome outcome =
                odysea::core::move_into(source, request.destinationDirectory, request.options);
            item.destination = outcome.destination;
            item.error = outcome.error;
        } else if (request.kind == FilesystemOperationKind::Rename) {
            const odysea::core::OperationOutcome outcome =
                odysea::core::rename_entry(source, request.newName, request.options);
            item.destination = outcome.destination;
            item.error = outcome.error;
        } else {
            const odysea::core::TrashOutcome outcome = odysea::core::move_to_trash(source);
            item.destination = outcome.trashed_path;
            item.error = outcome.error;
        }
        result.items.push_back(std::move(item));

        if (request.kind == FilesystemOperationKind::Rename) {
            break;
        }
    }

    return result;
}
