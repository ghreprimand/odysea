#include "odysea/core/file_operations.hpp"

#include <string>
#include <utility>

namespace odysea::core {
namespace fs = std::filesystem;

namespace {

std::error_code make_error(std::errc code) {
    return std::make_error_code(code);
}

OperationOutcome failure(std::errc code) {
    return OperationOutcome{.destination = {}, .error = make_error(code)};
}

OperationOutcome failure(std::error_code code) {
    return OperationOutcome{.destination = {}, .error = code};
}

OperationOutcome success(fs::path destination) {
    return OperationOutcome{.destination = std::move(destination), .error = {}};
}

/// True when the path exists, including a symlink whose target is missing.
bool path_present(const fs::path& path) {
    std::error_code ec;
    const fs::file_status status = fs::symlink_status(path, ec);
    return !ec && fs::exists(status);
}

/// Build the nth alternative for a colliding name: "report (2).txt".
fs::path numbered_variant(const fs::path& directory, const fs::path& name, unsigned attempt) {
    const std::string stem = name.stem().string();
    const std::string extension = name.extension().string();
    return directory / (stem + " (" + std::to_string(attempt) + ")" + extension);
}

/// True when `candidate` is `root` itself or lives underneath it.
bool inside_or_equal(const fs::path& candidate, const fs::path& root) {
    const fs::path normalized_candidate = fs::absolute(candidate).lexically_normal();
    const fs::path normalized_root = fs::absolute(root).lexically_normal();
    if (normalized_candidate == normalized_root) {
        return true;
    }

    auto root_part = normalized_root.begin();
    auto candidate_part = normalized_candidate.begin();
    for (; root_part != normalized_root.end(); ++root_part, ++candidate_part) {
        if (candidate_part == normalized_candidate.end() || *candidate_part != *root_part) {
            return false;
        }
    }
    return true;
}

/// Shared preflight for copy_into and move_into.
OperationOutcome prepare_transfer(const fs::path& source, const fs::path& destination_directory,
                                  const OperationOptions& options) {
    if (source.empty() || !path_present(source)) {
        return failure(std::errc::no_such_file_or_directory);
    }

    std::error_code ec;
    if (!fs::is_directory(destination_directory, ec) || ec) {
        return failure(std::errc::not_a_directory);
    }

    if (fs::is_directory(fs::symlink_status(source)) &&
        inside_or_equal(destination_directory, source)) {
        return failure(std::errc::invalid_argument);
    }

    return resolve_destination(destination_directory, source.filename().string(), options);
}

/// Clear the way for ConflictPolicy::Overwrite. Replacement, never a merge.
std::error_code clear_existing(const fs::path& destination, const OperationOptions& options) {
    if (options.conflict != ConflictPolicy::Overwrite || !path_present(destination)) {
        return {};
    }

    std::error_code ec;
    fs::remove_all(destination, ec);
    return ec;
}

} // namespace

OperationOutcome resolve_destination(const fs::path& directory, std::string_view name,
                                     const OperationOptions& options) {
    if (name.empty() || name == "." || name == ".." || name.find('/') != std::string_view::npos) {
        return failure(std::errc::invalid_argument);
    }

    const fs::path bare_name{name};
    const fs::path requested = directory / bare_name;
    if (!path_present(requested)) {
        return success(requested);
    }

    switch (options.conflict) {
    case ConflictPolicy::Fail:
        return failure(std::errc::file_exists);
    case ConflictPolicy::Overwrite:
        return success(requested);
    case ConflictPolicy::AutoRename:
        break;
    }

    // Bounded search so a pathological directory cannot spin forever.
    constexpr unsigned max_attempts = 10000;
    for (unsigned attempt = 2; attempt < max_attempts; ++attempt) {
        fs::path candidate = numbered_variant(directory, bare_name, attempt);
        if (!path_present(candidate)) {
            return success(std::move(candidate));
        }
    }
    return failure(std::errc::file_exists);
}

OperationOutcome copy_into(const fs::path& source, const fs::path& destination_directory,
                           const OperationOptions& options) {
    OperationOutcome outcome = prepare_transfer(source, destination_directory, options);
    if (!outcome.succeeded()) {
        return outcome;
    }

    if (const std::error_code ec = clear_existing(outcome.destination, options)) {
        return failure(ec);
    }

    std::error_code ec;
    fs::copy(source, outcome.destination,
             fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    if (ec) {
        return failure(ec);
    }
    return outcome;
}

OperationOutcome move_into(const fs::path& source, const fs::path& destination_directory,
                           const OperationOptions& options) {
    OperationOutcome outcome = prepare_transfer(source, destination_directory, options);
    if (!outcome.succeeded()) {
        return outcome;
    }

    if (const std::error_code ec = clear_existing(outcome.destination, options)) {
        return failure(ec);
    }

    std::error_code ec;
    fs::rename(source, outcome.destination, ec);
    if (!ec) {
        return outcome;
    }
    if (ec != std::errc::cross_device_link) {
        return failure(ec);
    }

    // Different filesystems: copy the tree across, then drop the original.
    std::error_code copy_ec;
    fs::copy(source, outcome.destination,
             fs::copy_options::recursive | fs::copy_options::copy_symlinks, copy_ec);
    if (copy_ec) {
        return failure(copy_ec);
    }

    std::error_code remove_ec;
    fs::remove_all(source, remove_ec);
    if (remove_ec) {
        return failure(remove_ec);
    }
    return outcome;
}

OperationOutcome rename_entry(const fs::path& source, std::string_view new_name,
                              const OperationOptions& options) {
    if (source.empty() || !path_present(source)) {
        return failure(std::errc::no_such_file_or_directory);
    }

    const fs::path parent = source.has_parent_path() ? source.parent_path() : fs::path(".");
    OperationOutcome outcome = resolve_destination(parent, new_name, options);
    if (!outcome.succeeded()) {
        return outcome;
    }

    if (outcome.destination == source) {
        return outcome;
    }

    if (const std::error_code ec = clear_existing(outcome.destination, options)) {
        return failure(ec);
    }

    std::error_code ec;
    fs::rename(source, outcome.destination, ec);
    if (ec) {
        return failure(ec);
    }
    return outcome;
}

} // namespace odysea::core
