#include "odysea/core/trash.hpp"

#include "odysea/core/descriptor.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace odysea::core {
namespace fs = std::filesystem;

namespace {

constexpr ::mode_t trash_directory_mode = 0700;

std::error_code errno_error() {
    return std::error_code(errno, std::generic_category());
}

std::error_code make_error(std::errc code) {
    return std::make_error_code(code);
}

TrashOutcome failure(std::error_code error) {
    return TrashOutcome{.trashed_path = {}, .info_path = {}, .error = error};
}

TrashOutcome failure(std::errc code) {
    return failure(make_error(code));
}

/// Read an environment variable, treating an empty value as unset.
const char* environment_value(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return nullptr;
    }
    return value;
}

/// Device identifier of a path, following no symlink of its own.
bool device_of(const fs::path& path, ::dev_t& device) {
    struct ::stat info{};
    if (::lstat(path.c_str(), &info) != 0) {
        return false;
    }
    device = info.st_dev;
    return true;
}

/// Walk upward until the filesystem changes: the mount point holding `path`.
fs::path top_directory_of(const fs::path& path, std::error_code& error) {
    ::dev_t start_device = 0;
    if (!device_of(path, start_device)) {
        error = errno_error();
        return {};
    }

    fs::path current = path;
    while (current.has_parent_path() && current.parent_path() != current) {
        const fs::path parent = current.parent_path();
        ::dev_t parent_device = 0;
        if (!device_of(parent, parent_device)) {
            error = errno_error();
            return {};
        }
        if (parent_device != start_device) {
            return current;
        }
        current = parent;
    }
    return current;
}

bool make_directory(const fs::path& path, std::error_code& error) {
    if (::mkdir(path.c_str(), trash_directory_mode) == 0) {
        return true;
    }
    if (errno == EEXIST) {
        struct ::stat info{};
        if (::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) {
            return true;
        }
        error = make_error(std::errc::not_a_directory);
        return false;
    }
    error = errno_error();
    return false;
}

/// Create `files` and `info` under a trash directory, plus the directory itself.
bool ensure_trash_layout(const fs::path& trash, std::error_code& error) {
    std::error_code parent_error;
    fs::create_directories(trash.parent_path(), parent_error);
    if (parent_error) {
        error = parent_error;
        return false;
    }
    return make_directory(trash, error) && make_directory(trash / "files", error) &&
           make_directory(trash / "info", error);
}

/// A shared `.Trash` is only trustworthy when it is a sticky real directory.
bool shared_trash_is_usable(const fs::path& shared_trash) {
    struct ::stat info{};
    if (::lstat(shared_trash.c_str(), &info) != 0) {
        return false;
    }
    return S_ISDIR(info.st_mode) && (info.st_mode & S_ISVTX) != 0;
}

std::string current_deletion_date() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    if (::localtime_r(&now, &local) == nullptr) {
        return "1970-01-01T00:00:00";
    }
    std::array<char, 32> buffer{};
    const std::size_t written =
        std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%S", &local);
    return std::string(buffer.data(), written);
}

/// The candidate name for attempt n: "report.txt", then "report_1.txt".
std::string candidate_name(const fs::path& original, unsigned attempt) {
    if (attempt == 0) {
        return original.filename().string();
    }
    return original.stem().string() + "_" + std::to_string(attempt) + original.extension().string();
}

bool write_all(int descriptor, std::string_view payload, std::error_code& error) {
    std::size_t written = 0;
    while (written < payload.size()) {
        const ::ssize_t chunk =
            ::write(descriptor, payload.data() + written, payload.size() - written);
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = errno_error();
            return false;
        }
        written += static_cast<std::size_t>(chunk);
    }
    return true;
}

} // namespace

std::string encode_trash_location(std::string_view path) {
    constexpr std::string_view literal_extras = "-_.!~*'()/";
    constexpr std::string_view hex_digits = "0123456789ABCDEF";

    std::string encoded;
    encoded.reserve(path.size());
    for (const char raw : path) {
        const auto value = static_cast<unsigned char>(raw);
        const bool alphanumeric = (value >= '0' && value <= '9') ||
                                  (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
        if (alphanumeric || literal_extras.find(raw) != std::string_view::npos) {
            encoded.push_back(raw);
            continue;
        }
        encoded.push_back('%');
        encoded.push_back(hex_digits[value >> 4U]);
        encoded.push_back(hex_digits[value & 0x0FU]);
    }
    return encoded;
}

fs::path home_trash_directory(std::error_code& error) {
    if (const char* data_home = environment_value("XDG_DATA_HOME")) {
        const fs::path base(data_home);
        if (base.is_absolute()) {
            return base / "Trash";
        }
    }
    if (const char* home = environment_value("HOME")) {
        const fs::path base(home);
        if (base.is_absolute()) {
            return base / ".local" / "share" / "Trash";
        }
    }
    error = make_error(std::errc::invalid_argument);
    return {};
}

fs::path trash_directory_for(const fs::path& path, std::error_code& error) {
    const fs::path home_trash = home_trash_directory(error);
    if (error) {
        return {};
    }

    // The home trash may not exist yet, so compare against the nearest
    // ancestor that does: that is the filesystem it will be created on.
    fs::path existing_ancestor = home_trash;
    ::dev_t home_device = 0;
    while (!device_of(existing_ancestor, home_device)) {
        if (!existing_ancestor.has_parent_path() ||
            existing_ancestor.parent_path() == existing_ancestor) {
            error = make_error(std::errc::no_such_file_or_directory);
            return {};
        }
        existing_ancestor = existing_ancestor.parent_path();
    }

    ::dev_t source_device = 0;
    if (!device_of(path, source_device)) {
        error = errno_error();
        return {};
    }

    if (source_device == home_device) {
        if (!ensure_trash_layout(home_trash, error)) {
            return {};
        }
        return home_trash;
    }

    const fs::path top_directory = top_directory_of(path, error);
    if (error) {
        return {};
    }

    const std::string user_id = std::to_string(::getuid());
    const fs::path shared_trash = top_directory / ".Trash";
    if (shared_trash_is_usable(shared_trash)) {
        const fs::path per_user = shared_trash / user_id;
        std::error_code shared_error;
        if (ensure_trash_layout(per_user, shared_error)) {
            return per_user;
        }
    }

    const fs::path fallback_trash = top_directory / (".Trash-" + user_id);
    if (!ensure_trash_layout(fallback_trash, error)) {
        return {};
    }
    return fallback_trash;
}

TrashOutcome move_to_trash(const fs::path& source) {
    if (source.empty()) {
        return failure(std::errc::invalid_argument);
    }

    std::error_code status_error;
    const fs::file_status status = fs::symlink_status(source, status_error);
    if (status_error || !fs::exists(status)) {
        return failure(std::errc::no_such_file_or_directory);
    }

    std::error_code absolute_error;
    const fs::path absolute_source = fs::absolute(source, absolute_error).lexically_normal();
    if (absolute_error) {
        return failure(absolute_error);
    }
    if (!absolute_source.has_relative_path()) {
        return failure(std::errc::invalid_argument);
    }

    std::error_code trash_error;
    const fs::path trash = trash_directory_for(absolute_source, trash_error);
    if (trash_error) {
        return failure(trash_error);
    }

    const std::string record =
        "[Trash Info]\nPath=" + encode_trash_location(absolute_source.string()) +
        "\nDeletionDate=" + current_deletion_date() + "\n";

    // Claim a free name by creating its record exclusively, so two concurrent
    // deletions of the same file name cannot resolve to the same slot.
    constexpr unsigned max_attempts = 10000;
    for (unsigned attempt = 0; attempt < max_attempts; ++attempt) {
        const std::string name = candidate_name(absolute_source, attempt);
        const fs::path info_path = trash / "info" / (name + ".trashinfo");
        const fs::path files_path = trash / "files" / name;

        Descriptor record_file(
            ::open(info_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
        if (!record_file.valid()) {
            if (errno == EEXIST) {
                continue;
            }
            return failure(errno_error());
        }

        std::error_code write_error;
        const bool written = write_all(record_file.get(), record, write_error);
        record_file.reset();
        if (!written) {
            std::error_code cleanup;
            fs::remove(info_path, cleanup);
            return failure(write_error);
        }

        std::error_code exists_error;
        if (fs::exists(fs::symlink_status(files_path, exists_error))) {
            std::error_code cleanup;
            fs::remove(info_path, cleanup);
            continue;
        }

        std::error_code move_error;
        fs::rename(absolute_source, files_path, move_error);
        if (move_error == std::errc::cross_device_link) {
            // The trash lives on another filesystem only when the top-level
            // lookup failed; fall back to a copy so the delete still lands.
            std::error_code copy_error;
            fs::copy(absolute_source, files_path,
                     fs::copy_options::recursive | fs::copy_options::copy_symlinks, copy_error);
            if (!copy_error) {
                fs::remove_all(absolute_source, move_error);
            } else {
                move_error = copy_error;
            }
        }
        if (move_error) {
            std::error_code cleanup;
            fs::remove(info_path, cleanup);
            fs::remove_all(files_path, cleanup);
            return failure(move_error);
        }

        return TrashOutcome{.trashed_path = files_path, .info_path = info_path, .error = {}};
    }

    return failure(std::errc::file_exists);
}

} // namespace odysea::core
