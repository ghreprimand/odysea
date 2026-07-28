// Headless tests for the thumbnail cache policy.
//
// Everything here is pure: no threads, no decoder, no display server. The
// digest and escaping expectations are taken from published test vectors and
// from what other desktop implementations produce, never from this
// implementation's own output, so a mistake here cannot validate itself.
#include "digest.hpp"
#include "odysea/core/thumbnail.hpp"
#include "test_support.hpp"

#include <array>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <utility>

namespace fs = std::filesystem;

using odysea::core::StoredThumbnail;
using odysea::core::ThumbnailKey;
using odysea::core::ThumbnailSize;
using odysea::test::check;

namespace {

/// Restores the cache-location variables when a test finishes, so one test
/// cannot leak an environment into the next.
class ScopedCacheEnvironment {
  public:
    ScopedCacheEnvironment() = default;

    ScopedCacheEnvironment(const ScopedCacheEnvironment&) = delete;
    ScopedCacheEnvironment& operator=(const ScopedCacheEnvironment&) = delete;
    ScopedCacheEnvironment(ScopedCacheEnvironment&&) = delete;
    ScopedCacheEnvironment& operator=(ScopedCacheEnvironment&&) = delete;

    ~ScopedCacheEnvironment() {
        restore("XDG_CACHE_HOME", cache_home_);
        restore("HOME", home_);
    }

    static void set(const char* name, const std::string& value) {
        ::setenv(name, value.c_str(), 1); // NOLINT(concurrency-mt-unsafe)
    }

    static void clear(const char* name) {
        ::unsetenv(name); // NOLINT(concurrency-mt-unsafe)
    }

  private:
    static void restore(const char* name, const std::optional<std::string>& saved) {
        if (saved.has_value()) {
            set(name, *saved);
        } else {
            clear(name);
        }
    }

    static std::optional<std::string> capture(const char* name) {
        const char* value = std::getenv(name); // NOLINT(concurrency-mt-unsafe)
        if (value == nullptr) {
            return std::nullopt;
        }
        return std::string(value);
    }

    std::optional<std::string> cache_home_ = capture("XDG_CACHE_HOME");
    std::optional<std::string> home_ = capture("HOME");
};

enum class LinkHandling { Follow, DoNotFollow };

bool set_modification_time(const fs::path& path, std::time_t seconds, LinkHandling handling) {
    const std::array<::timespec, 2> times{::timespec{.tv_sec = seconds, .tv_nsec = 0},
                                          ::timespec{.tv_sec = seconds, .tv_nsec = 0}};
    const int flags = handling == LinkHandling::DoNotFollow ? AT_SYMLINK_NOFOLLOW : 0;
    return ::utimensat(AT_FDCWD, path.c_str(), times.data(), flags) == 0;
}

void test_the_digest_matches_published_vectors() {
    // The seven vectors published with the algorithm, then the lengths that
    // exercise every padding decision: one byte short of the length field, the
    // length that forces an extra block, a full block, and the same boundaries
    // one block later.
    const std::array<std::pair<std::string, std::string>, 7> published{{
        {"", "d41d8cd98f00b204e9800998ecf8427e"},
        {"a", "0cc175b9c0f1b6a831c399e269772661"},
        {"abc", "900150983cd24fb0d6963f7d28e17f72"},
        {"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
        {"abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"},
        {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
         "d174ab98d277d9f5a5611c2c9f419d9f"},
        {"12345678901234567890123456789012345678901234567890123456789012345678901234567890",
         "57edf4a22be3c955ac49da2e2107b67a"},
    }};
    for (const auto& [input, expected] : published) {
        check(odysea::core::detail::md5_hex(input) == expected,
              "the digest reproduces a published vector");
    }

    const std::array<std::pair<std::size_t, std::string>, 6> padding_boundaries{{
        {55, "04364420e25c512fd958a70738aa8f72"},
        {56, "668a72d5ba17f08e62dabcafad6db14b"},
        {63, "7dc2ca208106a2f703567bdff99d8981"},
        {64, "c1bb4f81d892b2d57947682aeb252456"},
        {119, "ab347a5f68c8a443cfcddc633f12c24f"},
        {120, "fb98667f98096de92620b64f46e1c5b5"},
    }};
    for (const auto& [length, expected] : padding_boundaries) {
        check(odysea::core::detail::md5_hex(std::string(length, 'x')) == expected,
              "the digest pads correctly at a block boundary");
    }
}

void test_the_uri_escapes_the_same_bytes_other_desktops_escape() {
    // Expectations produced by an established desktop URI implementation. The
    // cache file name is the digest of these exact bytes, so any divergence
    // would silently create a cache no other application can find.
    const std::array<std::pair<std::string, std::string>, 11> expectations{{
        {"/tmp/plain.png", "file:///tmp/plain.png"},
        {"/tmp/a b.png", "file:///tmp/a%20b.png"},
        {"/tmp/a&b.png", "file:///tmp/a&b.png"},
        {"/tmp/a;b.png", "file:///tmp/a%3Bb.png"},
        {"/tmp/a#b.png", "file:///tmp/a%23b.png"},
        {"/tmp/a%b.png", "file:///tmp/a%25b.png"},
        {"/tmp/a?b.png", "file:///tmp/a%3Fb.png"},
        {"/tmp/!~*()-._", "file:///tmp/!~*()-._"},
        {"/tmp/a'b+c,d=e:f\x40g$h.png", "file:///tmp/a'b+c,d=e:f\x40g$h.png"},
        {"/tmp/a[b]c\"d<e>f{g}h|i\\j^k`l", "file:///tmp/a%5Bb%5Dc%22d%3Ce%3Ef%7Bg%7Dh%7Ci%5Cj%5Ek"
                                           "%60l"},
        {"/tmp/\xc3\xa9.png", "file:///tmp/%C3%A9.png"},
    }};
    for (const auto& [path, expected] : expectations) {
        check(odysea::core::file_uri(path) == expected, "a file URI escapes the standard byte set");
    }

    // A file name is a byte string, not text. Bytes that decode as nothing at
    // all still have to produce a stable URI.
    check(odysea::core::file_uri(fs::path(std::string("/tmp/\xff\xfe"))) == "file:///tmp/%FF%FE",
          "a file URI escapes bytes that are not valid text");
}

void test_the_uri_addresses_a_path_the_way_a_file_is_named() {
    check(odysea::core::file_uri("/tmp/pictures/") == "file:///tmp/pictures",
          "a trailing separator does not change which file is addressed");
    check(odysea::core::file_uri("/tmp/a/../b.png") == "file:///tmp/b.png",
          "redundant path components are removed");
    check(odysea::core::file_uri("/tmp//a///b.png") == "file:///tmp/a/b.png",
          "repeated separators are collapsed");
    check(odysea::core::file_uri("/").starts_with("file:///"), "the root path stays addressable");
}

void test_the_cache_name_is_the_digest_of_the_uri() {
    check(odysea::core::thumbnail_name("file:///tmp/odysea/example.png") ==
              "e7c5b377cadfa92e585704d07f480c97.png",
          "a cache name is the digest of the source URI");
    check(odysea::core::thumbnail_name("file:///tmp/odysea/a&b%3Bc.png") ==
              "da66f5791e343378337dad44a2125b24.png",
          "a cache name covers escaped and literal bytes alike");
}

void test_the_cache_layout_follows_the_standard(const fs::path& root) {
    const ScopedCacheEnvironment environment;
    std::error_code ec;

    ScopedCacheEnvironment::set("XDG_CACHE_HOME", (root / "cache").string());
    check(odysea::core::thumbnail_base_directory(ec) == root / "cache" / "thumbnails" && !ec,
          "the cache root honours the cache-home variable");

    const std::array<std::pair<ThumbnailSize, std::string_view>, 4> directories{{
        {ThumbnailSize::Normal, "normal"},
        {ThumbnailSize::Large, "large"},
        {ThumbnailSize::ExtraLarge, "x-large"},
        {ThumbnailSize::XxLarge, "xx-large"},
    }};
    for (const auto& [size, name] : directories) {
        check(odysea::core::thumbnail_directory(size, ec).filename() == name && !ec,
              "each size has its standard directory");
    }

    const std::array<std::pair<ThumbnailSize, std::uint32_t>, 4> edges{{
        {ThumbnailSize::Normal, 128},
        {ThumbnailSize::Large, 256},
        {ThumbnailSize::ExtraLarge, 512},
        {ThumbnailSize::XxLarge, 1024},
    }};
    for (const auto& [size, edge] : edges) {
        check(odysea::core::thumbnail_edge_pixels(size) == edge,
              "each size has its standard pixel bound");
    }

    const fs::path failures = odysea::core::thumbnail_failure_directory(ec);
    check(!ec && failures.parent_path().filename() == "fail",
          "refused sources are recorded under the failure directory");
    check(failures.filename().string().starts_with("odysea-"),
          "the failure directory is namespaced by application and version");

    const ThumbnailKey key{.uri = "file:///tmp/odysea/example.png",
                           .modified_seconds = 1,
                           .size = 2,
                           .edge = ThumbnailSize::Large};
    check(odysea::core::thumbnail_path(key, ec) == root / "cache" / "thumbnails" / "large" /
                                                       "e7c5b377cadfa92e585704d07f480c97.png" &&
              !ec,
          "a key names exactly one file in the shared cache");

    ScopedCacheEnvironment::clear("XDG_CACHE_HOME");
    ScopedCacheEnvironment::set("HOME", (root / "home").string());
    check(odysea::core::thumbnail_base_directory(ec) == root / "home" / ".cache" / "thumbnails" &&
              !ec,
          "the cache root falls back to the home directory");

    ScopedCacheEnvironment::set("HOME", "relative/not/absolute");
    static_cast<void>(odysea::core::thumbnail_base_directory(ec));
    check(ec == std::errc::invalid_argument,
          "an unusable cache location is reported rather than guessed");
}

void test_a_cache_directory_is_never_thumbnailed(const fs::path& root) {
    const ScopedCacheEnvironment environment;
    ScopedCacheEnvironment::set("XDG_CACHE_HOME", (root / "cache").string());

    check(odysea::core::thumbnail_is_excluded(root / "cache" / "thumbnails" / "normal" / "a.png"),
          "a stored thumbnail is never itself thumbnailed");
    check(odysea::core::thumbnail_is_excluded(root / "cache" / "thumbnails"),
          "the cache root is excluded");
    check(!odysea::core::thumbnail_is_excluded(root / "cache" / "other" / "a.png"),
          "a neighbouring cache directory stays eligible");
    check(!odysea::core::thumbnail_is_excluded(root / "pictures" / "a.png"),
          "an ordinary source stays eligible");
}

void test_a_key_describes_the_contents_a_path_resolves_to(const fs::path& root) {
    const fs::path directory = root / "sources";
    fs::create_directories(directory);

    const fs::path target = directory / "target.png";
    std::ofstream(target) << "payload";
    const fs::path link = directory / "link.png";
    std::error_code link_ec;
    fs::create_symlink(target, link, link_ec);

    constexpr std::time_t target_time = 1000000000;
    constexpr std::time_t link_time = 1500000000;
    check(set_modification_time(target, target_time, LinkHandling::Follow),
          "the fixture can time-stamp a source");

    std::error_code ec;
    const auto target_key = odysea::core::thumbnail_key_for(target, ThumbnailSize::Normal, ec);
    check(!ec && target_key.has_value(), "a readable source yields a key");
    check(target_key.has_value() && target_key->modified_seconds == target_time,
          "a key carries the source modification time");
    check(target_key.has_value() && target_key->size == 7, "a key carries the source size");
    check(target_key.has_value() && target_key->uri == odysea::core::file_uri(target),
          "a key addresses the source it was asked about");

    if (!link_ec && set_modification_time(link, link_time, LinkHandling::DoNotFollow)) {
        const auto link_key = odysea::core::thumbnail_key_for(link, ThumbnailSize::Normal, ec);
        check(!ec && link_key.has_value(), "a symbolic link yields a key");
        // The distinction the shell depends on: the link is addressed as
        // itself, so it appears under its own name, but the thumbnail goes
        // stale when the contents it points at change, not when the link does.
        check(link_key.has_value() && link_key->uri == odysea::core::file_uri(link),
              "a symbolic link is addressed as itself");
        check(link_key.has_value() && link_key->modified_seconds == target_time,
              "a symbolic link is validated against the contents it resolves to");
        check(link_key.has_value() && link_key->size == 7,
              "a symbolic link carries the size of its target");
    }

    static_cast<void>(
        odysea::core::thumbnail_key_for(directory / "absent.png", ThumbnailSize::Normal, ec));
    check(ec == std::errc::no_such_file_or_directory, "a missing source reports its cause");

    static_cast<void>(odysea::core::thumbnail_key_for(directory, ThumbnailSize::Normal, ec));
    check(ec == std::errc::invalid_argument, "a source that is not a regular file is refused");

    fs::remove_all(directory);
}

void test_a_stored_thumbnail_is_verified_before_it_is_shown() {
    const ThumbnailKey key{.uri = "file:///tmp/odysea/example.png",
                           .modified_seconds = 1000,
                           .size = 4096,
                           .edge = ThumbnailSize::Normal};

    const StoredThumbnail current{
        .image = {}, .uri = key.uri, .modified_seconds = 1000, .size = 4096, .size_recorded = true};
    check(odysea::core::thumbnail_matches(current, key), "a current record is accepted");

    StoredThumbnail stale = current;
    stale.modified_seconds = 999;
    check(!odysea::core::thumbnail_matches(stale, key), "a record from an older revision is stale");

    StoredThumbnail resized = current;
    resized.size = 8192;
    check(!odysea::core::thumbnail_matches(resized, key),
          "a record describing a different length is stale");

    StoredThumbnail without_size = current;
    without_size.size = 0;
    without_size.size_recorded = false;
    check(odysea::core::thumbnail_matches(without_size, key),
          "a record from a writer that omitted the length is still usable");

    // Two sources can share a cache file name: the digest carries no collision
    // guarantee. The recorded URI is what settles it, so a record whose other
    // fields agree must still be rejected when it describes another file.
    StoredThumbnail foreign = current;
    foreign.uri = "file:///tmp/odysea/other.png";
    check(!odysea::core::thumbnail_matches(foreign, key),
          "a record describing a different source is refused even when its metadata agrees");
}

} // namespace

int main() {
    const odysea::test::TemporaryTree tree("thumbnail_policy");

    test_the_digest_matches_published_vectors();
    test_the_uri_escapes_the_same_bytes_other_desktops_escape();
    test_the_uri_addresses_a_path_the_way_a_file_is_named();
    test_the_cache_name_is_the_digest_of_the_uri();
    test_the_cache_layout_follows_the_standard(tree.root());
    test_a_cache_directory_is_never_thumbnailed(tree.root());
    test_a_key_describes_the_contents_a_path_resolves_to(tree.root());
    test_a_stored_thumbnail_is_verified_before_it_is_shown();

    return odysea::test::report("core_thumbnail");
}
