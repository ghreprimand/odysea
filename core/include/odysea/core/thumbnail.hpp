// OdySea core: thumbnail cache policy.
//
// Toolkit-agnostic. Decoding an image needs a codec, and the application shell
// already links one, so pixels are produced outside this library. Everything
// that decides *which* thumbnail is wanted, *where* it belongs, and *whether* a
// stored one still describes its source lives here, where it can be verified
// headless and without a decoder.
//
// The layout and validity rules follow the freedesktop.org Thumbnail Managing
// Standard, so thumbnails written by this application are usable by other
// desktops and thumbnails written by them are usable here.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace odysea::core {

/// The standard thumbnail sizes. The value names the directory the standard
/// stores that size in; the pixel edge is the longest side of the result.
enum class ThumbnailSize { Normal, Large, ExtraLarge, XxLarge };

/// The longest-edge pixel bound for a size: 128, 256, 512, or 1024.
[[nodiscard]] std::uint32_t thumbnail_edge_pixels(ThumbnailSize size) noexcept;

/// The standard directory name for a size: normal, large, x-large, xx-large.
[[nodiscard]] std::string_view thumbnail_size_name(ThumbnailSize size) noexcept;

/// Everything that decides whether a stored thumbnail still describes a file.
///
/// Two keys that compare equal name the same cached image. A change to the
/// source's modification time or size therefore produces a different key, which
/// is what makes a stale thumbnail impossible to mistake for a current one.
struct ThumbnailKey {
    /// Canonical `file://` URI of the source, as addressed by the caller.
    std::string uri;
    /// Whole seconds of the source's last modification.
    std::int64_t modified_seconds = 0;
    /// Byte size of the source.
    std::uintmax_t size = 0;
    ThumbnailSize edge = ThumbnailSize::Normal;

    [[nodiscard]] bool operator==(const ThumbnailKey& other) const noexcept = default;
};

/// Decoded pixels, free of any toolkit type: RGBA8888, tightly packed, rows in
/// top-to-bottom order, so `pixels.size()` equals `width * height * 4`.
struct ThumbnailImage {
    std::vector<std::byte> pixels;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    /// Bytes this image occupies, the unit the memory cache is bounded in.
    [[nodiscard]] std::size_t byte_cost() const noexcept { return pixels.size(); }
    [[nodiscard]] bool empty() const noexcept { return pixels.empty(); }
};

/// A thumbnail read back from persistent storage, together with the source
/// description it claims to describe.
struct StoredThumbnail {
    ThumbnailImage image;
    /// The `Thumb::URI` recorded when the thumbnail was written.
    std::string uri;
    /// The `Thumb::MTime` recorded when the thumbnail was written.
    std::int64_t modified_seconds = 0;
    /// The `Thumb::Size` recorded when the thumbnail was written. The standard
    /// makes this optional, so `size_recorded` says whether it is meaningful.
    std::uintmax_t size = 0;
    bool size_recorded = false;
};

/// The canonical `file://` URI for a local path.
///
/// The escaping matches what other desktop file managers produce, because the
/// cache file name is derived from these exact bytes: alphanumerics, the bytes
/// `!$&'()*+,-./:=_~`, and the commercial-at byte stay literal, while every
/// other byte is percent-encoded with uppercase hexadecimal. Path bytes are
/// escaped as they are, without assuming any character encoding, since a Linux
/// file name is a byte string.
///
/// A relative path is resolved against the current directory when that
/// succeeds. Redundant separators and `.`/`..` components are removed
/// lexically; symbolic links are deliberately not resolved, so a link and its
/// target are addressed, and cached, separately.
[[nodiscard]] std::string file_uri(const std::filesystem::path& path);

/// The cache file name for a source URI: its digest followed by `.png`.
[[nodiscard]] std::string thumbnail_name(std::string_view uri);

/// The root of the shared thumbnail cache: `XDG_CACHE_HOME/thumbnails`, or
/// `HOME/.cache/thumbnails` when that variable is unset or not absolute.
///
/// Reports `std::errc::invalid_argument` when neither variable yields an
/// absolute path. Creates nothing.
[[nodiscard]] std::filesystem::path thumbnail_base_directory(std::error_code& error);

/// The directory holding thumbnails of one size. Creates nothing.
[[nodiscard]] std::filesystem::path thumbnail_directory(ThumbnailSize size, std::error_code& error);

/// The directory recording sources this application could not thumbnail.
///
/// Namespaced by application and version, as the standard requires, so a later
/// version retries what an earlier one refused. Creates nothing.
[[nodiscard]] std::filesystem::path thumbnail_failure_directory(std::error_code& error);

/// The full path of the cached thumbnail a key names. Creates nothing.
[[nodiscard]] std::filesystem::path thumbnail_path(const ThumbnailKey& key, std::error_code& error);

/// Whether a source must never be thumbnailed into the shared cache.
///
/// Thumbnails of thumbnails are excluded: the standard forbids them, and
/// without this a cache directory browsed in the shell would grow without
/// bound. A path that cannot be placed relative to the cache is not excluded,
/// because no cache location was resolved for it to pollute.
[[nodiscard]] bool thumbnail_is_excluded(const std::filesystem::path& path);

/// Describe the source at `path` for thumbnailing.
///
/// The URI addresses `path` as given, while the modification time and size come
/// from the file it ultimately resolves to. That asymmetry is deliberate: a
/// symbolic link is addressed as itself so the shell can show it under its own
/// name, but its thumbnail must go stale when the *contents* it points at
/// change. This is why listing metadata, which describes the link, cannot be
/// reused here.
///
/// Reports through `error` and yields no key when the source cannot be
/// examined, and `std::errc::invalid_argument` when it is not a regular file.
/// Never throws.
[[nodiscard]] std::optional<ThumbnailKey>
thumbnail_key_for(const std::filesystem::path& path, ThumbnailSize size, std::error_code& error);

/// Whether a stored thumbnail may be shown for a key.
///
/// The recorded URI must match, not only the digest that led to the file: the
/// digest is a naming scheme with no collision guarantee, so a record that
/// describes a different source has to be rejected on inspection. The recorded
/// modification time must match, and the recorded size must match whenever the
/// writer recorded one.
[[nodiscard]] bool thumbnail_matches(const StoredThumbnail& stored, const ThumbnailKey& key);

} // namespace odysea::core

template <>
struct std::hash<odysea::core::ThumbnailKey> {
    [[nodiscard]] std::size_t operator()(const odysea::core::ThumbnailKey& key) const noexcept;
};
