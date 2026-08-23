/* generated from config.template.json - do not edit manually, regenerate via CMake or python3 */
#pragma once

constexpr const char kEmbeddedConfig[] = R"JSON({
    "input": {
        "keyboard": {
            "layouts": "us,ara",
            "options": "compose:ralt,grp:alt_shift_toggle",
            "repeat_delay": 600,
            "repeat_rate": 25,
            "variant": ""
        },
        "mouse": {
            "accel_profile": "adaptive",
            "dpi": 0.0,
            "left_handed": false,
            "middle_emulation": false,
            "natural_scroll": false,
            "scroll_factor": 1.0,
            "speed": 0.0,
            "tap_button_map": "lrm",
            "tap_enabled": true
        }
    },
    "mod": "Alt",
    "keybinds": [
        {
            "action": "quit",
            "key": "Escape",
            "mods": [
                "Mod"
            ]
        },
        {
            "action": "focus_prev",
            "key": "F1",
            "mods": [
                "Mod"
            ]
        },
        {
            "action": "toggle_layout",
            "key": "F2",
            "mods": [
                "Mod"
            ]
        },
        {
            "action": "new_workspace",
            "key": "F3",
            "mods": [
                "Mod"
            ]
        },
        {
            "action": "goto_workspace",
            "arg": "1",
            "key": "F4",
            "mods": [
                "Mod"
            ]
        },
        {
            "action": "goto_workspace",
            "arg": "1",
            "key": "1",
            "mods": [
                "Mod"
            ]
        },
        {
            "action": "goto_workspace",
            "arg": "2",
            "key": "2",
            "mods": [
                "Mod"
            ]
        },
        {
            "action": "close_window",
            "key": "q",
            "mods": [
                "Mod",
                "Shift"
            ]
        },
        {
            "action": "swap_orientation",
            "key": "t",
            "mods": [
                "Mod"
            ]
        },
        {
            "action": "toggle_floating",
            "key": "f",
            "mods": [
                "Mod"
            ]
        },
        {
            "action": "toggle_maximize",
            "key": "m",
            "mods": [
                "Mod"
            ]
        },
        {
            "action": "toggle_fullscreen",
            "key": "F11",
            "mods": []
        },
        {
            "action": "toggle_fullscreen",
            "key": "Return",
            "mods": [
                "Mod"
            ]
        }
    ],
    "layout": {
        "keep_ratio_on_drop": true,
        "max_ratio": 0.9,
        "min_ratio": 0.1,
        "opposite_orientation": true,
        "split_ratio": 0.5
    },
    "decorations": {
        "border": {
            "enabled": true,
            "width": 2,
            "radius": 8,
            "active_color": "#ff5500",
            "inactive_color": "#3a3a3a",
            "gradient": {
                "enabled": false,
                "colors": ["#ff5500", "#ff00aa", "#5500ff"],
                "angle": 45,
                "animate": false
            },
            "animate": true,
            "animation_duration": 200,
            "animation_easing": "easeOutCubic"
        },
        "titlebar": {
            "enabled": false,
            "height": 28,
            "color": "#222222",
            "text_color": "#eeeeee",
            "font_size": 11,
            "show_title": true,
            "show_buttons": true
        },
        "outer_gap": 0,
        "inner_gap": 0
    },
    "animations": {
        "enabled": true,
        "speed": 1.0,
        "windowLayerOnly": true,
        "pairs": {
            "window": { "start": {"style":"scaleIn","duration":150,"easing":"easeOutCubic"}, "end": {"style":"scaleOut","duration":200,"easing":"easeInCubic"} },
            "popup": { "start": {"style":"fade","duration":80,"easing":"easeOutCubic"}, "end": {"style":"fade","duration":100,"easing":"easeInCubic"} },
            "tilingMove": { "start": {"style":"slide","duration":250,"easing":"easeOutCubic","enabled":true} },
            "workspaceSwitch": { "start": {"style":"slideLeft","duration":300,"easing":"easeInOutCubic"}, "end": {"style":"slideRight","duration":300,"easing":"easeInOutCubic"} },
            "fullscreen": { "start": {"style":"fade","duration":250,"easing":"easeOutCubic"}, "end": {"style":"fade","duration":200,"easing":"easeInCubic"} },
            "maximize": { "start": {"style":"scaleIn","duration":200,"easing":"easeOutCubic"}, "end": {"style":"scaleOut","duration":180,"easing":"easeInCubic"} },
            "floating": { "start": {"style":"scaleIn","duration":180,"easing":"easeOutCubic"}, "end": {"style":"scaleOut","duration":180,"easing":"easeInOutCubic"} },
            "focus": { "start": {"style":"fade","duration":200,"easing":"easeOutCubic"} },
            "layer": { "start": {"style":"slideTop","duration":220,"easing":"easeOutCubic"}, "end": {"style":"slideTop","duration":180,"easing":"easeInCubic"} }
        }
    },
    "outputs": {
        "default": {
            "height": 720,
            "refresh": 60.0,
            "scale": 1.0,
            "width": 1280
        },
        "monitors": {}
    }
}
)JSON";
