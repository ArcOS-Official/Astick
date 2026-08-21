/*
 *  Astick, the wayland compositor for ArcDE.
 *  Copyright (C) 2026 Eyad Ahmed Ragheb

 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "mouse.h"
#include "../util.h"
#include "../config.h"

struct MouseDestroyListener {
    struct wl_listener listener;
    Mouse *self;
};

static void mouse_handle_destroy(struct wl_listener *listener, void *)
{
    MouseDestroyListener *wrapper = wl_container_of(listener, wrapper, listener);
    Mouse *self = wrapper->self;
    wl_list_remove(&wrapper->listener.link);
    delete wrapper;
    emit self->destroyed();
    delete self;
}

Mouse::Mouse(struct wlr_input_device *device_)
{
    device = device_;
    auto *wrapper = new MouseDestroyListener;
    wrapper->self = this;
    wrapper->listener.notify = mouse_handle_destroy;
    wl_signal_add(&device->events.destroy, &wrapper->listener);
}

void Mouse::applyConfig(const MouseConfig &cfg) {
    if (!wlr_input_device_is_libinput(device)) {
        wlr_log(WLR_INFO, "Mouse %s is not libinput, skipping config", device->name ? device->name : "unknown");
        return;
    }
    struct libinput_device *libdev = wlr_libinput_get_device_handle(device);
    if (!libdev) {
        wlr_log(WLR_ERROR, "Failed to get libinput handle for %s", device->name ? device->name : "unknown");
        return;
    }

    // Accel profile
    if (libinput_device_config_accel_is_available(libdev)) {
        enum libinput_config_accel_profile profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
        if (cfg.accel_profile == "flat") profile = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
        else if (cfg.accel_profile == "none" || cfg.accel_profile == "off") profile = LIBINPUT_CONFIG_ACCEL_PROFILE_NONE;
        // Some libinput versions use CUSTOM, fallback
        if (libinput_device_config_accel_set_profile(libdev, profile) != LIBINPUT_CONFIG_STATUS_SUCCESS) {
            wlr_log(WLR_INFO, "Failed to set accel profile %s", cfg.accel_profile.c_str());
        }
        double speed = cfg.speed;
        if (speed < -1.0) speed = -1.0;
        if (speed > 1.0) speed = 1.0;
        if (libinput_device_config_accel_set_speed(libdev, speed) != LIBINPUT_CONFIG_STATUS_SUCCESS) {
            wlr_log(WLR_INFO, "Failed to set accel speed %.2f", speed);
        }
    }

    // Natural scroll
    if (libinput_device_config_scroll_has_natural_scroll(libdev)) {
        libinput_device_config_scroll_set_natural_scroll_enabled(libdev, cfg.natural_scroll ? 1 : 0);
    }

    // Scroll factor is not directly libinput; we can use scroll factor via pointer speed? For now log and store for cursor handling
    // Libinput doesn't have scroll_factor directly, but we can note it for future use
    if (cfg.scroll_factor != 1.0) {
        wlr_log(WLR_INFO, "Mouse scroll_factor %.2f requested (applied via compositor scaling)", cfg.scroll_factor);
    }

    // Left handed
    if (libinput_device_config_left_handed_is_available(libdev)) {
        libinput_device_config_left_handed_set(libdev, cfg.left_handed ? 1 : 0);
    }

    // Tap
    if (libinput_device_config_tap_get_finger_count(libdev) > 0) {
        enum libinput_config_tap_button_map map = LIBINPUT_CONFIG_TAP_MAP_LRM;
        if (cfg.tap_button_map == "lmr") map = LIBINPUT_CONFIG_TAP_MAP_LMR;
        libinput_device_config_tap_set_button_map(libdev, map);
        libinput_device_config_tap_set_enabled(libdev, cfg.tap_enabled ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
    }

    // Middle emulation
    if (libinput_device_config_middle_emulation_is_available(libdev)) {
        libinput_device_config_middle_emulation_set_enabled(libdev, cfg.middle_emulation ? LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED : LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);
    }

    // Tap drag? Could extend
    wlr_log(WLR_INFO, "Mouse %s config applied: accel=%s speed=%.2f natural=%d left_handed=%d tap=%d",
        device->name ? device->name : "unknown", cfg.accel_profile.c_str(), cfg.speed, cfg.natural_scroll, cfg.left_handed, cfg.tap_enabled);
}

Mouse::~Mouse() {}
