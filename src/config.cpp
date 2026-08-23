#include "config.h"
#include "embedded_config.h"
#include <fstream>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <xkbcommon/xkbcommon.h>

namespace fs = std::filesystem;

std::string Config::s_originalWaylandDisplay;
std::string Config::s_originalDisplay;

void Config::captureOriginalDisplay() {
    const char *wd = std::getenv("WAYLAND_DISPLAY");
    const char *d = std::getenv("DISPLAY");
    if (wd) s_originalWaylandDisplay = wd;
    if (d) s_originalDisplay = d;
}

bool Config::isWindowedMode() const {
    // Windowed/nested if we were launched inside an existing Wayland or X11 session.
    // s_originalWaylandDisplay is captured before we overwrite WAYLAND_DISPLAY with our socket.
    if (!s_originalWaylandDisplay.empty()) return true;
    if (!s_originalDisplay.empty()) {
        // X11 parent also counts as windowed (XWayland or X11 host)
        // But if we're running on DRM, DISPLAY may be empty. Only treat as windowed if WAYLAND_DISPLAY also? For safety treat DISPLAY presence as windowed too.
        return true;
    }
    // also check current env as fallback (in case capture not called yet)
    if (std::getenv("WAYLAND_DISPLAY") && std::string(std::getenv("WAYLAND_DISPLAY")).find("wayland-") != std::string::npos) {
        // This could be our own socket, but if it exists before backend start we treat as windowed
        return true;
    }
    return false;
}

std::string Config::effectiveMod() const {
    std::string lower = modkey;
    for (auto &c : lower) c = std::tolower((unsigned char)c);
    bool isSuper = (lower == "super" || lower == "mod4" || lower == "logo" || lower == "meta");
    if (isSuper && isWindowedMode()) {
        return "Alt";
    }
    return modkey;
}

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

std::string Config::embeddedDefaultJson() {
    return std::string(kEmbeddedConfig);
}

void Config::ensureDefaults() {
    if (keybinds.empty()) {
        keybinds = {
            {{"Mod"}, "Escape", "quit", "", 0, XKB_KEY_NoSymbol},
            {{"Mod"}, "F1", "focus_prev", "", 0, XKB_KEY_NoSymbol},
            {{"Mod"}, "F2", "toggle_layout", "", 0, XKB_KEY_NoSymbol},
            {{"Mod"}, "F3", "new_workspace", "", 0, XKB_KEY_NoSymbol},
            {{"Mod"}, "F4", "goto_workspace", "1", 0, XKB_KEY_NoSymbol},
            {{"Mod"}, "t", "swap_orientation", "", 0, XKB_KEY_NoSymbol},
            {{"Mod"}, "f", "toggle_floating", "", 0, XKB_KEY_NoSymbol},
            {{"Mod"}, "m", "toggle_maximize", "", 0, XKB_KEY_NoSymbol},
            {{}, "F11", "toggle_fullscreen", "", 0, XKB_KEY_NoSymbol},
            {{"Mod"}, "Return", "toggle_fullscreen", "", 0, XKB_KEY_NoSymbol},
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
            keybinds.push_back({{"Mod"}, "t", "swap_orientation", "", 0, XKB_KEY_NoSymbol});
            parseKeybinds();
        }
        if (!hasFloating) {
            keybinds.push_back({{"Mod"}, "f", "toggle_floating", "", 0, XKB_KEY_NoSymbol});
            parseKeybinds();
        }
        if (!hasMaximize) {
            keybinds.push_back({{"Mod"}, "m", "toggle_maximize", "", 0, XKB_KEY_NoSymbol});
            parseKeybinds();
        }
        if (!hasFullscreen) {
            keybinds.push_back({{}, "F11", "toggle_fullscreen", "", 0, XKB_KEY_NoSymbol});
            keybinds.push_back({{"Mod"}, "Return", "toggle_fullscreen", "", 0, XKB_KEY_NoSymbol});
            parseKeybinds();
        }
    }
}

void Config::parseKeybinds() {
    for (auto &k : keybinds) {
        std::string eff = effectiveMod();
        std::string origLower = modkey;
        for (auto &c : origLower) c = std::tolower((unsigned char)c);
        bool origIsSuper = (origLower=="super"||origLower=="mod4"||origLower=="logo"||origLower=="meta");
        bool windowed = isWindowedMode();
        for (auto &m : k.mods) {
            std::string lower = m;
            for (auto &c : lower) c = std::tolower((unsigned char)c);
            if (lower=="mod" || lower=="modkey" || lower=="mod_key" || lower=="mod-key") {
                m = eff;
            } else if (windowed && origIsSuper && (lower=="super"||lower=="mod4"||lower=="logo"||lower=="meta")) {
                // In windowed mode, if modkey was Super, explicit Super binds conflict with parent; fallback to Alt
                m = "Alt";
            }
            // else keep as is
        }
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

static std::string preprocessConfigText(const std::string &raw, std::vector<std::string> &outErrors) {
    std::string out;
    out.reserve(raw.size()*2);
    std::string lastKey;
    bool inString = false;
    bool escaped = false;
    size_t n = raw.size();
    for (size_t i = 0; i < n; ) {
        char c = raw[i];
        if (inString) {
            out.push_back(c);
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') {
                inString = false;
                // look ahead for key detection: check if this string was a key (next non-space is ':')
                size_t k = i + 1;
                while (k < n && (raw[k]==' '||raw[k]=='\t'||raw[k]=='\n'||raw[k]=='\r')) k++;
                if (k < n && raw[k]==':') {
                    // extract key content between quotes: we need to capture it
                    // find opening quote earlier: we already have it, so re-parse
                    // simpler: find last quoted string we just emitted
                    // We know out ends with '"', find its opening
                    size_t endPos = out.size()-1;
                    size_t startPos = out.rfind('"', endPos-1);
                    if (startPos != std::string::npos) {
                        std::string key = out.substr(startPos+1, endPos-startPos-1);
                        lastKey = key;
                    }
                }
            }
            i++;
            continue;
        }
        if (c == '"') {
            inString = true;
            out.push_back(c);
            i++;
            continue;
        }
        if (c == '/' && i+1 < n && raw[i+1]=='/') {
            // line comment - copy through to newline (tolerated)
            size_t j=i;
            while(j<n && raw[j]!='\n') { out.push_back(raw[j]); j++; }
            i=j;
            continue;
        }
        // bare identifiers off/default
        if (std::isalpha((unsigned char)c)) {
            size_t j=i;
            while(j<n && (std::isalnum((unsigned char)raw[j])||raw[j]=='_'||raw[j]=='-')) j++;
            std::string word = raw.substr(i, j-i);
            // pair-level keys for which bare off/default should expand to full pair
            auto isPairKey = [&](const std::string &k){
                return k=="window"||k=="popup"||k=="tilingMove"||k=="workspaceSwitch"||k=="fullscreen"||k=="maximize"||k=="floating"||k=="focus"||k=="layer";
            };
            if (word=="off") {
                if (isPairKey(lastKey)) {
                    out += "{\"start\":{\"enabled\":false,\"duration\":0,\"style\":\"fade\",\"easing\":\"linear\"},\"end\":{\"enabled\":false,\"duration\":0,\"style\":\"fade\",\"easing\":\"linear\"}}";
                } else {
                    out += "{\"enabled\":false,\"duration\":0,\"style\":\"fade\",\"easing\":\"linear\"}";
                }
            } else if (word=="default") {
                if (lastKey=="duration") out += "250";
                else if (lastKey=="easing") out += "\"easeOutCubic\"";
                else if (lastKey=="style") out += "\"fade\"";
                else if (lastKey=="enabled") out += "true";
                else if (lastKey=="speed") out += "1.0";
                else if (lastKey=="windowLayerOnly") out += "true";
                else if (isPairKey(lastKey)) {
                    out += "{\"start\":{\"enabled\":true,\"duration\":250,\"style\":\"fade\",\"easing\":\"easeOutCubic\"},\"end\":{\"enabled\":true,\"duration\":250,\"style\":\"fade\",\"easing\":\"easeOutCubic\"}}";
                } else {
                    // generic AnimDef default
                    out += "{\"enabled\":true,\"duration\":250,\"style\":\"fade\",\"easing\":\"easeOutCubic\"}";
                }
            } else {
                // unknown bare word -> keep as is and record error (will cause json parse failure)
                out += word;
                // only report if it looks like a macro position (after colon or comma)
                // we report all unknown bare identifiers as errors for full log
                // but avoid flagging boolean literals inside already?
                if (word!="true" && word!="false" && word!="null") {
                    outErrors.push_back("Unknown bare identifier '" + word + "' (use \"off\" or \"default\" macros, or quote strings)");
                }
            }
            i=j;
            continue;
        }
        // track lastKey for non-string? not needed
        out.push_back(c);
        i++;
    }
    // quick brace balance check
    int braces=0, brackets=0;
    {
        bool s=false, esc=false;
        for(char ch: out){
            if(s){
                if(esc) esc=false;
                else if(ch=='\\') esc=true;
                else if(ch=='"') s=false;
                continue;
            }
            if(ch=='"') s=true;
            else if(ch=='{') braces++;
            else if(ch=='}') braces--;
            else if(ch=='[') brackets++;
            else if(ch==']') brackets--;
        }
        if(braces!=0) outErrors.push_back("Unbalanced braces in config ({} mismatch)");
        if(brackets!=0) outErrors.push_back("Unbalanced brackets in config ([] mismatch)");
    }
    return out;
}

bool Config::load(const std::filesystem::path &path) {
    fs::path p = path.empty() ? defaultPath() : path;
    loadedPath = p;
    std::ifstream f(p);
    if (!f) {
        return false;
    }
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<std::string> allErrors;
    std::vector<std::string> preErrors;
    std::string preprocessed = preprocessConfigText(raw, preErrors);
    for(auto &e: preErrors) allErrors.push_back("preprocess: " + e);

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(preprocessed);
    } catch (const nlohmann::json::parse_error &e) {
        allErrors.push_back(std::string("JSON parse: ") + e.what());
        // also try to capture raw line info
    } catch (const std::exception &e) {
        allErrors.push_back(std::string("JSON parse error: ") + e.what());
    }

    // If parse failed, we cannot continue to per-field validation; go to error handling
    if (!allErrors.empty() && j.is_null()) {
        // j is null due to parse failure -> report and archive
        std::cerr << "Config load error at " << p << " (" << allErrors.size() << " issues):\n";
        for(auto &e: allErrors) std::cerr << " - " << e << "\n";
        // try to archive
        try {
            fs::path oldp = p;
            oldp += ".old";
            std::filesystem::rename(p, oldp);
            std::cerr << "Renamed invalid config to " << oldp << "\n";
        } catch(...) {}
        // regenerate
        loadOrCreateDefault();
        return false;
    }

    // j parsed, now validate per-section and collect all errors (not just first)
    rawJson = j;
    // helper to capture per-section exceptions
    auto trySection = [&](const std::string &name, auto fn){
        try { fn(); } catch (const std::exception &e) {
            allErrors.push_back(name + ": " + e.what());
        } catch (...) {
            allErrors.push_back(name + ": unknown error");
        }
    };

    trySection("outputs", [&](){
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
    });
    trySection("input", [&](){
        if (j.contains("input")) {
            auto &in = j["input"];
            if (in.contains("keyboard")) keyboard = in["keyboard"].get<KeyboardConfig>();
            if (in.contains("mouse")) mouse = in["mouse"].get<MouseConfig>();
        }
    });
    trySection("layout", [&](){
        if (j.contains("layout") && j["layout"].is_object()) {
            bsp = j["layout"].get<BspConfig>();
        }
        if (j.contains("bsp") && j["bsp"].is_object()) {
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
    });
    trySection("modkey", [&](){
        // top-level mod: "mod", "modkey", "modKey"
        if (j.contains("mod") && j["mod"].is_string()) j["mod"].get_to(modkey);
        else if (j.contains("modkey") && j["modkey"].is_string()) j["modkey"].get_to(modkey);
        else if (j.contains("modKey") && j["modKey"].is_string()) j["modKey"].get_to(modkey);
        if (j.contains("general") && j["general"].is_object()) {
            auto &g = j["general"];
            if (g.contains("mod") && g["mod"].is_string()) g["mod"].get_to(modkey);
            else if (g.contains("modkey") && g["modkey"].is_string()) g["modkey"].get_to(modkey);
        }
        if (j.contains("input") && j["input"].is_object()) {
            auto &in = j["input"];
            if (in.contains("mod") && in["mod"].is_string()) in["mod"].get_to(modkey);
            else if (in.contains("modkey") && in["modkey"].is_string()) in["modkey"].get_to(modkey);
            else if (in.contains("modKey") && in["modKey"].is_string()) in["modKey"].get_to(modkey);
        }
    });
    trySection("keybinds", [&](){
        if (j.contains("keybinds") && j["keybinds"].is_array()) {
            keybinds = j["keybinds"].get<std::vector<Keybind>>();
            parseKeybinds();
        } else {
            ensureDefaults();
        }
        if (keybinds.empty()) ensureDefaults();
        else ensureDefaults();
        // re-parse after modkey known (ensureDefaults already called parseKeybinds with effectiveMod)
        // if modkey was parsed after ensureDefaults, we need to re-resolve Mod placeholders
        parseKeybinds();
    });
    trySection("decorations", [&](){
        if (j.contains("decorations") && j["decorations"].is_object()) {
            decorations = j["decorations"].get<DecorationConfigData>();
        }
        if (j.contains("appearance") && j["appearance"].is_object()) {
            auto &ap = j["appearance"];
            if (ap.contains("decorations")) decorations = ap["decorations"].get<DecorationConfigData>();
        }
    });
    trySection("animations", [&](){
        if (j.contains("animations") && j["animations"].is_object()) {
            animations = j["animations"].get<AnimationsConfig>();
        }
        if (j.contains("appearance") && j["appearance"].is_object()) {
            auto &ap = j["appearance"];
            if (ap.contains("animations")) animations = ap["animations"].get<AnimationsConfig>();
        }
    });

    // run animation validation (collect all)
    {
        auto animErrs = validateAnimationsConfig(animations);
        for(auto &e: animErrs) allErrors.push_back("animations: " + e);
    }

    if (!allErrors.empty()) {
        std::cerr << "Config load error at " << p << " (" << allErrors.size() << " issues):\n";
        for(auto &e: allErrors) std::cerr << " - " << e << "\n";
        try {
            fs::path oldp = p;
            oldp += ".old";
            // remove previous .old if exists
            std::filesystem::remove(oldp);
            std::filesystem::rename(p, oldp);
            std::cerr << "Renamed invalid config to " << oldp << "\n";
        } catch(const std::exception &e){
            std::cerr << "Failed to rename invalid config: " << e.what() << "\n";
        }
        // avoid recursion: directly write embedded instead of calling loadOrCreateDefault which would try load again
        std::string embedded = embeddedDefaultJson();
        if (!embedded.empty()) {
            try {
                std::filesystem::create_directories(p.parent_path());
                std::ofstream out(p);
                out << embedded;
                std::cerr << "Regenerated default config at " << p << "\n";
                // reload the freshly generated default (should succeed)
                // parse embedded directly to reset state
                try {
                    nlohmann::json jj = nlohmann::json::parse(embedded);
                    if (jj.contains("animations")) animations = jj["animations"].get<AnimationsConfig>();
                    if (jj.contains("decorations")) decorations = jj["decorations"].get<DecorationConfigData>();
                    // keep other fields at defaults
                    ensureDefaults();
                } catch(...) {}
                return false;
            } catch(...) {}
        }
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
        j["decorations"] = decorations;
        j["animations"] = animations;
        j["mod"] = modkey;
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
    fs::path p = defaultPath();
    if (std::filesystem::exists(p)) {
        if (load(p)) return;
    }
    std::string embedded = embeddedDefaultJson();
    if (!embedded.empty()) {
        std::filesystem::create_directories(p.parent_path());
        std::ofstream out(p);
        out << embedded;
    } else {
        ensureDefaults();
        save(p);
    }
    load(p);
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

void to_json(nlohmann::json &j, const GradientConfigData &g) {
    j = nlohmann::json{{"enabled", g.enabled}, {"colors", g.colors}, {"angle", g.angle}, {"animate", g.animate}};
}
void from_json(const nlohmann::json &j, GradientConfigData &g) {
    if (j.contains("enabled")) j.at("enabled").get_to(g.enabled);
    if (j.contains("colors")) j.at("colors").get_to(g.colors);
    if (j.contains("angle")) j.at("angle").get_to(g.angle);
    if (j.contains("animate")) j.at("animate").get_to(g.animate);
}
void to_json(nlohmann::json &j, const BorderConfigData &b) {
    j = nlohmann::json{{"enabled", b.enabled}, {"width", b.width}, {"radius", b.radius}, {"active_color", b.active_color}, {"inactive_color", b.inactive_color}, {"gradient", b.gradient}, {"animate", b.animate}, {"animation_duration", b.animation_duration}, {"animation_easing", b.animation_easing}};
}
void from_json(const nlohmann::json &j, BorderConfigData &b) {
    if (j.contains("enabled")) j.at("enabled").get_to(b.enabled);
    if (j.contains("width")) j.at("width").get_to(b.width);
    if (j.contains("radius")) j.at("radius").get_to(b.radius);
    if (j.contains("active_color")) j.at("active_color").get_to(b.active_color);
    if (j.contains("inactive_color")) j.at("inactive_color").get_to(b.inactive_color);
    if (j.contains("gradient")) j.at("gradient").get_to(b.gradient);
    if (j.contains("animate")) j.at("animate").get_to(b.animate);
    if (j.contains("animation_duration")) j.at("animation_duration").get_to(b.animation_duration);
    else if (j.contains("duration")) j.at("duration").get_to(b.animation_duration);
    if (j.contains("animation_easing")) j.at("animation_easing").get_to(b.animation_easing);
    else if (j.contains("easing")) j.at("easing").get_to(b.animation_easing);
}
void to_json(nlohmann::json &j, const TitleBarConfigData &t) {
    j = nlohmann::json{{"enabled", t.enabled}, {"height", t.height}, {"color", t.color}, {"text_color", t.text_color}, {"font_size", t.font_size}, {"show_title", t.show_title}, {"show_buttons", t.show_buttons}};
}
void from_json(const nlohmann::json &j, TitleBarConfigData &t) {
    if (j.contains("enabled")) j.at("enabled").get_to(t.enabled);
    if (j.contains("height")) j.at("height").get_to(t.height);
    if (j.contains("color")) j.at("color").get_to(t.color);
    if (j.contains("text_color")) j.at("text_color").get_to(t.text_color);
    if (j.contains("font_size")) j.at("font_size").get_to(t.font_size);
    if (j.contains("show_title")) j.at("show_title").get_to(t.show_title);
    if (j.contains("show_buttons")) j.at("show_buttons").get_to(t.show_buttons);
}
void to_json(nlohmann::json &j, const DecorationConfigData &d) {
    j = nlohmann::json{{"border", d.border}, {"titlebar", d.titlebar}, {"outer_gap", d.outer_gap}, {"inner_gap", d.inner_gap}};
}
void from_json(const nlohmann::json &j, DecorationConfigData &d) {
    if (j.contains("border")) j.at("border").get_to(d.border);
    if (j.contains("titlebar")) j.at("titlebar").get_to(d.titlebar);
    if (j.contains("outer_gap")) j.at("outer_gap").get_to(d.outer_gap);
    if (j.contains("inner_gap")) j.at("inner_gap").get_to(d.inner_gap);
}
std::string toString(AnimationStyle s) {
    switch(s){
        case AnimationStyle::Fade: return "fade";
        case AnimationStyle::ScaleIn: return "scaleIn";
        case AnimationStyle::ScaleOut: return "scaleOut";
        case AnimationStyle::SlideTop: return "slideTop";
        case AnimationStyle::SlideBottom: return "slideBottom";
        case AnimationStyle::SlideLeft: return "slideLeft";
        case AnimationStyle::SlideRight: return "slideRight";
        case AnimationStyle::Pop: return "pop";
        case AnimationStyle::SlideFade: return "slideFade";
        case AnimationStyle::Slide: return "slide";
        case AnimationStyle::Cube: return "cube";
        case AnimationStyle::FadeWindowLayer: return "fadeWindowLayer";
    }
    return "fade";
}
AnimationStyle styleFromString(const std::string &s, AnimationStyle fallback) {
    std::string t=s;
    for(char &c: t) c = std::tolower((unsigned char)c);
    if(t=="fade") return AnimationStyle::Fade;
    if(t=="scalein"||t=="scale") return AnimationStyle::ScaleIn;
    if(t=="scaleout") return AnimationStyle::ScaleOut;
    if(t=="slidetop"||t=="slide_top"||t=="top") return AnimationStyle::SlideTop;
    if(t=="slidebottom"||t=="slide_bottom"||t=="bottom") return AnimationStyle::SlideBottom;
    if(t=="slideleft"||t=="slide_left"||t=="left") return AnimationStyle::SlideLeft;
    if(t=="slideright"||t=="slide_right"||t=="right") return AnimationStyle::SlideRight;
    if(t=="pop") return AnimationStyle::Pop;
    if(t=="slidefade"||t=="slide_fade") return AnimationStyle::SlideFade;
    if(t=="slide") return AnimationStyle::Slide;
    if(t=="cube") return AnimationStyle::Cube;
    if(t=="fadewindowlayer"||t=="fade_window_layer") return AnimationStyle::FadeWindowLayer;
    return fallback;
}

void to_json(nlohmann::json &j, const AnimationPreset &a) {
    j = nlohmann::json{{"enabled", a.enabled}, {"duration", a.duration}, {"easing", a.easing}};
}
void from_json(const nlohmann::json &j, AnimationPreset &a) {
    if (j.contains("enabled")) j.at("enabled").get_to(a.enabled);
    if (j.contains("duration")) j.at("duration").get_to(a.duration);
    if (j.contains("easing")) j.at("easing").get_to(a.easing);
}
void to_json(nlohmann::json &j, const AnimDef &a) {
    j = nlohmann::json{{"enabled", a.enabled}, {"style", toString(a.style)}, {"duration", a.duration}, {"easing", a.easing}};
}
void from_json(const nlohmann::json &j, AnimDef &a) {
    if (j.contains("enabled")) j.at("enabled").get_to(a.enabled);
    if (j.contains("duration")) j.at("duration").get_to(a.duration);
    if (j.contains("easing")) j.at("easing").get_to(a.easing);
    if (j.contains("style")) {
        std::string s; j.at("style").get_to(s);
        a.style = styleFromString(s, a.style);
    }
}
void to_json(nlohmann::json &j, const AnimPair &p) {
    j = nlohmann::json::object();
    j["start"] = p.start;
    if (p.end) j["end"] = *p.end;
}
void from_json(const nlohmann::json &j, AnimPair &p) {
    if (j.contains("start")) j.at("start").get_to(p.start);
    else {
        // allow direct AnimDef as pair (shorthand)
        try { p.start = j.get<AnimDef>(); } catch(...) {}
    }
    if (j.contains("end")) {
        AnimDef e; j.at("end").get_to(e);
        p.end = e;
    }
}
std::vector<std::string> validateAnimationsConfig(const AnimationsConfig &a) {
    std::vector<std::string> errs;
    auto checkDef = [&](const std::string &id, const AnimDef &d, const std::string &which){
        if(d.duration < 0 || d.duration > 10000) errs.push_back(id + "." + which + ".duration out of range 0..10000: " + std::to_string(d.duration));
        static const std::vector<std::string> validEasings = {"linear","easeInQuad","easeOutQuad","easeInOutQuad","easeInCubic","easeOutCubic","easeInOutCubic","easeInQuart","easeOutQuart","easeInOutQuart","easeOutBack","easeOutElastic"};
        bool found=false;
        std::string low=d.easing; for(char &c:low) c=std::tolower((unsigned char)c);
        for(auto &v: validEasings){ std::string vl=v; for(char &c:vl) c=std::tolower((unsigned char)c); if(vl==low) {found=true;break;} }
        if(!found && !d.easing.empty()) errs.push_back(id + "." + which + ".easing unknown: " + d.easing);
    };
    for(auto &kv: a.pairs){
        checkDef(kv.first, kv.second.start, "start");
        if(kv.second.end) checkDef(kv.first, *kv.second.end, "end");
        // reversible asserts: window, popup, workspaceSwitch, fullscreen, maximize, floating, layer must have end
        // but if start is disabled (off -> duration 0 / enabled false), we treat as fully disabled and don't require end
        const std::vector<std::string> reversible = {"window","popup","workspaceSwitch","fullscreen","maximize","floating","layer"};
        bool isRev = false;
        for(auto &r: reversible) if(r==kv.first) isRev=true;
        if(isRev && !kv.second.hasEnd()){
            bool startDisabled = !kv.second.start.enabled || kv.second.start.duration == 0;
            if(!startDisabled) errs.push_back(kv.first + " is reversible and must have 'end' defined");
        }
    }
    return errs;
}
void to_json(nlohmann::json &j, const AnimationsConfig &a) {
    j = nlohmann::json{{"enabled", a.enabled}, {"speed", a.speed}, {"windowLayerOnly", a.windowLayerOnly}};
    nlohmann::json pairs = nlohmann::json::object();
    for (auto &kv : a.pairs) pairs[kv.first] = kv.second;
    j["pairs"] = pairs;
    // keep presets for backwards compat (write both)
    nlohmann::json presets = nlohmann::json::object();
    for (auto &kv : a.presets) presets[kv.first] = kv.second;
    if(!a.presets.empty()) j["presets"] = presets;
}
void from_json(const nlohmann::json &j, AnimationsConfig &a) {
    if (j.contains("enabled")) j.at("enabled").get_to(a.enabled);
    if (j.contains("speed")) j.at("speed").get_to(a.speed);
    else if (j.contains("global_speed")) j.at("global_speed").get_to(a.speed);
    if (j.contains("windowLayerOnly")) j.at("windowLayerOnly").get_to(a.windowLayerOnly);
    else if (j.contains("window_layer_only")) j.at("window_layer_only").get_to(a.windowLayerOnly);
    // new grouped pairs
    if (j.contains("pairs") && j["pairs"].is_object()) {
        for (auto &el : j["pairs"].items()) {
            try { a.pairs[el.key()] = el.value().get<AnimPair>(); } catch (const std::exception &e) {
                // try AnimDef shorthand
                try { AnimDef d = el.value().get<AnimDef>(); AnimPair p; p.start = d; a.pairs[el.key()] = p; } catch(...) {}
            }
        }
    }
    // also support flat pairs at top-level: "window": {start...}
    for (auto &el : j.items()) {
        if (el.key()=="enabled"||el.key()=="speed"||el.key()=="global_speed"||el.key()=="presets"||el.key()=="pairs"||el.key()=="windowLayerOnly"||el.key()=="window_layer_only") continue;
        if (el.value().is_object() && (el.value().contains("start")||el.value().contains("end")||el.value().contains("style"))) {
            if (a.pairs.find(el.key())==a.pairs.end()) {
                try { a.pairs[el.key()] = el.value().get<AnimPair>(); } catch(...) {
                    try { AnimDef d = el.value().get<AnimDef>(); AnimPair p; p.start = d; a.pairs[el.key()] = p; } catch(...) {}
                }
            }
        }
    }
    // legacy presets -> migrate into pairs for backwards compat
    auto ensurePair = [&](const std::string &pairKey, const std::string &openKey, const std::string &closeKey, AnimationStyle openStyle, AnimationStyle closeStyle){
        if(a.pairs.find(pairKey)!=a.pairs.end()) return;
        auto itO = a.presets.find(openKey);
        auto itC = a.presets.find(closeKey);
        if(itO!=a.presets.end() || itC!=a.presets.end()){
            AnimPair p;
            if(itO!=a.presets.end()){
                p.start.enabled = itO->second.enabled;
                p.start.duration = itO->second.duration;
                p.start.easing = itO->second.easing;
                p.start.style = openStyle;
            }
            if(itC!=a.presets.end()){
                AnimDef e;
                e.enabled = itC->second.enabled;
                e.duration = itC->second.duration;
                e.easing = itC->second.easing;
                e.style = closeStyle;
                p.end = e;
            }
            a.pairs[pairKey]=p;
        }
    };
    // migrated presets can be under "presets" object or flat keys
    auto parsePreset = [&](const nlohmann::json &obj, const std::string &key){
        if (!obj.contains(key)) return;
        try { a.presets[key] = obj.at(key).get<AnimationPreset>(); } catch (...) {}
        if (obj.at(key).is_boolean()) a.presets[key].enabled = obj.at(key).get<bool>();
        else if (obj.at(key).is_number()) a.presets[key].duration = obj.at(key).get<int>();
    };
    if (j.contains("presets") && j["presets"].is_object()) {
        for (auto &el : j["presets"].items()) {
            try { a.presets[el.key()] = el.value().get<AnimationPreset>(); } catch (...) {
                if (el.value().is_boolean()) a.presets[el.key()].enabled = el.value().get<bool>();
            }
        }
    }
    const char* ids[] = {"border","window_open","window_close","workspace_switch","titlebar","fade"};
    for (auto *id : ids) parsePreset(j, id);
    for (auto &el : j.items()) {
        if (el.key()=="enabled"||el.key()=="speed"||el.key()=="global_speed"||el.key()=="presets"||el.key()=="pairs"||el.key()=="windowLayerOnly"||el.key()=="window_layer_only") continue;
        if (el.value().is_object() && (el.value().contains("duration")||el.value().contains("enabled")||el.value().contains("easing"))) {
            if(a.presets.find(el.key())==a.presets.end()){
                try { a.presets[el.key()] = el.value().get<AnimationPreset>(); } catch (...) {}
            }
        } else if (el.value().is_boolean()) {
            if(a.presets.find(el.key())==a.presets.end()) a.presets[el.key()].enabled = el.value().get<bool>();
        }
    }
    ensurePair("window","window_open","window_close", AnimationStyle::ScaleIn, AnimationStyle::ScaleOut);
    ensurePair("workspaceSwitch","workspace_switch","", AnimationStyle::SlideLeft, AnimationStyle::SlideRight);
    ensurePair("popup","popup_open","popup_close", AnimationStyle::Fade, AnimationStyle::Fade);
    // if still no pairs, create defaults
    if(a.pairs.empty() && !a.presets.empty()){
        // leave migration as above; if still empty, caller will ensure defaults elsewhere
    }
    // ensure some defaults if nothing defined
    auto ensureDefaultPair = [&](const std::string &key, AnimationStyle st, AnimationStyle en, int dur){
        if(a.pairs.find(key)==a.pairs.end()){
            AnimPair p; p.start.style=st; p.start.duration=dur; p.start.easing="easeOutCubic"; p.start.enabled=true;
            AnimDef e; e.style=en; e.duration=dur; e.easing="easeInCubic"; e.enabled=true;
            p.end=e;
            a.pairs[key]=p;
        }
    };
    if(a.pairs.find("window")==a.pairs.end()) ensureDefaultPair("window", AnimationStyle::ScaleIn, AnimationStyle::ScaleOut, 250);
    if(a.pairs.find("popup")==a.pairs.end()) ensureDefaultPair("popup", AnimationStyle::Fade, AnimationStyle::Fade, 180);
    if(a.pairs.find("tilingMove")==a.pairs.end()){
        AnimPair p; p.start.style=AnimationStyle::Slide; p.start.duration=200; p.start.easing="easeOutCubic"; a.pairs["tilingMove"]=p;
    }
}

