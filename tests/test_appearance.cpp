// Headless tests for the appearance settings model.
//
// Persistence tests run inside a temporary directory, so nothing touches a
// real configuration file.
#include "odysea/core/appearance.hpp"

#include "test_support.hpp"

#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
using odysea::test::check;
using namespace odysea::core;

namespace {

void test_defaults_are_the_shipped_configuration() {
    const AppearanceSettings s;
    check(s.palette == "odyssey-default", "the shipped palette is odyssey-default");
    check(s.accent_preset == "tideglass", "the shipped accent preset is tideglass");
    check(s.profile == EffectProfile::Balanced, "the shipped profile is balanced");
    check(s.font_source == FontSource::Bundled, "the shipped font source is the bundled face");
    check(s.density == Density::Cozy, "the shipped density is cozy");
    check(s.scale == 1.0, "the shipped scale is 1.0");
    check(s.glass_opacity == 1.0, "the shipped ground is opaque");
    check(!s.reduced_motion && !s.high_contrast, "accessibility overrides start off");
    check(s.custom == effect_profile_levels(EffectProfile::Balanced),
          "a fresh custom profile is seeded from balanced");
    check(s.places ==
              std::vector<NavigationPlace>{NavigationPlace{.label = "Filesystem", .path = "/"}},
          "the filesystem root is the only shipped Place");
    check(s.recent_destinations.empty(), "recent destinations start empty");
    check(!s.dual_pane_enabled && s.split_ratio == 0.5,
          "the workspace starts as one evenly divisible pane");
}

void test_profile_presets_are_distinct_and_ordered() {
    const EffectLevels off = effect_profile_levels(EffectProfile::Off);
    const EffectLevels minimal = effect_profile_levels(EffectProfile::Minimal);
    const EffectLevels balanced = effect_profile_levels(EffectProfile::Balanced);
    const EffectLevels strong = effect_profile_levels(EffectProfile::Strong);

    check(off == EffectLevels{}, "off is the absence of every effect");
    check(minimal != off, "minimal is not merely off");
    check(minimal.bloom_core == 0.0 && minimal.scanline == 0.0 && minimal.persistence == 0.0,
          "minimal keeps the picture still");
    check(minimal.deep_field > 0.0, "minimal retains ground-material depth");
    check(balanced.bloom_core > 0.0 && balanced.scanline > 0.0, "balanced renders visible effects");
    check(strong.bloom_core > balanced.bloom_core && strong.bloom_wide > balanced.bloom_wide &&
              strong.scanline > balanced.scanline && strong.vignette > balanced.vignette,
          "strong exceeds balanced on every emissive gain");
    check(effect_profile_levels(EffectProfile::Custom) == balanced,
          "the custom seed equals balanced");
}

void test_effect_levels_clamp_to_their_documented_ranges() {
    EffectLevels wild;
    wild.bloom_core = 9.0;
    wild.bloom_wide = -1.0;
    wild.scanline = 1.0;
    wild.vignette = 2.0;
    wild.persistence = -0.5;
    wild.deep_field = 3.0;
    wild.text_lift = 9.0;
    const EffectLevels c = clamp_effect_levels(wild);
    check(c.bloom_core == 0.8, "bloom core clamps to 0.8");
    check(c.bloom_wide == 0.0, "bloom wide clamps up to zero");
    check(c.scanline == 0.35, "scanline clamps to 0.35");
    check(c.vignette == 0.45, "vignette clamps to 0.45");
    check(c.persistence == 0.0, "persistence clamps up to zero");
    check(c.deep_field == 1.0, "deep field clamps to 1.0");
    check(c.text_lift == 1.5, "text lift clamps to 1.5");
}

void test_settings_clamp_scale_and_material() {
    AppearanceSettings s;
    s.scale = 10.0;
    s.glass_opacity = 0.0;
    s.surface_opacity = -1.0;
    s.split_ratio = 0.95;
    const AppearanceSettings c = clamp_appearance(s);
    check(c.scale == 2.0, "scale clamps to 2.0");
    check(c.glass_opacity == 0.2, "the ground never becomes fully transparent");
    check(c.surface_opacity == 0.45, "surface blend keeps functional surfaces on a readable bed");
    check(c.split_ratio == 0.75, "the stored pane divider keeps both panes usable");
}

void test_effective_levels_resolve_the_profile() {
    AppearanceSettings s;
    s.profile = EffectProfile::Strong;
    check(effective_effect_levels(s) == effect_profile_levels(EffectProfile::Strong),
          "a preset profile renders its preset");

    s.profile = EffectProfile::Custom;
    s.custom.bloom_core = 0.2;
    s.custom.scanline = 0.05;
    const EffectLevels custom = effective_effect_levels(s);
    check(custom.bloom_core == 0.2 && custom.scanline == 0.05,
          "the custom profile renders the stored levels");
}

void test_accessibility_overrides_shape_the_effective_levels() {
    AppearanceSettings s;
    s.profile = EffectProfile::Strong;
    s.reduced_motion = true;
    const EffectLevels reduced = effective_effect_levels(s);
    const EffectLevels strong = effect_profile_levels(EffectProfile::Strong);
    check(reduced.bloom_core == strong.bloom_core && reduced.bloom_wide == strong.bloom_wide &&
              reduced.scanline == strong.scanline && reduced.vignette == strong.vignette &&
              reduced.persistence == 0.0,
          "reduced motion removes temporal persistence without dimming static effects");

    s.reduced_motion = false;
    s.high_contrast = true;
    const EffectLevels hc = effective_effect_levels(s);
    check(hc.bloom_core == 0.0 && hc.bloom_wide == 0.0 && hc.scanline == 0.0 &&
              hc.vignette == 0.0 && hc.persistence == 0.0,
          "high contrast removes every emissive and motion source");
    check(hc.text_lift == 1.0, "high contrast leaves text brightness unmodified");
}

void test_serialization_round_trips() {
    AppearanceSettings s;
    s.palette = "odyssey-aurora";
    s.accent_preset = "beacon";
    s.profile = EffectProfile::Custom;
    s.font_source = FontSource::Named;
    s.font_family = "Example Mono";
    s.density = Density::Comfortable;
    s.scale = 1.25;
    s.glass_opacity = 0.6;
    s.surface_opacity = 0.85;
    s.custom.bloom_core = 0.3;
    s.custom.bloom_wide = 0.7;
    s.custom.scanline = 0.2;
    s.custom.vignette = 0.1;
    s.custom.persistence = 0.5;
    s.custom.deep_field = 0.25;
    s.custom.text_lift = 1.4;
    s.reduced_motion = true;
    s.high_contrast = true;
    s.places = {NavigationPlace{.label = "Projects | active", .path = "/synthetic/projects"},
                NavigationPlace{.label = "Archive % 2026", .path = "/synthetic/archive 2026"}};
    s.recent_destinations = {"/synthetic/projects/alpha", "/synthetic/archive 2026"};
    s.dual_pane_enabled = true;
    s.split_ratio = 0.625;

    const AppearanceSettings back = parse_appearance(serialize_appearance(s));
    check(back == s, "a serialized settings value parses back identically");
}

void test_navigation_settings_are_bounded_and_tolerant() {
    AppearanceSettings settings;
    settings.places.clear();
    for (std::size_t index = 0; index < AppearanceSettings::maximum_places + 4; ++index) {
        settings.places.push_back(
            NavigationPlace{.label = "Place " + std::to_string(index),
                            .path = "/synthetic/place-" + std::to_string(index)});
    }
    settings.places.push_back(NavigationPlace{.label = "Duplicate", .path = "/synthetic/place-0"});
    settings.places.push_back(NavigationPlace{.label = "Relative", .path = "relative/place"});
    settings.recent_destinations = {"/synthetic/recent", "/synthetic/recent", "relative"};

    const AppearanceSettings clean = clamp_appearance(settings);
    check(clean.places.size() == AppearanceSettings::maximum_places,
          "Places clamp to their documented bound");
    check(clean.recent_destinations == std::vector<std::string>{"/synthetic/recent"},
          "recent destinations reject relative and duplicate paths");

    const AppearanceSettings damaged =
        parse_appearance("version=999\nplaces_count=2\nplace=bad%escape\n"
                         "recent_count=1\nrecent=%ZZ\n");
    check(damaged.places == AppearanceSettings{}.places,
          "a damaged Places payload falls back to shipped defaults");
    check(damaged.recent_destinations.empty(),
          "a damaged recent payload falls back to an empty list");

    const AppearanceSettings intentionally_empty =
        parse_appearance("version=3\nplaces_count=0\nrecent_count=0\n");
    check(intentionally_empty.places.empty(), "an explicitly empty Places list stays empty");
}

void test_parsing_tolerates_damage_and_the_future() {
    check(parse_appearance("") == AppearanceSettings{}, "empty text yields the defaults");
    check(parse_appearance("complete nonsense\n\x01\x02") == AppearanceSettings{},
          "unparseable text yields the defaults");

    const AppearanceSettings future = parse_appearance("version=999\n"
                                                       "palette=odyssey-amber\n"
                                                       "accent_preset=ember\n"
                                                       "profile=someday-profile\n"
                                                       "brand_new_key=brand new value\n"
                                                       "scale=not-a-number\n"
                                                       "# a comment line\n"
                                                       "  density = comfortable \n");
    check(future.palette == "odyssey-amber", "known keys load from a newer file");
    check(future.accent_preset == "ember", "known accent presets load from a newer file");
    check(future.profile == EffectProfile::Balanced,
          "an unknown profile name falls back to balanced");
    check(future.scale == 1.0, "a malformed number keeps its default");
    check(future.density == Density::Comfortable, "keys and values may carry spaces");

    const AppearanceSettings hot = parse_appearance("custom_scanline=7.0\nscale=0.01\n");
    check(hot.custom.scanline == 0.35, "loaded levels clamp to their ranges");
    check(hot.scale == 0.75, "loaded scale clamps to its range");

    const AppearanceSettings priorSurface = parse_appearance("surface_opacity=0\n");
    check(priorSurface.surface_opacity == 0.45,
          "a stored transparent surface becomes the opaque readable blend");
}

void test_non_finite_numbers_never_survive() {
    // Parsed: std::from_chars accepts nan/inf spellings, but a NaN defeats
    // every clamp comparison, so the parser counts them as malformed and the
    // field keeps its default.
    const AppearanceSettings poisoned = parse_appearance("scale=nan\n"
                                                         "glass_opacity=-nan\n"
                                                         "surface_opacity=inf\n"
                                                         "custom_bloom_core=nan\n"
                                                         "custom_text_lift=-inf\n"
                                                         "custom_persistence=infinity\n");
    check(poisoned == AppearanceSettings{}, "non-finite persisted numbers keep their defaults");
    check(std::isfinite(poisoned.scale) && poisoned.scale == 1.0,
          "a nan scale never reaches the geometry");

    // Constructed in memory: the clamp helpers pin a non-finite value to the
    // low bound of its range instead of letting it through.
    AppearanceSettings s;
    s.scale = std::numeric_limits<double>::quiet_NaN();
    s.glass_opacity = -std::numeric_limits<double>::infinity();
    s.custom.bloom_core = std::numeric_limits<double>::quiet_NaN();
    s.custom.text_lift = std::numeric_limits<double>::infinity() * -1.0;
    const AppearanceSettings c = clamp_appearance(s);
    check(c.scale == 0.75, "a nan scale clamps to the low bound");
    check(c.glass_opacity == 0.2, "a -inf opacity clamps to the low bound");
    check(c.custom.bloom_core == 0.0, "a nan effect level clamps to zero");
    check(c.custom.text_lift == 1.0, "a non-finite text lift clamps to its floor");

    // Serialized: the written form is clamped first, so a poisoned in-memory
    // value cannot round-trip back in through the file.
    const std::string text = serialize_appearance(s);
    check(text.find("nan") == std::string::npos && text.find("inf") == std::string::npos,
          "a serialized settings value never carries a non-finite number");
    check(parse_appearance(text) == c, "the clamped form round-trips identically");
}

void test_stored_strings_cannot_inject_keys() {
    // The persisted form is line-oriented with no escaping and the last
    // occurrence of a key wins, so a newline inside a stored string could
    // rewrite any key serialized after it. Control characters are removed.
    AppearanceSettings s;
    s.font_source = FontSource::Named;
    s.font_family = "Fake Family\nprofile=strong\npalette=odyssey-amber";
    const AppearanceSettings back = parse_appearance(serialize_appearance(s));
    check(back.profile == EffectProfile::Balanced, "a font family cannot rewrite the profile");
    check(back.palette == "odyssey-default", "a font family cannot rewrite the palette");
    check(back.font_family == "Fake Familyprofile=strongpalette=odyssey-amber",
          "control characters are removed from the stored family");
    check(parse_appearance(serialize_appearance(back)) == back, "the sanitized form is stable");

    AppearanceSettings p;
    p.palette = "odyssey-default\r\nhigh_contrast=true";
    const AppearanceSettings palette_back = parse_appearance(serialize_appearance(p));
    check(!palette_back.high_contrast, "a palette identifier cannot inject keys");

    AppearanceSettings accent;
    accent.accent_preset = "tideglass\r\nhigh_contrast=true";
    const AppearanceSettings accent_back = parse_appearance(serialize_appearance(accent));
    check(!accent_back.high_contrast, "an accent preset identifier cannot inject keys");
}

void test_enum_names_round_trip() {
    for (const EffectProfile p :
         {EffectProfile::Off, EffectProfile::Minimal, EffectProfile::Balanced,
          EffectProfile::Strong, EffectProfile::Custom}) {
        check(effect_profile_from(to_string(p)) == p, "profile names round-trip");
    }
    for (const FontSource f : {FontSource::Bundled, FontSource::System, FontSource::Named}) {
        check(font_source_from(to_string(f)) == f, "font source names round-trip");
    }
    for (const Density d : {Density::Compact, Density::Cozy, Density::Comfortable}) {
        check(density_from(to_string(d)) == d, "density names round-trip");
    }
}

void test_load_and_save(const fs::path& dir) {
    std::error_code ec;

    const fs::path missing = dir / "never-written.conf";
    const AppearanceSettings first_run = load_appearance(missing, ec);
    check(!ec, "a missing settings file is not an error");
    check(first_run == AppearanceSettings{}, "a missing settings file yields the defaults");

    AppearanceSettings s;
    s.palette = "odyssey-graphite";
    s.profile = EffectProfile::Minimal;
    s.scale = 1.5;

    const fs::path nested = dir / "config" / "sub" / "appearance.conf";
    save_appearance(nested, s, ec);
    check(!ec, "saving creates missing parent directories");
    check(fs::exists(nested), "the settings file exists after a save");
    check(!fs::exists(nested.parent_path() / "appearance.conf.tmp"),
          "no temporary file survives a successful save");

    const AppearanceSettings loaded = load_appearance(nested, ec);
    check(!ec, "a saved settings file loads");
    check(loaded == s, "a saved settings value loads back identically");

    s.profile = EffectProfile::Strong;
    save_appearance(nested, s, ec);
    check(!ec, "overwriting an existing settings file succeeds");
    check(load_appearance(nested, ec).profile == EffectProfile::Strong,
          "an overwrite replaces the stored state");
}

} // namespace

int main() {
    const odysea::test::TemporaryTree tree("appearance");

    test_defaults_are_the_shipped_configuration();
    test_profile_presets_are_distinct_and_ordered();
    test_effect_levels_clamp_to_their_documented_ranges();
    test_settings_clamp_scale_and_material();
    test_effective_levels_resolve_the_profile();
    test_accessibility_overrides_shape_the_effective_levels();
    test_serialization_round_trips();
    test_navigation_settings_are_bounded_and_tolerant();
    test_parsing_tolerates_damage_and_the_future();
    test_non_finite_numbers_never_survive();
    test_stored_strings_cannot_inject_keys();
    test_enum_names_round_trip();
    test_load_and_save(tree.root());

    return odysea::test::report("core appearance settings");
}
