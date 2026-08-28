#include "odysea/core/thumbnail.hpp"

#include "digest.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <sys/stat.h>

#ifndef ODYSEA_THUMBNAIL_NAMESPACE
#error "ODYSEA_THUMBNAIL_NAMESPACE must be defined by the build"
#endif

namespace odysea::core {
namespace fs = std::filesystem;

namespace {

/// Bytes left literal in a file URI. Alphanumerics are handled separately.
///
/// This is the set the shared thumbnail cache is keyed on, and it is not the
/// same as the set the trash specification wants: `;` is escaped here, while
/// `$&+,=` and the commercial-at byte are not. The cache file name is the
/// digest of these exact bytes, so a divergence would silently produce a
/// private cache that nothing else on the system can find, and would leave this
/// application blind to the entries already there.
///
/// The commercial-at byte is spelled as an escape because tracked text in this
/// repository keeps that character out of prose and literals.
constexpr std::string_view literal_uri_bytes = "!$&'()*+,-./:="
                                               "\x40"
                                               "_~";

constexpr std::string_view uri_scheme = "file://";

[[nodiscard]] std::error_code make_error(std::errc code) {
    return std::make_error_code(code);
}

[[nodiscard]] const char* environment_value(const char* name) {
    const char* value = std::getenv(name); // NOLINT(concurrency-mt-unsafe)
    return (value != nullptr && *value != '\0') ? value : nullptr;
}

/// Remove a trailing separator so `/tmp/pictures/` and `/tmp/pictures` address
/// the same file, as every URI producer does.
[[nodiscard]] std::string without_trailing_separator(std::string text) {
    while (text.size() > 1 && text.back() == '/') {
        text.pop_back();
    }
    return text;
}

[[nodiscard]] bool path_is_within(const fs::path& base, const fs::path& candidate) {
    const fs::path normal_base = base.lexically_normal();
    const fs::path normal_candidate = candidate.lexically_normal();
    const auto relative = normal_candidate.lexically_relative(normal_base);
    if (relative.empty()) {
        return false;
    }
    return *relative.begin() != "..";
}

} // namespace

std::uint32_t thumbnail_edge_pixels(ThumbnailSize size) noexcept {
    switch (size) {
    case ThumbnailSize::Normal:
        return 128;
    case ThumbnailSize::Large:
        return 256;
    case ThumbnailSize::ExtraLarge:
        return 512;
    case ThumbnailSize::XxLarge:
        return 1024;
    }
    return 128;
}

std::string_view thumbnail_size_name(ThumbnailSize size) noexcept {
    switch (size) {
    case ThumbnailSize::Normal:
        return "normal";
    case ThumbnailSize::Large:
        return "large";
    case ThumbnailSize::ExtraLarge:
        return "x-large";
    case ThumbnailSize::XxLarge:
        return "xx-large";
    }
    return "normal";
}

std::string file_uri(const fs::path& path) {
    constexpr std::string_view hex_digits = "0123456789ABCDEF";

    fs::path absolute = path;
    if (!absolute.is_absolute()) {
        std::error_code ec;
        const fs::path resolved = fs::absolute(path, ec);
        if (!ec) {
            absolute = resolved;
        }
    }

    const std::string text = without_trailing_separator(absolute.lexically_normal().string());

    std::string uri(uri_scheme);
    uri.reserve(uri.size() + text.size());
    for (const char raw : text) {
        const auto value = static_cast<unsigned char>(raw);
        const bool alphanumeric = (value >= '0' && value <= '9') ||
                                  (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
        if (alphanumeric || literal_uri_bytes.find(raw) != std::string_view::npos) {
            uri.push_back(raw);
            continue;
        }
        uri.push_back('%');
        uri.push_back(hex_digits[value >> 4U]);
        uri.push_back(hex_digits[value & 0x0FU]);
    }
    return uri;
}

std::string thumbnail_name(std::string_view uri) {
    return detail::md5_hex(uri) + ".png";
}

fs::path thumbnail_base_directory(std::error_code& error) {
    error.clear();
    if (const char* cache_home = environment_value("XDG_CACHE_HOME")) {
        const fs::path base(cache_home);
        if (base.is_absolute()) {
            return base / "thumbnails";
        }
    }
    if (const char* home = environment_value("HOME")) {
        const fs::path base(home);
        if (base.is_absolute()) {
            return base / ".cache" / "thumbnails";
        }
    }
    error = make_error(std::errc::invalid_argument);
    return {};
}

fs::path thumbnail_directory(ThumbnailSize size, std::error_code& error) {
    const fs::path base = thumbnail_base_directory(error);
    if (error) {
        return {};
    }
    return base / thumbnail_size_name(size);
}

fs::path thumbnail_failure_directory(std::error_code& error) {
    const fs::path base = thumbnail_base_directory(error);
    if (error) {
        return {};
    }
    return base / "fail" / ODYSEA_THUMBNAIL_NAMESPACE;
}

fs::path thumbnail_path(const ThumbnailKey& key, std::error_code& error) {
    const fs::path directory = thumbnail_directory(key.edge, error);
    if (error) {
        return {};
    }
    return directory / thumbnail_name(key.uri);
}

bool thumbnail_is_excluded(const fs::path& path) {
    std::error_code error;
    const fs::path base = thumbnail_base_directory(error);
    if (error) {
        return false;
    }

    fs::path absolute = path;
    if (!absolute.is_absolute()) {
        std::error_code absolute_error;
        const fs::path resolved = fs::absolute(path, absolute_error);
        if (absolute_error) {
            return false;
        }
        absolute = resolved;
    }
    return path_is_within(base, absolute);
}

std::optional<ThumbnailKey> thumbnail_key_for(const fs::path& path, ThumbnailSize size,
                                              std::error_code& error) {
    error.clear();

    // Deliberately follows symbolic links: the thumbnail describes contents,
    // and the contents live at the target. The URI keeps addressing the path as
    // the caller named it.
    struct ::stat metadata{};
    if (::stat(path.c_str(), &metadata) != 0) {
        error = std::error_code(errno, std::generic_category());
        return std::nullopt;
    }
    if (!S_ISREG(metadata.st_mode)) {
        error = make_error(std::errc::invalid_argument);
        return std::nullopt;
    }

    return ThumbnailKey{.uri = file_uri(path),
                        .modified_seconds = static_cast<std::int64_t>(metadata.st_mtim.tv_sec),
                        .size = static_cast<std::uintmax_t>(metadata.st_size),
                        .edge = size};
}

bool thumbnail_matches(const StoredThumbnail& stored, const ThumbnailKey& key) {
    if (stored.uri != key.uri) {
        return false;
    }
    if (stored.modified_seconds != key.modified_seconds) {
        return false;
    }
    return !stored.size_recorded || stored.size == key.size;
}

} // namespace odysea::core

std::size_t std::hash<odysea::core::ThumbnailKey>::operator()(
    const odysea::core::ThumbnailKey& key) const noexcept {
    const std::size_t uri_hash = std::hash<std::string>{}(key.uri);
    const std::size_t time_hash = std::hash<std::int64_t>{}(key.modified_seconds);
    const std::size_t size_hash = std::hash<std::uintmax_t>{}(key.size);
    const std::size_t edge_hash = static_cast<std::size_t>(key.edge);

    std::size_t combined = uri_hash;
    for (const std::size_t part : std::array<std::size_t, 3>{time_hash, size_hash, edge_hash}) {
        constexpr auto spread = static_cast<std::size_t>(0x9e3779b97f4a7c15ULL);
        combined ^= part + spread + (combined << 6U) + (combined >> 2U);
    }
    return combined;
}
