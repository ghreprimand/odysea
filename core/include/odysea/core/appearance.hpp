// OdySea core: appearance settings model.
//
// Toolkit-agnostic. Everything that decides which appearance state is valid,
// what each effect profile means, and how preferences persist across runs
// lives here, where it can be verified headless. The presentation layer maps
// this state onto colors, fonts, and GPU effects; it never redefines it.
//
// The persisted form is a small versioned key=value text file. Parsing is
// tolerant by design: unknown keys are ignored so newer files degrade cleanly
// on older builds, malformed and non-finite numeric values keep their
// defaults, and out-of-range finite values clamp to their documented bounds
// instead of failing the load.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace odysea::core {

/// How strongly the shell renders its screen-effect layer.
///
/// The first four profiles are fixed presets; `Custom` reads the user's stored
/// effect levels instead. `Balanced` is the shipped default.
enum class EffectProfile { Off, Minimal, Balanced, Strong, Custom };

/// Where the body typeface comes from.
///
/// `Bundled` names the default face the application ships with. `System` asks
/// the platform for its fixed-width face. `Named` uses a family the user typed
/// directly. All three fall back to an available fixed-width family when the
/// requested one is missing, so text always renders with stable metrics.
enum class FontSource { Bundled, System, Named };

/// Vertical spacing of list and grid content.
enum class Density { Compact, Cozy, Comfortable };

/// One resolved set of screen-effect intensities.
///
/// Values are unitless gains in the documented ranges below. The presentation
/// layer treats them as inputs to its effect pipeline; a value of zero always
/// means the corresponding effect is absent.
struct EffectLevels {
    /// Tight glow around bright content, 0 to 0.8.
    double bloom_core = 0.0;
    /// Wide halo around bright content, 0 to 1.0.
    double bloom_wide = 0.0;
    /// Horizontal band modulation depth, 0 to 0.35.
    double scanline = 0.0;
    /// Edge darkening amount, 0 to 0.45.
    double vignette = 0.0;
    /// Trailing-decay gain for moving highlights, 0 to 1.
    double persistence = 0.0;
    /// Depth of the window ground's center-to-edge gradient, 0 to 1. This is
    /// a still material amount, which is why `Minimal` keeps it while zeroing
    /// every emissive and banding gain above.
    double deep_field = 0.0;
    /// Brightness lift applied to chromatic text inks, 1.0 (none) to 1.5.
    double text_lift = 1.0;

    [[nodiscard]] bool operator==(const EffectLevels& other) const noexcept = default;
};

/// The preset effect levels for a fixed profile.
///
/// `Custom` has no preset; asking for it returns the `Balanced` table, which
/// is also the seed a fresh custom profile starts from.
[[nodiscard]] EffectLevels effect_profile_levels(EffectProfile profile) noexcept;

/// Clamps every field of `levels` to its documented range.
[[nodiscard]] EffectLevels clamp_effect_levels(const EffectLevels& levels) noexcept;

/// Everything the user can persist about how the application looks.
///
/// A default-constructed value is the shipped configuration.
struct AppearanceSettings {
    /// Version of the persisted schema this build reads and writes.
    static constexpr int current_version = 1;

    /// Identifier of the active color family. The core does not interpret the
    /// value; the presentation layer resolves unknown identifiers to the
    /// shipped default family.
    std::string palette = "odyssey-default";

    EffectProfile profile = EffectProfile::Balanced;

    FontSource font_source = FontSource::Bundled;
    /// Family for `FontSource::Named`; ignored by the other sources.
    std::string font_family;

    Density density = Density::Cozy;
    /// Interface scale multiplier, 0.75 to 2.0.
    double scale = 1.0;

    /// Opacity of the window ground, 0.2 to 1.0. Text and content surfaces are
    /// exempt; only the material behind them becomes translucent.
    double glass_opacity = 1.0;
    /// Opacity of colored functional surfaces, 0.0 to 1.0.
    double surface_opacity = 1.0;

    /// Stored levels for the `Custom` profile. Kept even while a preset is
    /// active so switching to `Custom` restores the user's last adjustments.
    EffectLevels custom = effect_profile_levels(EffectProfile::Balanced);

    bool reduced_motion = false;
    bool high_contrast = false;

    [[nodiscard]] bool operator==(const AppearanceSettings& other) const noexcept = default;
};

/// Returns `settings` with every numeric field clamped to its documented
/// range — a non-finite value pins to the low bound of its range — control
/// characters removed from the stored strings, and enumerated fields left
/// untouched (they cannot hold invalid values). String sanitization exists
/// because the persisted form is line-oriented with no escaping: a value
/// carrying a newline could otherwise rewrite any later key.
[[nodiscard]] AppearanceSettings clamp_appearance(const AppearanceSettings& settings) noexcept;

/// The effect levels the presentation layer should render for `settings`.
///
/// Resolves the active profile (presets for the fixed profiles, the clamped
/// stored levels for `Custom`) and then applies the accessibility overrides:
/// reduced motion forces `persistence` to zero, and high contrast forces
/// `scanline` and `vignette` to zero and `text_lift` to one so nothing
/// modulates text legibility.
[[nodiscard]] EffectLevels effective_effect_levels(const AppearanceSettings& settings) noexcept;

/// Names for the enumerated fields as persisted, e.g. "balanced".
[[nodiscard]] std::string_view to_string(EffectProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(FontSource source) noexcept;
[[nodiscard]] std::string_view to_string(Density density) noexcept;

/// Parses a persisted enum name. Unrecognized names yield the shipped default
/// for that field, which is what keeps unknown future values loadable.
[[nodiscard]] EffectProfile effect_profile_from(std::string_view name) noexcept;
[[nodiscard]] FontSource font_source_from(std::string_view name) noexcept;
[[nodiscard]] Density density_from(std::string_view name) noexcept;

/// Renders `settings` as the persisted key=value text form. The settings are
/// clamped and sanitized first, so the output never carries a non-finite
/// number or a control character.
[[nodiscard]] std::string serialize_appearance(const AppearanceSettings& settings);

/// Parses the persisted text form.
///
/// Missing keys keep their defaults, unknown keys are ignored (a later save
/// does not preserve them), malformed and non-finite numeric values keep
/// their defaults, and every numeric field is clamped. The version key is
/// recorded for forward compatibility but does not reject the file: a newer
/// writer's known keys still load.
[[nodiscard]] AppearanceSettings parse_appearance(std::string_view text);

/// Reads settings from `path`. A missing file is not an error: it yields the
/// shipped defaults, which is the first-run experience.
[[nodiscard]] AppearanceSettings load_appearance(const std::filesystem::path& path,
                                                 std::error_code& ec);

/// Writes settings to `path`, creating parent directories as needed. The write
/// goes through a sibling temporary file and a rename, so a reader never
/// observes a partially written file. Durability across power loss and
/// coordination between concurrent writers are out of scope: appearance
/// preferences are cheap to re-enter and never worth blocking use over.
void save_appearance(const std::filesystem::path& path, const AppearanceSettings& settings,
                     std::error_code& ec);

} // namespace odysea::core
