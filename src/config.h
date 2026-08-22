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

    // Input
    KeyboardConfig keyboard;
    MouseConfig mouse;

    // Layout (BSP)
    BspConfig bsp;

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
