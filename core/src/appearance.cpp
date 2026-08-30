// OdySea core: appearance settings model.
//
// See the header for the model's contract. The preset tables below are the
// accepted product values for each effect profile; the presentation layer
// renders them, it does not redefine them.
#include "odysea/core/appearance.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>

namespace odysea::core {

namespace {

constexpr double kBloomCoreMax = 0.8;
constexpr double kBloomWideMax = 1.0;
constexpr double kScanlineMax = 0.35;
constexpr double kVignetteMax = 0.45;
constexpr double kPersistenceMax = 1.0;
constexpr double kDeepFieldMax = 1.0;
constexpr double kTextLiftMin = 1.0;
constexpr double kTextLiftMax = 1.5;
constexpr double kScaleMin = 0.75;
constexpr double kScaleMax = 2.0;
constexpr double kGlassOpacityMin = 0.2;
constexpr double kSurfaceOpacityMin = 0.45;
constexpr double kSplitRatioMin = 0.25;
constexpr double kSplitRatioMax = 0.75;
constexpr std::size_t kMaximumStoredLabelLength = 128;
constexpr std::size_t kMaximumStoredPathLength = 4096;

[[nodiscard]] double clamped(double value, double lo, double hi) noexcept {
    // NaN compares false against both bounds, so std::clamp would pass it
    // through untouched. A non-finite value pins to the low bound instead:
    // every documented range treats its low end as the safe minimum.
    if (!std::isfinite(value)) {
        return lo;
    }
    return std::clamp(value, lo, hi);
}

/// Parses a decimal double without locale or exception behavior. Returns
/// `fallback` when `text` is not entirely a finite number: `nan` and `inf`
/// are valid spellings to the parser but poison every later comparison, so
/// they count as malformed here and keep the field's default.
[[nodiscard]] double parse_double(std::string_view text, double fallback) noexcept {
    double value = 0.0;
    const char* const first = text.data();
    const char* const last =
        first + text.size(); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    // The view's size travels with the call as `last`; std::from_chars never
    // reads past it and needs no terminator.
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
    const auto [ptr, err] = std::from_chars(first, last, value);
    if (err != std::errc() || ptr != last || !std::isfinite(value)) {
        return fallback;
    }
    return value;
}

/// Removes ASCII control characters and DEL from a stored string. The
/// persisted form is line-oriented with no escaping, so a value that could
/// carry a newline would be able to write arbitrary later keys.
[[nodiscard]] std::string without_control_characters(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte >= 0x20 && byte != 0x7F) {
            out.push_back(ch);
        }
    }
    return out;
}

[[nodiscard]] bool parse_bool(std::string_view text, bool fallback) noexcept {
    if (text == "true" || text == "1") {
        return true;
    }
    if (text == "false" || text == "0") {
        return false;
    }
    return fallback;
}

void apply_accent_preset(AppearanceSettings& settings, std::string_view key,
                         std::string_view value) {
    if (key == "accent_preset" && !value.empty()) {
        settings.accent_preset = std::string(value);
    }
}

[[nodiscard]] std::optional<std::size_t> parse_size(std::string_view text) noexcept {
    std::size_t value = 0;
    const char* const first = text.data();
    const char* const last =
        first + text.size(); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
    const auto [ptr, err] = std::from_chars(first, last, value);
    if (err != std::errc() || ptr != last) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] bool is_unreserved(unsigned char byte) noexcept {
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.' ||
           byte == '~' || byte == '/';
}

[[nodiscard]] std::string encode_setting_value(std::string_view value) {
    static constexpr std::string_view hex = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (is_unreserved(byte)) {
            encoded.push_back(character);
            continue;
        }
        encoded.push_back('%');
        encoded.push_back(hex.at((byte >> 4U) & 0x0FU));
        encoded.push_back(hex.at(byte & 0x0FU));
    }
    return encoded;
}

[[nodiscard]] int hex_value(char character) noexcept {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    return -1;
}

[[nodiscard]] std::optional<std::string> decode_setting_value(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value.at(index) != '%') {
            decoded.push_back(value.at(index));
            continue;
        }
        if (index + 2 >= value.size()) {
            return std::nullopt;
        }
        const int high = hex_value(value.at(index + 1));
        const int low = hex_value(value.at(index + 2));
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        decoded.push_back(static_cast<char>((high << 4) | low));
        index += 2;
    }
    return decoded;
}

[[nodiscard]] std::optional<NavigationPlace> parse_place(std::string_view value) {
    const std::size_t separator = value.find('|');
    if (separator == std::string_view::npos) {
        return std::nullopt;
    }
    const std::optional<std::string> label = decode_setting_value(value.substr(0, separator));
    const std::optional<std::string> path = decode_setting_value(value.substr(separator + 1));
    if (!label || !path) {
        return std::nullopt;
    }
    return NavigationPlace{.label = *label, .path = *path};
}

[[nodiscard]] std::string_view trimmed(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

void append_key(std::string& out, std::string_view key, std::string_view value) {
    out.append(key);
    out.push_back('=');
    out.append(value);
    out.push_back('\n');
}

void append_key(std::string& out, std::string_view key, double value) {
    char buffer[32];
    const int written = std::snprintf(buffer, sizeof buffer, "%.4f", value);
    append_key(out, key, std::string_view(buffer, written > 0 ? static_cast<size_t>(written) : 0));
}

void append_key(std::string& out, std::string_view key, bool value) {
    append_key(out, key, value ? std::string_view("true") : std::string_view("false"));
}

} // namespace

EffectLevels effect_profile_levels(EffectProfile profile) noexcept {
    switch (profile) {
    case EffectProfile::Off:
        return EffectLevels{};
    case EffectProfile::Minimal:
        // Minimal keeps the picture entirely still — no emission, no banding —
        // but retains half the ground-material depth.
        return EffectLevels{.deep_field = 0.5};
    case EffectProfile::Strong:
        return EffectLevels{.bloom_core = 0.60,
                            .bloom_wide = 0.80,
                            .scanline = 0.26,
                            .vignette = 0.42,
                            .persistence = 1.0,
                            .deep_field = 1.0,
                            .text_lift = 1.25};
    case EffectProfile::Balanced:
    case EffectProfile::Custom:
        break;
    }
    return EffectLevels{.bloom_core = 0.45,
                        .bloom_wide = 0.50,
                        .scanline = 0.14,
                        .vignette = 0.30,
                        .persistence = 1.0,
                        .deep_field = 1.0,
                        .text_lift = 1.15};
}

EffectLevels clamp_effect_levels(const EffectLevels& levels) noexcept {
    return EffectLevels{.bloom_core = clamped(levels.bloom_core, 0.0, kBloomCoreMax),
                        .bloom_wide = clamped(levels.bloom_wide, 0.0, kBloomWideMax),
                        .scanline = clamped(levels.scanline, 0.0, kScanlineMax),
                        .vignette = clamped(levels.vignette, 0.0, kVignetteMax),
                        .persistence = clamped(levels.persistence, 0.0, kPersistenceMax),
                        .deep_field = clamped(levels.deep_field, 0.0, kDeepFieldMax),
                        .text_lift = clamped(levels.text_lift, kTextLiftMin, kTextLiftMax)};
}

AppearanceSettings clamp_appearance(const AppearanceSettings& settings) noexcept {
    AppearanceSettings result = settings;
    result.palette = without_control_characters(settings.palette);
    result.accent_preset = without_control_characters(settings.accent_preset);
    result.font_family = without_control_characters(settings.font_family);
    result.scale = clamped(settings.scale, kScaleMin, kScaleMax);
    result.glass_opacity = clamped(settings.glass_opacity, kGlassOpacityMin, 1.0);
    result.surface_opacity = clamped(settings.surface_opacity, kSurfaceOpacityMin, 1.0);
    result.split_ratio = clamped(settings.split_ratio, kSplitRatioMin, kSplitRatioMax);
    result.custom = clamp_effect_levels(settings.custom);

    result.places.clear();
    std::unordered_set<std::string> place_paths;
    for (const NavigationPlace& place : settings.places) {
        NavigationPlace clean{.label = without_control_characters(place.label),
                              .path = without_control_characters(place.path)};
        clean.label.resize(std::min(clean.label.size(), kMaximumStoredLabelLength));
        clean.path.resize(std::min(clean.path.size(), kMaximumStoredPathLength));
        if (clean.label.empty() || clean.path.empty() || clean.path.front() != '/' ||
            !place_paths.insert(clean.path).second) {
            continue;
        }
        result.places.push_back(std::move(clean));
        if (result.places.size() == AppearanceSettings::maximum_places) {
            break;
        }
    }

    result.recent_destinations.clear();
    std::unordered_set<std::string> recent_paths;
    for (const std::string& stored_path : settings.recent_destinations) {
        std::string clean = without_control_characters(stored_path);
        clean.resize(std::min(clean.size(), kMaximumStoredPathLength));
        if (clean.empty() || clean.front() != '/' || !recent_paths.insert(clean).second) {
            continue;
        }
        result.recent_destinations.push_back(std::move(clean));
        if (result.recent_destinations.size() == AppearanceSettings::maximum_recent_destinations) {
            break;
        }
    }
    return result;
}

EffectLevels effective_effect_levels(const AppearanceSettings& settings) noexcept {
    EffectLevels levels = settings.profile == EffectProfile::Custom
                              ? clamp_effect_levels(settings.custom)
                              : effect_profile_levels(settings.profile);
    if (settings.reduced_motion || settings.high_contrast) {
        // Accessibility overrides remove every animated or emissive source
        // from the composed scene. Deep-field material remains palette-side,
        // but glow, bloom, scanline, vignette, and persistence do not leave
        // a second visual or motion path behind either override.
        levels.bloom_core = 0.0;
        levels.bloom_wide = 0.0;
        levels.scanline = 0.0;
        levels.vignette = 0.0;
        levels.persistence = 0.0;
    }
    if (settings.high_contrast) {
        levels.text_lift = 1.0;
    }
    return levels;
}

std::string_view to_string(EffectProfile profile) noexcept {
    switch (profile) {
    case EffectProfile::Off:
        return "off";
    case EffectProfile::Minimal:
        return "minimal";
    case EffectProfile::Strong:
        return "strong";
    case EffectProfile::Custom:
        return "custom";
    case EffectProfile::Balanced:
        break;
    }
    return "balanced";
}

std::string_view to_string(FontSource source) noexcept {
    switch (source) {
    case FontSource::System:
        return "system";
    case FontSource::Named:
        return "named";
    case FontSource::Bundled:
        break;
    }
    return "bundled";
}

std::string_view to_string(Density density) noexcept {
    switch (density) {
    case Density::Compact:
        return "compact";
    case Density::Comfortable:
        return "comfortable";
    case Density::Cozy:
        break;
    }
    return "cozy";
}

EffectProfile effect_profile_from(std::string_view name) noexcept {
    if (name == "off") {
        return EffectProfile::Off;
    }
    if (name == "minimal") {
        return EffectProfile::Minimal;
    }
    if (name == "strong") {
        return EffectProfile::Strong;
    }
    if (name == "custom") {
        return EffectProfile::Custom;
    }
    return EffectProfile::Balanced;
}

FontSource font_source_from(std::string_view name) noexcept {
    if (name == "system") {
        return FontSource::System;
    }
    if (name == "named") {
        return FontSource::Named;
    }
    return FontSource::Bundled;
}

Density density_from(std::string_view name) noexcept {
    if (name == "compact") {
        return Density::Compact;
    }
    if (name == "comfortable") {
        return Density::Comfortable;
    }
    return Density::Cozy;
}

std::string serialize_appearance(const AppearanceSettings& settings) {
    const AppearanceSettings s = clamp_appearance(settings);
    std::string out;
    out.reserve(512);
    append_key(out, "version", std::to_string(AppearanceSettings::current_version));
    append_key(out, "palette", s.palette);
    append_key(out, "accent_preset", s.accent_preset);
    append_key(out, "profile", to_string(s.profile));
    append_key(out, "font_source", to_string(s.font_source));
    append_key(out, "font_family", s.font_family);
    append_key(out, "density", to_string(s.density));
    append_key(out, "scale", s.scale);
    append_key(out, "glass_opacity", s.glass_opacity);
    append_key(out, "surface_opacity", s.surface_opacity);
    append_key(out, "custom_bloom_core", s.custom.bloom_core);
    append_key(out, "custom_bloom_wide", s.custom.bloom_wide);
    append_key(out, "custom_scanline", s.custom.scanline);
    append_key(out, "custom_vignette", s.custom.vignette);
    append_key(out, "custom_persistence", s.custom.persistence);
    append_key(out, "custom_deep_field", s.custom.deep_field);
    append_key(out, "custom_text_lift", s.custom.text_lift);
    append_key(out, "reduced_motion", s.reduced_motion);
    append_key(out, "high_contrast", s.high_contrast);
    append_key(out, "places_count", std::to_string(s.places.size()));
    for (const NavigationPlace& place : s.places) {
        append_key(out, "place",
                   encode_setting_value(place.label) + '|' + encode_setting_value(place.path));
    }
    append_key(out, "recent_count", std::to_string(s.recent_destinations.size()));
    for (const std::string& path : s.recent_destinations) {
        append_key(out, "recent", encode_setting_value(path));
    }
    append_key(out, "dual_pane_enabled", s.dual_pane_enabled);
    append_key(out, "split_ratio", s.split_ratio);
    return out;
}

AppearanceSettings parse_appearance(std::string_view text) {
    AppearanceSettings s;
    std::optional<std::size_t> expected_places;
    std::optional<std::size_t> expected_recents;
    std::vector<NavigationPlace> parsed_places;
    std::vector<std::string> parsed_recents;
    std::string_view rest = text;
    while (!rest.empty()) {
        const size_t newline = rest.find('\n');
        std::string_view line = newline == std::string_view::npos ? rest : rest.substr(0, newline);
        rest = newline == std::string_view::npos ? std::string_view() : rest.substr(newline + 1);

        line = trimmed(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        const std::string_view key = trimmed(line.substr(0, equals));
        const std::string_view value = trimmed(line.substr(equals + 1));
        apply_accent_preset(s, key, value);

        if (key == "palette" && !value.empty()) {
            s.palette = std::string(value);
        } else if (key == "profile") {
            s.profile = effect_profile_from(value);
        } else if (key == "font_source") {
            s.font_source = font_source_from(value);
        } else if (key == "font_family") {
            s.font_family = std::string(value);
        } else if (key == "density") {
            s.density = density_from(value);
        } else if (key == "scale") {
            s.scale = parse_double(value, s.scale);
        } else if (key == "glass_opacity") {
            s.glass_opacity = parse_double(value, s.glass_opacity);
        } else if (key == "surface_opacity") {
            s.surface_opacity = parse_double(value, s.surface_opacity);
        } else if (key == "custom_bloom_core") {
            s.custom.bloom_core = parse_double(value, s.custom.bloom_core);
        } else if (key == "custom_bloom_wide") {
            s.custom.bloom_wide = parse_double(value, s.custom.bloom_wide);
        } else if (key == "custom_scanline") {
            s.custom.scanline = parse_double(value, s.custom.scanline);
        } else if (key == "custom_vignette") {
            s.custom.vignette = parse_double(value, s.custom.vignette);
        } else if (key == "custom_persistence") {
            s.custom.persistence = parse_double(value, s.custom.persistence);
        } else if (key == "custom_deep_field") {
            s.custom.deep_field = parse_double(value, s.custom.deep_field);
        } else if (key == "custom_text_lift") {
            s.custom.text_lift = parse_double(value, s.custom.text_lift);
        } else if (key == "reduced_motion") {
            s.reduced_motion = parse_bool(value, s.reduced_motion);
        } else if (key == "high_contrast") {
            s.high_contrast = parse_bool(value, s.high_contrast);
        } else if (key == "places_count") {
            expected_places = parse_size(value);
        } else if (key == "place" && parsed_places.size() < AppearanceSettings::maximum_places) {
            if (const std::optional<NavigationPlace> place = parse_place(value)) {
                parsed_places.push_back(*place);
            }
        } else if (key == "recent_count") {
            expected_recents = parse_size(value);
        } else if (key == "recent" &&
                   parsed_recents.size() < AppearanceSettings::maximum_recent_destinations) {
            if (const std::optional<std::string> path = decode_setting_value(value)) {
                parsed_recents.push_back(*path);
            }
        } else if (key == "dual_pane_enabled") {
            s.dual_pane_enabled = parse_bool(value, s.dual_pane_enabled);
        } else if (key == "split_ratio") {
            s.split_ratio = parse_double(value, s.split_ratio);
        }
        // The version key and unknown keys fall through: tolerated, unused.
    }
    if (expected_places && *expected_places <= AppearanceSettings::maximum_places &&
        parsed_places.size() == *expected_places) {
        s.places = std::move(parsed_places);
    }
    if (expected_recents && *expected_recents <= AppearanceSettings::maximum_recent_destinations &&
        parsed_recents.size() == *expected_recents) {
        s.recent_destinations = std::move(parsed_recents);
    }
    return clamp_appearance(s);
}

AppearanceSettings load_appearance(const std::filesystem::path& path, std::error_code& ec) {
    ec.clear();
    std::error_code exists_ec;
    if (!std::filesystem::exists(path, exists_ec)) {
        // First run: no file yet, shipped defaults, not an error.
        return AppearanceSettings{};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ec = std::make_error_code(std::errc::io_error);
        return AppearanceSettings{};
    }
    std::ostringstream contents;
    contents << in.rdbuf();
    if (in.bad()) {
        ec = std::make_error_code(std::errc::io_error);
        return AppearanceSettings{};
    }
    return parse_appearance(contents.str());
}

void save_appearance(const std::filesystem::path& path, const AppearanceSettings& settings,
                     std::error_code& ec) {
    ec.clear();
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return;
        }
    }
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            ec = std::make_error_code(std::errc::io_error);
            return;
        }
        const std::string text = serialize_appearance(settings);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            ec = std::make_error_code(std::errc::io_error);
            return;
        }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::error_code remove_ec;
        std::filesystem::remove(temp, remove_ec);
    }
}

} // namespace odysea::core
