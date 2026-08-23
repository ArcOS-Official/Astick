#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <filesystem>
#include <climits>
#include <QString>
#include <QHash>
#include <QList>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <nlohmann/json.hpp>
#include "wlroots.h"

struct OutputEntry {
    int width = 0;
    int height = 0;
    double refresh = 0;
    double scale = 0;
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
    std::string accel_profile = "adaptive";
    double speed = 0.0;
    bool natural_scroll = false;
    double scroll_factor = 1.0;
    bool tap_enabled = true;
    std::string tap_button_map = "lrm";
    bool middle_emulation = false;
    bool left_handed = false;
    double dpi = 0;
};

struct BspConfig {
    double split_ratio = 0.5;
    bool opposite_orientation = true;
    bool keep_ratio_on_drop = true;
    double min_ratio = 0.1;
    double max_ratio = 0.9;
};

struct GradientConfigData {
    bool enabled = false;
    std::vector<std::string> colors;
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
    Slide,
    Cube,
    FadeWindowLayer,
};

std::string toString(AnimationStyle s);
AnimationStyle styleFromString(const std::string &s, AnimationStyle fallback = AnimationStyle::Fade);

struct AnimDef {
    bool enabled = true;
    AnimationStyle style = AnimationStyle::Fade;
    int duration = 250;
    std::string easing = "easeOutCubic";
    int endPercent = 0;
};

struct AnimPair {
    AnimDef start;
    std::optional<AnimDef> end;
    bool hasEnd() const;
};

struct AnimationsConfig {
    bool enabled = true;
    double speed = 1.0;
    bool windowLayerOnly = true;
    std::unordered_map<std::string, AnimPair> pairs;
    std::unordered_map<std::string, AnimationPreset> presets;

    const AnimPair* pairFor(const std::string &id) const;
    AnimPair* pairFor(const std::string &id);
    bool isEnabled(const std::string &id) const;
    int durationFor(const std::string &id, int fallback) const;
    std::string easingFor(const std::string &id, const std::string &fallback) const;
    int durationFor(const std::string &id, bool isStart, int fallback) const;
    std::string easingFor(const std::string &id, bool isStart, const std::string &fallback) const;
    bool hasEndFor(const std::string &id) const;
};

struct Keybind {
    std::vector<std::string> mods;
    std::string key;
    std::string action;
    std::string arg;

    uint32_t modsMask = 0;
    xkb_keysym_t keysym = XKB_KEY_NoSymbol;

    bool matches(uint32_t modsMask_, xkb_keysym_t sym) const;
};

class Config {
public:
    Config();

    static QString defaultPath();
    static std::filesystem::path defaultPathFs();
    static std::string embeddedDefaultJson();
    static QString templatePath() { return QStringLiteral("config.template.json"); }
    bool load(const QString &path = {});
    bool load(const std::filesystem::path &path);
    bool save(const QString &path = {}) const;
    bool save(const std::filesystem::path &path) const;
    void loadOrCreateDefault();

    static std::string outputId(struct wlr_output *output);
    static QString outputIdQ(struct wlr_output *output);
    OutputEntry getOutputConfig(struct wlr_output *output) const;
    DefaultOutput defaultOutput;
    QHash<QString, OutputEntry> monitors;

    double detectDpi(struct wlr_output *output) const;
    double getOutputScale(struct wlr_output *output, const OutputEntry &entry) const;
    void setOutputConfig(const QString &id, const OutputEntry &entry);
    void setOutputConfig(const std::string &id, const OutputEntry &entry);

    std::string modkey = "Alt";
    std::string effectiveMod() const;
    bool isWindowedMode() const;
    static void captureOriginalDisplay();

    KeyboardConfig keyboard;
    MouseConfig mouse;

    BspConfig bsp;

    DecorationConfigData decorations;
    AnimationsConfig animations;

    QList<Keybind> keybinds;
    const Keybind* findKeybind(uint32_t mods, xkb_keysym_t sym) const;

    nlohmann::json rawJson;

private:
    QString loadedPath;
    void ensureDefaults();
    void parseKeybinds();
    static uint32_t parseMods(const std::vector<std::string> &mods);
    static xkb_keysym_t parseKeysym(const std::string &key);
    static std::string s_originalWaylandDisplay;
    static std::string s_originalDisplay;
};

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
