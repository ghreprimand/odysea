#include "odysea/core/file_operations.hpp"

#include "file_operations_internal.hpp"

#include <atomic>
#include <cstddef>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace odysea::core {
namespace fs = std::filesystem;

namespace {

/// Leading marker shared by every entry these operations create for their own
/// use. Begins with a dot so the entries stay out of a default listing.
constexpr std::string_view working_marker = ".odysea-";

/// Fixed-width role markers. Same width for both so the shape of a working
/// name does not depend on its role.
constexpr std::string_view prepared_marker = "new";
constexpr std::string_view replaced_marker = "old";

/// Length of the per-process part of a working name.
constexpr std::size_t working_tag_digits = 12;

/// Upper bound on the length of a working name: marker, role, two separators,
/// the per-process tag, and a serial number that cannot exceed the twenty
/// digits of a 64-bit value.
constexpr std::size_t working_name_bound =
    working_marker.size() + prepared_marker.size() + 2 + working_tag_digits + 20;

static_assert(working_name_bound < 255, "a working name must fit in a single filesystem component");

std::string_view role_marker(WorkingEntryRole role) {
    return role == WorkingEntryRole::Replaced ? replaced_marker : prepared_marker;
}

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

/// Whether replacing `destination` with `source` has to go through the staged
/// route rather than a single rename.
///
/// A rename replaces one non-directory with another atomically, which is the
/// strongest guarantee available: at no point does neither copy exist. The
/// kernel refuses every other combination, so a directory on either side has to
/// be replaced in steps.
bool replacement_needs_staging(const fs::path& source, const fs::path& destination) {
    return is_directory_entry(source) || is_directory_entry(destination);
}

/// A value that differs between processes working in the same directory.
///
/// Drawn once. Two processes that both reserve a working name in one directory
/// pick from disjoint name spaces, so neither has to retry because of the
/// other.
std::string process_tag() {
    static const std::string tag = [] {
        std::random_device source;
        std::uniform_int_distribution<unsigned> digits(0, 15);
        std::string value;
        value.reserve(working_tag_digits);
        for (unsigned index = 0; index < working_tag_digits; ++index) {
            value.push_back("0123456789abcdef"[digits(source)]);
        }
        return value;
    }();
    return tag;
}

/// Build the working name for one reservation attempt.
///
/// Deliberately independent of the name being operated on. A file name may be
/// as long as the filesystem allows for a single component — 255 bytes on the
/// common Linux filesystems — so any name derived from it by adding a prefix
/// would be too long to create, and the operation would fail on exactly the
/// entries that are hardest to recover by hand. This name has a fixed shape and
/// a bounded length instead: a marker identifying the operation that owns it, a
/// role, a per-process tag, and a serial number.
std::string working_name(WorkingEntryRole role, unsigned long long serial) {
    std::string name;
    name.reserve(working_name_bound);
    name += working_marker;
    name += role_marker(role);
    name += '-';
    name += process_tag();
    name += '-';
    name += std::to_string(serial);
    return name;
}

/// Reserve an unused working name in `directory`.
///
/// Working entries always live in the directory holding the destination, so
/// installing one is a rename within a single directory and can never cross a
/// filesystem boundary.
///
/// The serial advances globally rather than per call, so a candidate that is
/// already taken — by another thread, another process, or an entry left behind
/// by an earlier interrupted run — is never retried by a caller that would pick
/// the same name again.
fs::path reserve_working_path(const fs::path& directory, WorkingEntryRole role,
                              std::error_code& error) {
    static std::atomic<unsigned long long> serial{0};

    constexpr unsigned max_attempts = 10000;
    for (unsigned attempt = 0; attempt < max_attempts; ++attempt) {
        fs::path candidate =
            directory / working_name(role, serial.fetch_add(1, std::memory_order_relaxed));
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

/// Remove an entry this code created and no longer needs.
///
/// Only ever called for a staging or backup path, never for anything the caller
/// named. Failure is not reported: the operation it belongs to has already
/// decided its outcome, and leaving an entry behind costs a stray name rather
/// than data.
void discard_working_entry(const fs::path& working) {
    std::error_code ec;
    fs::remove_all(working, ec);
    if (!ec) {
        return;
    }

    // A failed copy may have reproduced a directory that cannot be entered.
    grant_traversal(working);
    std::error_code retry_ec;
    fs::remove_all(working, retry_ec);
}

/// Abandon a staged operation without losing anything.
///
/// When the source was relocated into the staging path it is the only copy, so
/// it is put back under its original name. When the staging path holds a copy
/// the source still exists, so the copy is discarded. If the source cannot be
/// put back it is left under the staging name rather than deleted: a misplaced
/// entry is recoverable, a deleted one is not.
void unwind_staging(const fs::path& staging, const fs::path& source, bool source_relocated,
                    const detail::RenameStep& rename_step) {
    if (!source_relocated) {
        discard_working_entry(staging);
        return;
    }
    std::error_code ec;
    rename_step(detail::RenameKind::Unwind, staging, source, ec);
}

/// Put `prepared` at `destination`, keeping whatever is already there until the
/// install has succeeded.
///
/// The occupant is moved aside to a sibling backup rather than removed, so
/// there is no point at which the destination has been given up before its
/// replacement exists. A failed install puts the occupant straight back. The
/// backup is removed only once the destination holds the replacement.
///
/// `prepared` is left alone in every failure path: the caller owns it and knows
/// whether it is a copy to discard or the only remaining copy of the source.
///
/// When the install fails and the occupant cannot be put back, the occupant
/// stays under the backup name. That is a deliberate choice of debris over
/// deletion: an entry under an unexpected name can be recovered, and there is
/// nothing to gain from removing the only copy of data the caller never asked
/// to lose.
std::error_code install_over(const fs::path& prepared, const fs::path& destination,
                             const detail::RenameStep& rename_step) {
    if (!path_present(destination)) {
        std::error_code install_ec;
        rename_step(detail::RenameKind::Install, prepared, destination, install_ec);
        return install_ec;
    }

    std::error_code reserve_ec;
    const fs::path backup =
        reserve_working_path(destination.parent_path(), WorkingEntryRole::Replaced, reserve_ec);
    if (reserve_ec) {
        return reserve_ec;
    }

    // Nothing has been disturbed yet, so a failure here costs nothing.
    std::error_code backup_ec;
    rename_step(detail::RenameKind::Backup, destination, backup, backup_ec);
    if (backup_ec) {
        return backup_ec;
    }

    std::error_code install_ec;
    rename_step(detail::RenameKind::Install, prepared, destination, install_ec);
    if (install_ec) {
        std::error_code restore_ec;
        rename_step(detail::RenameKind::Restore, backup, destination, restore_ec);
        return install_ec;
    }

    discard_working_entry(backup);
    return {};
}

constexpr fs::copy_options recursive_copy =
    fs::copy_options::recursive | fs::copy_options::copy_symlinks;

} // namespace

WorkingEntryRole classify_working_entry(std::string_view name) noexcept {
    if (!name.starts_with(working_marker)) {
        return WorkingEntryRole::None;
    }
    std::string_view rest = name.substr(working_marker.size());

    WorkingEntryRole role = WorkingEntryRole::None;
    if (rest.starts_with(prepared_marker)) {
        role = WorkingEntryRole::Prepared;
        rest = rest.substr(prepared_marker.size());
    } else if (rest.starts_with(replaced_marker)) {
        role = WorkingEntryRole::Replaced;
        rest = rest.substr(replaced_marker.size());
    } else {
        return WorkingEntryRole::None;
    }

    // "-<tag>-<serial>", with a fixed-width tag and at least one serial digit.
    if (rest.size() < working_tag_digits + 3 || rest.front() != '-') {
        return WorkingEntryRole::None;
    }
    rest = rest.substr(1);

    const std::string_view tag = rest.substr(0, working_tag_digits);
    for (const char digit : tag) {
        const bool hexadecimal = (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
        if (!hexadecimal) {
            return WorkingEntryRole::None;
        }
    }

    rest = rest.substr(working_tag_digits);
    if (rest.size() < 2 || rest.front() != '-') {
        return WorkingEntryRole::None;
    }
    rest = rest.substr(1);
    for (const char digit : rest) {
        if (digit < '0' || digit > '9') {
            return WorkingEntryRole::None;
        }
    }
    return role;
}

bool is_working_entry(std::string_view name) noexcept {
    return classify_working_entry(name) != WorkingEntryRole::None;
}

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

namespace detail {

void rename_with_filesystem(RenameKind /*kind*/, const fs::path& from, const fs::path& to,
                            std::error_code& error) {
    fs::rename(from, to, error);
}

OperationOutcome copy_into_using(const fs::path& source, const fs::path& destination_directory,
                                 const OperationOptions& options, const RenameStep& rename_step) {
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

    // The copy is always assembled under a staging name and installed once it
    // is complete, whether or not the destination is occupied. A copy that
    // fails part-way therefore leaves nothing behind: neither a partial entry
    // at a free destination nor a damaged one at an occupied destination.
    std::error_code staging_ec;
    const fs::path staging = reserve_working_path(outcome.destination.parent_path(),
                                                  WorkingEntryRole::Prepared, staging_ec);
    if (staging_ec) {
        return failure(staging_ec);
    }

    std::error_code copy_ec;
    fs::copy(source, staging, recursive_copy, copy_ec);
    if (copy_ec) {
        discard_working_entry(staging);
        return failure(copy_ec);
    }

    const std::error_code install_ec = install_over(staging, outcome.destination, rename_step);
    if (install_ec) {
        discard_working_entry(staging);
        return failure(install_ec);
    }
    return outcome;
}

OperationOutcome move_into_using(const fs::path& source, const fs::path& destination_directory,
                                 const OperationOptions& options, const RenameStep& rename_step) {
    OperationOutcome outcome = prepare_transfer(source, destination_directory, options);
    if (!outcome.succeeded()) {
        return outcome;
    }

    // The entry is already where it was asked to go.
    if (same_entry(source, outcome.destination)) {
        return outcome;
    }

    // A rename moves the entry in one step when nothing is in the way, and
    // replaces one non-directory with another atomically when something is. Try
    // it before anything is disturbed: nothing has been removed if it fails.
    if (!path_present(outcome.destination) ||
        !replacement_needs_staging(source, outcome.destination)) {
        std::error_code ec;
        rename_step(RenameKind::Relocate, source, outcome.destination, ec);
        if (!ec) {
            return outcome;
        }
        if (ec != std::errc::cross_device_link) {
            return failure(ec);
        }
    }

    // Either the rename cannot replace what is in the way, or the two paths are
    // on different filesystems. Both cases assemble the moved entry beside the
    // destination first: nothing existing is removed until the replacement is
    // complete, so a failure along the way costs neither the source nor the
    // destination.
    std::error_code staging_ec;
    const fs::path staging = reserve_working_path(outcome.destination.parent_path(),
                                                  WorkingEntryRole::Prepared, staging_ec);
    if (staging_ec) {
        return failure(staging_ec);
    }

    bool source_relocated = false;
    std::error_code relocate_ec;
    rename_step(RenameKind::Relocate, source, staging, relocate_ec);
    if (!relocate_ec) {
        // Same filesystem: the source now lives under the staging name, and can
        // be put back untouched if a later step fails.
        source_relocated = true;
    } else if (relocate_ec == std::errc::cross_device_link) {
        std::error_code copy_ec;
        fs::copy(source, staging, recursive_copy, copy_ec);
        if (copy_ec) {
            discard_working_entry(staging);
            return failure(copy_ec);
        }
    } else {
        // The staging name was reserved, never created.
        return failure(relocate_ec);
    }

    const std::error_code install_ec = install_over(staging, outcome.destination, rename_step);
    if (install_ec) {
        unwind_staging(staging, source, source_relocated, rename_step);
        return failure(install_ec);
    }

    if (!source_relocated) {
        // The move was completed by copying, so the original is still in place.
        std::error_code remove_ec;
        fs::remove_all(source, remove_ec);
        if (remove_ec) {
            return failure(remove_ec);
        }
    }
    return outcome;
}

OperationOutcome rename_entry_using(const fs::path& source, std::string_view new_name,
                                    const OperationOptions& options,
                                    const RenameStep& rename_step) {
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

    // A free name, or one non-directory replacing another, is a single atomic
    // rename. Both paths share a parent directory, so it cannot cross a
    // filesystem boundary and has no fallback to fall back to.
    if (!path_present(outcome.destination) ||
        !replacement_needs_staging(source, outcome.destination)) {
        std::error_code ec;
        rename_step(RenameKind::Relocate, source, outcome.destination, ec);
        if (ec) {
            return failure(ec);
        }
        return outcome;
    }

    // A directory is involved, so the destination has to move aside first. The
    // source is relocated to a staging name before that happens, which keeps
    // the recovery uniform: whatever fails, the source goes back under its
    // original name and the destination goes back under its own.
    std::error_code staging_ec;
    const fs::path staging = reserve_working_path(outcome.destination.parent_path(),
                                                  WorkingEntryRole::Prepared, staging_ec);
    if (staging_ec) {
        return failure(staging_ec);
    }

    std::error_code relocate_ec;
    rename_step(RenameKind::Relocate, source, staging, relocate_ec);
    if (relocate_ec) {
        // The staging name was reserved, never created.
        return failure(relocate_ec);
    }

    const std::error_code install_ec = install_over(staging, outcome.destination, rename_step);
    if (install_ec) {
        unwind_staging(staging, source, true, rename_step);
        return failure(install_ec);
    }
    return outcome;
}

} // namespace detail

OperationOutcome copy_into(const fs::path& source, const fs::path& destination_directory,
                           const OperationOptions& options) {
    return detail::copy_into_using(source, destination_directory, options,
                                   &detail::rename_with_filesystem);
}

OperationOutcome move_into(const fs::path& source, const fs::path& destination_directory,
                           const OperationOptions& options) {
    return detail::move_into_using(source, destination_directory, options,
                                   &detail::rename_with_filesystem);
}

OperationOutcome rename_entry(const fs::path& source, std::string_view new_name,
                              const OperationOptions& options) {
    return detail::rename_entry_using(source, new_name, options, &detail::rename_with_filesystem);
}

} // namespace odysea::core
