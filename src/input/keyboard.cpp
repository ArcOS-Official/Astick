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

#include "keyboard.h"
#include "../util.h"
#include "../config.h"

void keyboard_handle_modifiers(struct wl_listener *listener, void *)
{
    Keyboard *self = wl_container_of(listener, self, modifiersListener);
    wlr_seat_set_keyboard(self->seat, self->wlrKeyboard);
    wlr_seat_keyboard_notify_modifiers(self->seat, &self->wlrKeyboard->modifiers);
    emit self->modifiersChanged();
}

void keyboard_handle_key(struct wl_listener *listener, void *data)
{
    Keyboard *self = wl_container_of(listener, self, keyListener);
    struct wlr_keyboard_key_event *event = (struct wlr_keyboard_key_event *)data;
    emit self->keyPressed(event);
}

void keyboard_handle_destroy(struct wl_listener *listener, void *)
{
    Keyboard *self = wl_container_of(listener, self, destroyListener);
    wl_list_remove(&self->modifiersListener.link);
    wl_list_remove(&self->keyListener.link);
    wl_list_remove(&self->destroyListener.link);
    emit self->destroyed();
    delete self;
}

uint64_t Keyboard::genId() {
    uint64_t h = (uint64_t)(uintptr_t)wlrKeyboard;
    return ResourceKind::InputBase + (h % ResourceKind::CountPerKind);
}

Keyboard::Keyboard(struct wlr_input_device *device, struct wlr_seat *seat_)
{
    generateId();
    seat = seat_;
    wlrKeyboard = wlr_keyboard_from_input_device(device);

    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, nullptr,
        XKB_KEYMAP_COMPILE_NO_FLAGS);

    wlr_keyboard_set_keymap(wlrKeyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(wlrKeyboard, 25, 600);

    signal(modifiersListener, &wlrKeyboard->events.modifiers, keyboard_handle_modifiers);
    signal(keyListener, &wlrKeyboard->events.key, keyboard_handle_key);
    signal(destroyListener, &device->events.destroy, keyboard_handle_destroy);
}

void Keyboard::applyConfig(const KeyboardConfig &cfg) {
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context) {
        wlr_log(WLR_ERROR, "Failed to create xkb_context");
        return;
    }
    struct xkb_rule_names names = {};
    std::string layouts = cfg.layouts;
    std::string variant = cfg.variant;
    std::string options = cfg.options;
    // xkbcommon requires layout string to be non-empty; fallback to "us" if empty
    if (layouts.empty()) layouts = "us";
    names.rules = nullptr;
    names.model = nullptr;
    names.layout = layouts.c_str();
    names.variant = variant.empty() ? nullptr : variant.c_str();
    names.options = options.empty() ? nullptr : options.c_str();
    wlr_log(WLR_INFO, "Creating keymap: layouts='%s' variant='%s' options='%s'", layouts.c_str(), variant.c_str(), options.c_str());
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap) {
        wlr_keyboard_set_keymap(wlrKeyboard, keymap);
        // Ensure seat uses updated keymap if this is the active keyboard
        wlr_seat_set_keyboard(seat, wlrKeyboard);
        xkb_keymap_unref(keymap);
        wlr_log(WLR_INFO, "Keyboard keymap set successfully for %s", layouts.c_str());
    } else {
        wlr_log(WLR_ERROR, "Failed to create keymap for layouts '%s' variant '%s' options '%s' - trying fallback us", layouts.c_str(), variant.c_str(), options.c_str());
        struct xkb_rule_names fallback = {};
        fallback.layout = "us";
        struct xkb_keymap *fb = xkb_keymap_new_from_names(context, &fallback, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (fb) {
            wlr_keyboard_set_keymap(wlrKeyboard, fb);
            wlr_seat_set_keyboard(seat, wlrKeyboard);
            xkb_keymap_unref(fb);
        }
    }
    xkb_context_unref(context);
    int rate = cfg.repeat_rate;
    int delay = cfg.repeat_delay;
    if (rate < 0) rate = 0;
    if (rate > 100) rate = 100;
    if (delay < 100) delay = 100;
    if (delay > 2000) delay = 2000;
    wlr_keyboard_set_repeat_info(wlrKeyboard, rate, delay);
    wlr_log(WLR_INFO, "Keyboard config applied: layouts=%s variant=%s options=%s rate=%d delay=%d",
        cfg.layouts.c_str(), cfg.variant.c_str(), cfg.options.c_str(), rate, delay);
    // Log current keymap for verification
    if (wlrKeyboard->keymap) {
        const char *layout = xkb_keymap_layout_get_name(wlrKeyboard->keymap, 0);
        wlr_log(WLR_INFO, "Current keymap layout[0]: %s", layout ? layout : "unknown");
    }
}

Keyboard::~Keyboard()
{
    wl_list_remove(&modifiersListener.link);
    wl_list_remove(&keyListener.link);
    wl_list_remove(&destroyListener.link);
}
