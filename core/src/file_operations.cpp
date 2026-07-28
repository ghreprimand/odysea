#include "odysea/core/file_operations.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

/// True when the path is a directory in its own right. A symlink to a
/// directory is not one: these operations act on the link, never its target.
bool is_directory_entry(const fs::path& path) {
    std::error_code ec;
    const fs::file_status status = fs::symlink_status(path, ec);
    return !ec && fs::is_directory(status);
}

/// True when two paths name the same entry on disk, through any combination of
/// hard links, symlinks, or spelling. Never throws: a comparison that cannot be
/// made reports false, and every caller treats false as "distinct entries",
/// which is the conservative answer for the checks below.
bool same_entry(const fs::path& first, const fs::path& second) {
    if (!path_present(first) || !path_present(second)) {
        return false;
    }
    std::error_code ec;
    const bool equivalent = fs::equivalent(first, second, ec);
    return !ec && equivalent;
}

/// Build the nth alternative for a colliding name: "report (2).txt".
fs::path numbered_variant(const fs::path& directory, std::string_view name, unsigned attempt) {
    const fs::path bare_name{name};
    const std::string stem = bare_name.stem().string();
    const std::string extension = bare_name.extension().string();
    return directory / (stem + " (" + std::to_string(attempt) + ")" + extension);
}

/// True when `candidate` is `root` itself or lives underneath it.
///
/// Used to refuse self-destructive transfers, so an inconclusive answer is
/// reported as "inside": refusing a legitimate operation is recoverable,
/// copying a directory into itself is not.
bool inside_or_equal(const fs::path& candidate, const fs::path& root) {
    std::error_code candidate_ec;
    std::error_code root_ec;
    const fs::path normalized_candidate = fs::absolute(candidate, candidate_ec).lexically_normal();
    const fs::path normalized_root = fs::absolute(root, root_ec).lexically_normal();
    if (candidate_ec || root_ec) {
        return true;
    }
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

    if (is_directory_entry(source) && inside_or_equal(destination_directory, source)) {
        return failure(std::errc::invalid_argument);
    }

    return resolve_destination(destination_directory, source.filename().string(), options);
}

/// Whether replacing `destination` with `source` needs the destination removed
/// first.
///
/// A rename replaces one non-directory with another atomically, so removing
/// beforehand would only open a window where neither copy exists. Every other
/// combination is a rename the kernel refuses, so the destination has to go
/// first.
bool replacement_needs_removal(const fs::path& source, const fs::path& destination) {
    return is_directory_entry(source) || is_directory_entry(destination);
}

/// Reserve an unused staging name beside `destination`.
///
/// Copies are assembled under this name and moved into place once they are
/// complete, so a failure part-way through leaves the existing destination
/// intact.
fs::path reserve_staging_path(const fs::path& destination, std::error_code& error) {
    const fs::path directory = destination.parent_path();
    const std::string base = ".odysea-staging-" + destination.filename().string();

    constexpr unsigned max_attempts = 10000;
    for (unsigned attempt = 0; attempt < max_attempts; ++attempt) {
        fs::path candidate = directory / (base + "." + std::to_string(attempt));
        if (!path_present(candidate)) {
            error.clear();
            return candidate;
        }
    }
    error = make_error(std::errc::file_exists);
    return {};
}

/// Grant the owner traversal rights throughout a tree.
///
/// A copy that failed part-way can leave behind directories reproduced with
/// permissions that forbid entering them, which would strand the staging tree.
/// Only ever applied to a staging path this code created.
void grant_traversal(const fs::path& root) {
    std::error_code ec;
    fs::permissions(root, fs::perms::owner_all, fs::perm_options::replace, ec);

    std::vector<fs::path> pending{root};
    while (!pending.empty()) {
        const fs::path current = std::move(pending.back());
        pending.pop_back();

        std::error_code iterate_ec;
        fs::directory_iterator element(current, fs::directory_options::skip_permission_denied,
                                       iterate_ec);
        if (iterate_ec) {
            continue;
        }

        const fs::directory_iterator end;
        while (element != end) {
            const fs::path child = element->path();
            std::error_code status_ec;
            if (fs::is_directory(fs::symlink_status(child, status_ec)) && !status_ec) {
                std::error_code permission_ec;
                fs::permissions(child, fs::perms::owner_all, fs::perm_options::replace,
                                permission_ec);
                pending.push_back(child);
            }

            std::error_code step_ec;
            element.increment(step_ec);
            if (step_ec) {
                break;
            }
        }
    }
}

void discard_staging(const fs::path& staging) {
    std::error_code ec;
    fs::remove_all(staging, ec);
    if (!ec) {
        return;
    }

    // The failed copy may have reproduced a directory that cannot be entered.
    grant_traversal(staging);
    std::error_code retry_ec;
    fs::remove_all(staging, retry_ec);
}

constexpr fs::copy_options recursive_copy =
    fs::copy_options::recursive | fs::copy_options::copy_symlinks;

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
        fs::path candidate = numbered_variant(directory, name, attempt);
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

    // The entry already occupies the requested destination. Copying it over
    // itself is a no-op, and any attempt to clear the way first would destroy
    // the only copy.
    if (same_entry(source, outcome.destination)) {
        return outcome;
    }

    if (!path_present(outcome.destination)) {
        std::error_code ec;
        fs::copy(source, outcome.destination, recursive_copy, ec);
        if (ec) {
            return failure(ec);
        }
        return outcome;
    }

    // Replacing something that exists: assemble the copy under a staging name
    // first, so a failure part-way through leaves the destination intact.
    std::error_code staging_ec;
    const fs::path staging = reserve_staging_path(outcome.destination, staging_ec);
    if (staging_ec) {
        return failure(staging_ec);
    }

    std::error_code copy_ec;
    fs::copy(source, staging, recursive_copy, copy_ec);
    if (copy_ec) {
        discard_staging(staging);
        return failure(copy_ec);
    }

    if (replacement_needs_removal(staging, outcome.destination)) {
        std::error_code remove_ec;
        fs::remove_all(outcome.destination, remove_ec);
        if (remove_ec) {
            discard_staging(staging);
            return failure(remove_ec);
        }
    }

    std::error_code install_ec;
    fs::rename(staging, outcome.destination, install_ec);
    if (install_ec) {
        discard_staging(staging);
        return failure(install_ec);
    }
    return outcome;
}

OperationOutcome move_into(const fs::path& source, const fs::path& destination_directory,
                           const OperationOptions& options) {
    OperationOutcome outcome = prepare_transfer(source, destination_directory, options);
    if (!outcome.succeeded()) {
        return outcome;
    }

    // The entry is already where it was asked to go.
    if (same_entry(source, outcome.destination)) {
        return outcome;
    }

    if (path_present(outcome.destination) &&
        replacement_needs_removal(source, outcome.destination)) {
        std::error_code remove_ec;
        fs::remove_all(outcome.destination, remove_ec);
        if (remove_ec) {
            return failure(remove_ec);
        }
    }

    std::error_code ec;
    fs::rename(source, outcome.destination, ec);
    if (!ec) {
        return outcome;
    }
    if (ec != std::errc::cross_device_link) {
        return failure(ec);
    }

    // Different filesystems: copy the tree across, then drop the original. The
    // source is only removed once the copy has fully succeeded.
    std::error_code copy_ec;
    fs::copy(source, outcome.destination, recursive_copy | fs::copy_options::overwrite_existing,
             copy_ec);
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

    // Renaming an entry to the name it already has changes nothing. Checked by
    // identity as well as spelling, so no path to the entry can be clobbered.
    if (outcome.destination == source || same_entry(source, outcome.destination)) {
        return outcome;
    }

    if (path_present(outcome.destination) &&
        replacement_needs_removal(source, outcome.destination)) {
        std::error_code remove_ec;
        fs::remove_all(outcome.destination, remove_ec);
        if (remove_ec) {
            return failure(remove_ec);
        }
    }

    std::error_code ec;
    fs::rename(source, outcome.destination, ec);
    if (ec) {
        return failure(ec);
    }
    return outcome;
}

} // namespace odysea::core
