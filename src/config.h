#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <filesystem>
#include <climits>
#include <nlohmann/json.hpp>
#include "wlroots.h"

struct OutputEntry {
    int width = 0;
    int height = 0;
    double refresh = 0; // Hz, 0 = preferred/auto
    double scale = 0;   // 0 = auto (DPI)
    int x = INT_MIN;
    int y = INT_MIN;
    bool enabled = true;
    std::optional<double> dpi = std::nullopt;
};

struct DefaultOutput {
    int width = 1280;
    int height = 720;
    double refresh = 60.0;
    double scale = 1.0;
};

struct KeyboardConfig {
    std::string layouts = "us";
    std::string variant = "";
    std::string options = "";
    int repeat_rate = 25;
    int repeat_delay = 600;
};

struct MouseConfig {
    std::string accel_profile = "adaptive"; // flat, adaptive, none
    double speed = 0.0; // -1.0 .. 1.0
    bool natural_scroll = false;
    double scroll_factor = 1.0;
    bool tap_enabled = true;
    std::string tap_button_map = "lrm"; // lrm, lmr
    bool middle_emulation = false;
    bool left_handed = false;
    double dpi = 0; // 0 = auto
};

struct BspConfig {
    double split_ratio = 0.5; // default 50/50
    bool opposite_orientation = true; // new branch opposite of parent if true
    bool keep_ratio_on_drop = true; // keep old ratio when reinserting dragged window
    double min_ratio = 0.1;
    double max_ratio = 0.9;
};

struct GradientConfigData {
    bool enabled = false;
    std::vector<std::string> colors; // hex "#rrggbb"
    double angle = 0.0;
    bool animate = false;
};

struct BorderConfigData {
    bool enabled = true;
    int width = 2;
    int radius = 8;
    std::string active_color = "#ff5500";
    std::string inactive_color = "#3a3a3a";
    GradientConfigData gradient;
    bool animate = true;
    int animation_duration = 200;
    std::string animation_easing = "easeOutCubic";
};

struct TitleBarConfigData {
    bool enabled = false;
    int height = 28;
    std::string color = "#222222";
    std::string text_color = "#eeeeee";
    int font_size = 11;
    bool show_title = true;
    bool show_buttons = true;
};

struct DecorationConfigData {
    BorderConfigData border;
    TitleBarConfigData titlebar;
    int outer_gap = 0;
    int inner_gap = 0;
};

struct AnimationPreset {
    bool enabled = true;
    int duration = 250;
    std::string easing = "easeOutCubic";
};

enum class AnimationStyle {
    Fade,
    ScaleIn,
    ScaleOut,
    SlideTop,
    SlideBottom,
    SlideLeft,
    SlideRight,
    Pop,
    SlideFade,
    Slide, // generic slide for tilingMove
    Cube,
    FadeWindowLayer, // workspace special: fade window layer only
};

std::string toString(AnimationStyle s);
AnimationStyle styleFromString(const std::string &s, AnimationStyle fallback = AnimationStyle::Fade);

struct AnimDef {
    bool enabled = true;
    AnimationStyle style = AnimationStyle::Fade;
    int duration = 250;
    std::string easing = "easeOutCubic";
    int endPercent = 0; // 0-100 percentage for end opacity/size, default 0 = fully closed
};

struct AnimPair {
    AnimDef start;
    std::optional<AnimDef> end; // nullopt = no reverse; reversible triggers must have value
    bool hasEnd() const { return end.has_value(); }
};

struct AnimationsConfig {
    bool enabled = true;
    double speed = 1.0; // global animation speed multiplier
    bool windowLayerOnly = true; // workspace switch affects only window layer when true
    std::unordered_map<std::string, AnimPair> pairs; // key = "window","popup","tilingMove","workspaceSwitch","fullscreen","maximize","floating","focus","layer"
    // legacy presets migrated into pairs
    std::unordered_map<std::string, AnimationPreset> presets;

    const AnimPair* pairFor(const std::string &id) const {
        auto it = pairs.find(id);
        if (it != pairs.end()
            ) return &it->second;
        return nullptr;
    }
    AnimPair* pairFor(const std::string &id) {
        auto it = pairs.find(id);
        if (it != pairs.end()
            ) return &it->second;
        return nullptr;
    }
    // legacy helpers (kept for compat)
    bool isEnabled(const std::string &id) const {
        if (auto *p = pairFor(id)
            ) return p->start.enabled;
        auto it = presets.find(id);
        if (it != presets.end()
            ) return it->second.enabled;
        return true;
    }
    int durationFor(const std::string &id, int fallback) const {
        if (auto *p = pairFor(id)) {
            if (p->start.duration >= 0)
                return p->start.duration;
        }
        auto it = presets.find(id);
        if (it != presets.end()
            && it->second.duration >= 0) return it->second.duration;
        return fallback;
    }
    std::string easingFor(const std::string &id, const std::string &fallback) const {
        if (auto *p = pairFor(id)) {
            if (!p->start.easing.empty()
                ) return p->start.easing;
        }
        auto it = presets.find(id);
        if (it != presets.end()
            && !it->second.easing.empty()) return it->second.easing;
        return fallback;
    }
    // new helpers for paired model
    int durationFor(const std::string &id, bool isStart, int fallback) const {
        if (auto *p = pairFor(id)) {
            const AnimDef &d = isStart ? p->start : (p->end ? *p->end : p->start);
            if (d.duration >= 0)
                return d.duration;
        }
        return durationFor(id, fallback);
    }
    std::string easingFor(const std::string &id, bool isStart, const std::string &fallback) const {
        if (auto *p = pairFor(id)) {
            const AnimDef &d = isStart ? p->start : (p->end ? *p->end : p->start);
            if (!d.easing.empty()
                ) return d.easing;
        }
        return easingFor(id, fallback);
    }
    bool hasEndFor(const std::string &id) const {
        if (auto *p = pairFor(id)
            ) return p->hasEnd();
        return false;
    }
};

struct Keybind {
    std::vector<std::string> mods; // e.g. ["Alt","Ctrl"]
    std::string key;               // e.g. "Escape", "F1", "a"
    std::string action;            // quit, focus_prev, focus_next, toggle_layout, new_workspace, goto_workspace
    std::string arg;               // optional argument (workspace id etc)

    uint32_t modsMask = 0; // parsed
    xkb_keysym_t keysym = XKB_KEY_NoSymbol;

    bool matches(uint32_t modsMask_, xkb_keysym_t sym) const {
        return modsMask == modsMask_ && keysym == sym;
    }
};

class Config {
public:
    Config();

    static std::filesystem::path defaultPath();
    static std::string embeddedDefaultJson();
    static std::filesystem::path templatePath() {
        return "config.template.json";
    }
    bool load(const std::filesystem::path &path = {});
    bool save(const std::filesystem::path &path = {}) const;
    void loadOrCreateDefault();

    // Output
    static std::string outputId(struct wlr_output *output);
    OutputEntry getOutputConfig(struct wlr_output *output) const;
    DefaultOutput defaultOutput;
    std::unordered_map<std::string, OutputEntry> monitors; // persistent id -> config

    // Helpers
    double detectDpi(struct wlr_output *output) const;
    double getOutputScale(struct wlr_output *output, const OutputEntry &entry) const;
    void setOutputConfig(const std::string &id, const OutputEntry &entry);

    // Modkey - abstracted modifier for keybinds ("Alt", "Super", "Ctrl", etc). Keybinds should use "Mod".
    // When running windowed (nested Wayland/X11), Super is occupied by parent compositor, so effectiveMod() falls back to Alt.
    std::string modkey = "Alt";
    std::string effectiveMod() const;
    bool isWindowedMode() const;
    static void captureOriginalDisplay(); // call early in main()

    // Input
    KeyboardConfig keyboard;
    MouseConfig mouse;

    // Layout (BSP)
    BspConfig bsp;

    // Decorations & Animations
    DecorationConfigData decorations;
    AnimationsConfig animations;

    // Keybinds
    std::vector<Keybind> keybinds;
    const Keybind* findKeybind(uint32_t mods, xkb_keysym_t sym) const;

    // Raw json for unknown fields preservation
    nlohmann::json rawJson;

private:
    std::filesystem::path loadedPath;
    void ensureDefaults();
    void parseKeybinds();
    static uint32_t parseMods(const std::vector<std::string> &mods);
    static xkb_keysym_t parseKeysym(const std::string &key);
    static std::string s_originalWaylandDisplay;
    static std::string s_originalDisplay;
};

// nlohmann json adapters
void to_json(nlohmann::json &j, const OutputEntry &o);
void from_json(const nlohmann::json &j, OutputEntry &o);
void to_json(nlohmann::json &j, const DefaultOutput &o);
void from_json(const nlohmann::json &j, DefaultOutput &o);
void to_json(nlohmann::json &j, const KeyboardConfig &k);
void from_json(const nlohmann::json &j, KeyboardConfig &k);
void to_json(nlohmann::json &j, const MouseConfig &m);
void from_json(const nlohmann::json &j, MouseConfig &m);
void to_json(nlohmann::json &j, const Keybind &k);
void from_json(const nlohmann::json &j, Keybind &k);
void to_json(nlohmann::json &j, const BspConfig &b);
void from_json(const nlohmann::json &j, BspConfig &b);
void to_json(nlohmann::json &j, const GradientConfigData &g);
void from_json(const nlohmann::json &j, GradientConfigData &g);
void to_json(nlohmann::json &j, const BorderConfigData &b);
void from_json(const nlohmann::json &j, BorderConfigData &b);
void to_json(nlohmann::json &j, const TitleBarConfigData &t);
void from_json(const nlohmann::json &j, TitleBarConfigData &t);
void to_json(nlohmann::json &j, const DecorationConfigData &d);
void from_json(const nlohmann::json &j, DecorationConfigData &d);
void to_json(nlohmann::json &j, const AnimationPreset &a);
void from_json(const nlohmann::json &j, AnimationPreset &a);
void to_json(nlohmann::json &j, const AnimDef &a);
void from_json(const nlohmann::json &j, AnimDef &a);
void to_json(nlohmann::json &j, const AnimPair &a);
void from_json(const nlohmann::json &j, AnimPair &a);
void to_json(nlohmann::json &j, const AnimationsConfig &a);
void from_json(const nlohmann::json &j, AnimationsConfig &a);
std::vector<std::string> validateAnimationsConfig(const AnimationsConfig &a);
