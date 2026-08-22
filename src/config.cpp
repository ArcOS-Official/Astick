#include "config.h"
#include <fstream>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <xkbcommon/xkbcommon.h>

namespace fs = std::filesystem;

Config::Config() {
    ensureDefaults();
}

std::filesystem::path Config::defaultPath() {
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    std::string base;
    if (xdg && *xdg) {
        base = xdg;
    } else {
        const char *home = std::getenv("HOME");
        if (home) base = std::string(home) + "/.config";
        else base = ".";
    }
    return fs::path(base) / "Astick" / "config.json";
}

void Config::ensureDefaults() {
    if (keybinds.empty()) {
        keybinds = {
            {{"Alt"}, "Escape", "quit", "", 0, XKB_KEY_NoSymbol},
            {{"Alt"}, "F1", "focus_prev", "", 0, XKB_KEY_NoSymbol},
            {{"Alt"}, "F2", "toggle_layout", "", 0, XKB_KEY_NoSymbol},
            {{"Alt"}, "F3", "new_workspace", "", 0, XKB_KEY_NoSymbol},
            {{"Alt"}, "F4", "goto_workspace", "1", 0, XKB_KEY_NoSymbol},
            {{"Alt"}, "t", "swap_orientation", "", 0, XKB_KEY_NoSymbol},
            {{"Alt"}, "f", "toggle_floating", "", 0, XKB_KEY_NoSymbol},
            {{"Alt"}, "m", "toggle_maximize", "", 0, XKB_KEY_NoSymbol},
            {{}, "F11", "toggle_fullscreen", "", 0, XKB_KEY_NoSymbol},
            {{"Alt"}, "Return", "toggle_fullscreen", "", 0, XKB_KEY_NoSymbol},
        };
        parseKeybinds();
    } else {
        bool hasSwap = false;
        bool hasFloating = false;
        bool hasMaximize = false;
        bool hasFullscreen = false;
        for (auto &k : keybinds) {
            if (k.action == "swap_orientation" || k.action == "toggle_split") hasSwap = true;
            if (k.action == "toggle_floating" || k.action == "toggle_float" || k.action == "floating_toggle") hasFloating = true;
            if (k.action == "toggle_maximize" || k.action == "maximize" || k.action == "toggle_maximized") hasMaximize = true;
            if (k.action == "toggle_fullscreen" || k.action == "fullscreen" || k.action == "toggle_fullscreened") hasFullscreen = true;
        }
        if (!hasSwap) {
            keybinds.push_back({{"Alt"}, "t", "swap_orientation", "", 0, XKB_KEY_NoSymbol});
            parseKeybinds();
        }
        if (!hasFloating) {
            keybinds.push_back({{"Alt"}, "f", "toggle_floating", "", 0, XKB_KEY_NoSymbol});
            parseKeybinds();
        }
        if (!hasMaximize) {
            keybinds.push_back({{"Alt"}, "m", "toggle_maximize", "", 0, XKB_KEY_NoSymbol});
            parseKeybinds();
        }
        if (!hasFullscreen) {
            keybinds.push_back({{}, "F11", "toggle_fullscreen", "", 0, XKB_KEY_NoSymbol});
            keybinds.push_back({{"Alt"}, "Return", "toggle_fullscreen", "", 0, XKB_KEY_NoSymbol});
            parseKeybinds();
        }
    }
}

void Config::parseKeybinds() {
    for (auto &k : keybinds) {
        k.modsMask = parseMods(k.mods);
        k.keysym = parseKeysym(k.key);
    }
}

uint32_t Config::parseMods(const std::vector<std::string> &mods) {
    uint32_t mask = 0;
    for (auto &m : mods) {
        std::string lower = m;
        for (auto &c : lower) c = std::tolower(c);
        if (lower == "shift") mask |= WLR_MODIFIER_SHIFT;
        else if (lower == "ctrl" || lower == "control") mask |= WLR_MODIFIER_CTRL;
        else if (lower == "alt" || lower == "mod1") mask |= WLR_MODIFIER_ALT;
        else if (lower == "super" || lower == "mod4" || lower == "meta" || lower == "logo") mask |= WLR_MODIFIER_LOGO;
        else if (lower == "mod2") mask |= WLR_MODIFIER_MOD2;
        else if (lower == "mod3") mask |= WLR_MODIFIER_MOD3;
        else if (lower == "mod5") mask |= WLR_MODIFIER_MOD5;
    }
    return mask;
}

xkb_keysym_t Config::parseKeysym(const std::string &key) {
    if (key.empty()) return XKB_KEY_NoSymbol;
    xkb_keysym_t sym = xkb_keysym_from_name(key.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
    if (sym == XKB_KEY_NoSymbol && key.size() == 1) {
        // fallback single char
        sym = xkb_keysym_from_name(key.c_str(), XKB_KEYSYM_NO_FLAGS);
    }
    return sym;
}

const Keybind* Config::findKeybind(uint32_t mods, xkb_keysym_t sym) const {
    for (auto &k : keybinds) {
        if (k.matches(mods, sym)) return &k;
    }
    return nullptr;
}

std::string Config::outputId(struct wlr_output *output) {
    if (!output) return "unknown";
    std::string name = output->name ? output->name : "unknown";
    std::string make = output->make ? output->make : "";
    std::string model = output->model ? output->model : "";
    std::string serial = output->serial ? output->serial : "";
    // Persistent ID: name:make:model:serial . If serial empty, use description hash fallback
    std::string id = name;
    if (!make.empty() || !model.empty() || !serial.empty()) {
        id += ":" + make + ":" + model;
        if (!serial.empty()) id += ":" + serial;
    }
    // sanitize: replace spaces and slashes
    for (char &c : id) {
        if (c == '/' || c == ' ') c = '_';
    }
    if (id.empty()) id = "unknown";
    return id;
}

OutputEntry Config::getOutputConfig(struct wlr_output *output) const {
    std::string id = outputId(output);
    auto it = monitors.find(id);
    if (it != monitors.end()) {
        return it->second;
    }
    // not found: return default-derived entry with 0 meaning use preferred
    OutputEntry e;
    e.width = defaultOutput.width;
    e.height = defaultOutput.height;
    e.refresh = defaultOutput.refresh;
    e.scale = defaultOutput.scale;
    e.enabled = true;
    return e;
}

void Config::setOutputConfig(const std::string &id, const OutputEntry &entry) {
    monitors[id] = entry;
}

double Config::detectDpi(struct wlr_output *output) const {
    if (!output) return 96.0;
    // If config has dpi set for this monitor, use it
    std::string id = outputId(output);
    auto it = monitors.find(id);
    if (it != monitors.end() && it->second.dpi && *it->second.dpi > 0) {
        return *it->second.dpi;
    }
    if (mouse.dpi > 0) return mouse.dpi;
    // Auto detect from phys size
    int mmW = output->phys_width;
    int mmH = output->phys_height;
    int pxW = output->width;
    int pxH = output->height;
    if (mmW <= 0 || mmH <= 0 || pxW <=0 || pxH <=0) {
        // try to use current mode size if output width not yet set
        if (output->current_mode) {
            pxW = output->current_mode->width;
            pxH = output->current_mode->height;
        }
        if (mmW <=0 || mmH <=0) return 96.0;
    }
    double diagPx = std::hypot((double)pxW, (double)pxH);
    double diagMm = std::hypot((double)mmW, (double)mmH);
    double diagIn = diagMm / 25.4;
    if (diagIn <= 0) return 96.0;
    double dpi = diagPx / diagIn;
    if (dpi < 30 || dpi > 1000) return 96.0;
    return dpi;
}

double Config::getOutputScale(struct wlr_output *output, const OutputEntry &entry) const {
    if (entry.scale > 0.1) return entry.scale;
    if (entry.dpi && *entry.dpi > 0) return *entry.dpi / 96.0;
    double dpi = detectDpi(output);
    // auto scale: 1.0 for <120 dpi, 1.25 for 120-144, 1.5 for 144-192, 2.0 for >192
    // but if user wants manual, they set scale. For auto, we can compute
    // simple: round(dpi/96)
    // Keep conservative: if dpi < 140 => 1.0, 140-180 =>1.25 etc, but use 1.0 by default to avoid surprise
    // We'll just return 1.0 if scale not set, unless dpi very high
    if (dpi >= 192) return 2.0;
    if (dpi >= 144) return 1.5;
    if (dpi >= 120) return 1.25;
    return 1.0;
}

bool Config::load(const std::filesystem::path &path) {
    fs::path p = path.empty() ? defaultPath() : path;
    loadedPath = p;
    std::ifstream f(p);
    if (!f) {
        // no file, keep defaults
        return false;
    }
    try {
        nlohmann::json j;
        f >> j;
        rawJson = j;
        if (j.contains("outputs")) {
            auto &o = j["outputs"];
            if (o.contains("default")) {
                defaultOutput = o["default"].get<DefaultOutput>();
            }
            if (o.contains("monitors") && o["monitors"].is_object()) {
                monitors.clear();
                for (auto &el : o["monitors"].items()) {
                    monitors[el.key()] = el.value().get<OutputEntry>();
                }
            }
        }
        if (j.contains("input")) {
            auto &in = j["input"];
            if (in.contains("keyboard")) keyboard = in["keyboard"].get<KeyboardConfig>();
            if (in.contains("mouse")) mouse = in["mouse"].get<MouseConfig>();
        }
        if (j.contains("layout") && j["layout"].is_object()) {
            bsp = j["layout"].get<BspConfig>();
        }
        if (j.contains("bsp") && j["bsp"].is_object()) {
            // alternative key for backwards compat
            bsp = j["bsp"].get<BspConfig>();
        }
        if (j.contains("tiling") && j["tiling"].is_object()) {
            auto &t = j["tiling"];
            if (t.contains("bsp_split_ratio")) t.at("bsp_split_ratio").get_to(bsp.split_ratio);
            if (t.contains("bsp_opposite")) t.at("bsp_opposite").get_to(bsp.opposite_orientation);
            if (t.contains("bsp_keep_ratio_on_drop")) t.at("bsp_keep_ratio_on_drop").get_to(bsp.keep_ratio_on_drop);
            if (t.contains("bsp_min_ratio")) t.at("bsp_min_ratio").get_to(bsp.min_ratio);
            if (t.contains("bsp_max_ratio")) t.at("bsp_max_ratio").get_to(bsp.max_ratio);
        }
        if (j.contains("keybinds") && j["keybinds"].is_array()) {
            keybinds = j["keybinds"].get<std::vector<Keybind>>();
            parseKeybinds();
        } else {
            ensureDefaults();
        }
        // Also support legacy flat keybinds? ensure
        if (keybinds.empty()) ensureDefaults();
        else ensureDefaults(); // ensure swap_orientation present
    } catch (const std::exception &e) {
        std::cerr << "Config load error: " << e.what() << std::endl;
        ensureDefaults();
        return false;
    }
    return true;
}

bool Config::save(const std::filesystem::path &path) const {
    fs::path p = path.empty() ? loadedPath : path;
    if (p.empty()) p = defaultPath();
    try {
        if (!p.parent_path().empty()) fs::create_directories(p.parent_path());
        nlohmann::json j;
        // preserve raw unknown fields? For now rebuild
        j["outputs"] = nlohmann::json::object();
        j["outputs"]["default"] = defaultOutput;
        j["outputs"]["monitors"] = nlohmann::json::object();
        for (auto &kv : monitors) {
            j["outputs"]["monitors"][kv.first] = kv.second;
        }
        j["input"] = nlohmann::json::object();
        j["input"]["keyboard"] = keyboard;
        j["input"]["mouse"] = mouse;
        j["layout"] = bsp;
        j["keybinds"] = keybinds;
        std::ofstream f(p);
        if (!f) return false;
        f << j.dump(4) << std::endl;
        return true;
    } catch (const std::exception &e) {
        std::cerr << "Config save error: " << e.what() << std::endl;
        return false;
    }
}

void Config::loadOrCreateDefault() {
    if (!load()) {
        ensureDefaults();
        save();
    }
}

// JSON adapters

void to_json(nlohmann::json &j, const OutputEntry &o) {
    j = nlohmann::json::object();
    if (o.width) j["width"] = o.width;
    if (o.height) j["height"] = o.height;
    if (o.refresh) j["refresh"] = o.refresh;
    if (o.scale) j["scale"] = o.scale;
    if (o.x != INT_MIN) j["x"] = o.x;
    if (o.y != INT_MIN) j["y"] = o.y;
    j["enabled"] = o.enabled;
    if (o.dpi) j["dpi"] = *o.dpi;
}

void from_json(const nlohmann::json &j, OutputEntry &o) {
    if (j.contains("width")) j.at("width").get_to(o.width);
    if (j.contains("height")) j.at("height").get_to(o.height);
    if (j.contains("refresh")) j.at("refresh").get_to(o.refresh);
    if (j.contains("scale")) j.at("scale").get_to(o.scale);
    if (j.contains("x")) j.at("x").get_to(o.x);
    if (j.contains("y")) j.at("y").get_to(o.y);
    if (j.contains("enabled")) j.at("enabled").get_to(o.enabled);
    if (j.contains("dpi")) o.dpi = j.at("dpi").get<double>();
}

void to_json(nlohmann::json &j, const DefaultOutput &o) {
    j = nlohmann::json{{"width", o.width}, {"height", o.height}, {"refresh", o.refresh}, {"scale", o.scale}};
}
void from_json(const nlohmann::json &j, DefaultOutput &o) {
    if (j.contains("width")) j.at("width").get_to(o.width);
    if (j.contains("height")) j.at("height").get_to(o.height);
    if (j.contains("refresh")) j.at("refresh").get_to(o.refresh);
    if (j.contains("scale")) j.at("scale").get_to(o.scale);
}
void to_json(nlohmann::json &j, const KeyboardConfig &k) {
    j = nlohmann::json{{"layouts", k.layouts}, {"variant", k.variant}, {"options", k.options}, {"repeat_rate", k.repeat_rate}, {"repeat_delay", k.repeat_delay}};
}
void from_json(const nlohmann::json &j, KeyboardConfig &k) {
    if (j.contains("layouts")) j.at("layouts").get_to(k.layouts);
    if (j.contains("variant")) j.at("variant").get_to(k.variant);
    if (j.contains("options")) j.at("options").get_to(k.options);
    if (j.contains("repeat_rate")) j.at("repeat_rate").get_to(k.repeat_rate);
    if (j.contains("repeat_delay")) j.at("repeat_delay").get_to(k.repeat_delay);
}
void to_json(nlohmann::json &j, const MouseConfig &m) {
    j = nlohmann::json{
        {"accel_profile", m.accel_profile},
        {"speed", m.speed},
        {"natural_scroll", m.natural_scroll},
        {"scroll_factor", m.scroll_factor},
        {"tap_enabled", m.tap_enabled},
        {"tap_button_map", m.tap_button_map},
        {"middle_emulation", m.middle_emulation},
        {"left_handed", m.left_handed},
        {"dpi", m.dpi}
    };
}
void from_json(const nlohmann::json &j, MouseConfig &m) {
    if (j.contains("accel_profile")) j.at("accel_profile").get_to(m.accel_profile);
    if (j.contains("speed")) j.at("speed").get_to(m.speed);
    if (j.contains("natural_scroll")) j.at("natural_scroll").get_to(m.natural_scroll);
    if (j.contains("scroll_factor")) j.at("scroll_factor").get_to(m.scroll_factor);
    if (j.contains("tap_enabled")) j.at("tap_enabled").get_to(m.tap_enabled);
    if (j.contains("tap_button_map")) j.at("tap_button_map").get_to(m.tap_button_map);
    if (j.contains("middle_emulation")) j.at("middle_emulation").get_to(m.middle_emulation);
    if (j.contains("left_handed")) j.at("left_handed").get_to(m.left_handed);
    if (j.contains("dpi")) j.at("dpi").get_to(m.dpi);
}
void to_json(nlohmann::json &j, const Keybind &k) {
    j = nlohmann::json{{"mods", k.mods}, {"key", k.key}, {"action", k.action}};
    if (!k.arg.empty()) j["arg"] = k.arg;
}
void from_json(const nlohmann::json &j, Keybind &k) {
    if (j.contains("mods")) j.at("mods").get_to(k.mods);
    if (j.contains("key")) j.at("key").get_to(k.key);
    if (j.contains("action")) j.at("action").get_to(k.action);
    if (j.contains("arg")) j.at("arg").get_to(k.arg);
    // modsMask and keysym will be filled by Config::parseKeybinds
}
void to_json(nlohmann::json &j, const BspConfig &b) {
    j = nlohmann::json{
        {"split_ratio", b.split_ratio},
        {"opposite_orientation", b.opposite_orientation},
        {"keep_ratio_on_drop", b.keep_ratio_on_drop},
        {"min_ratio", b.min_ratio},
        {"max_ratio", b.max_ratio}
    };
}
void from_json(const nlohmann::json &j, BspConfig &b) {
    if (j.contains("split_ratio")) j.at("split_ratio").get_to(b.split_ratio);
    if (j.contains("opposite_orientation")) j.at("opposite_orientation").get_to(b.opposite_orientation);
    if (j.contains("keep_ratio_on_drop")) j.at("keep_ratio_on_drop").get_to(b.keep_ratio_on_drop);
    if (j.contains("min_ratio")) j.at("min_ratio").get_to(b.min_ratio);
    if (j.contains("max_ratio")) j.at("max_ratio").get_to(b.max_ratio);
    if (j.contains("bsp_split_ratio")) j.at("bsp_split_ratio").get_to(b.split_ratio);
    if (j.contains("bsp_opposite")) j.at("bsp_opposite").get_to(b.opposite_orientation);
}

