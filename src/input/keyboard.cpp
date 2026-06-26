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

Keyboard::Keyboard(struct wlr_input_device *device, struct wlr_seat *seat_)
{
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

Keyboard::~Keyboard()
{
    wl_list_remove(&modifiersListener.link);
    wl_list_remove(&keyListener.link);
    wl_list_remove(&destroyListener.link);
}
