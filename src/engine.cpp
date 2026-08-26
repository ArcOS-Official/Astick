#include <cassert>
#include <quuid.h>
#include <wayland-server-core.h>
#include "engine.h"
#include "wlr.h"

void Compositor::focus(uint32_t id) {
	/* Note: this function only deals with keyboard focus. */
    Compositor::Toplevel *toplevel = nullptr;
    for (size_t i = 0; i < server.toplevels.size(); i++) {
        Compositor::Toplevel *t = &server.toplevels[i];
        if (t->id == id)
            toplevel = t;
    }
    assert(toplevel);

    Compositor::Server *server = toplevel->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	if (prev_surface == surface) {
		return;
	}
	if (prev_surface) {
		struct wlr_xdg_toplevel *prev_toplevel =
			wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
		if (prev_toplevel != nullptr) {
			wlr_xdg_toplevel_set_activated(prev_toplevel, false);
		}
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
	if (keyboard != nullptr) {
		wlr_seat_keyboard_notify_enter(seat, surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}
}

void keyboard_handle_modifiers(
		struct wl_listener *listener, void *) {
    Compositor::Keyboard *keyboard =
		wl_container_of(listener, keyboard, modifiers);
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
		&keyboard->wlr_keyboard->modifiers);
}

void keyboard_handle_key(
		struct wl_listener *listener, void *data) {
	/* This event is raised when a key is pressed or released. */
    Compositor::Keyboard *keyboard =
		wl_container_of(listener, keyboard, key);
    Compositor::Server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = (struct wlr_keyboard_key_event *)data;
	struct wlr_seat *seat = server->seat;

	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
			keyboard->wlr_keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	if ((modifiers & WLR_MODIFIER_ALT) &&
			event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (int i = 0; i < nsyms; i++) {
		}
	}

	if (!handled) {
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec,
			event->keycode, event->state);
	}
}

void keyboard_handle_destroy(struct wl_listener *listener, void *) {
    Compositor::Keyboard *keyboard =
		wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
    for (size_t i = 0; i < keyboard->server->keyboards.size(); i++) {
        if (keyboard->server->keyboards[i].id = keyboard->id)
            keyboard->server->keyboards.erase(keyboard->server->keyboards.begin() + i);
    }
}

void server_new_keyboard(Compositor::Server *server,
		struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

    server->keyboards.push_back({0});
    Compositor::Keyboard *keyboard = &server->keyboards[server->keyboards.size()-1];
    keyboard->id = genId();
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, nullptr,
		XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	/* Here we set up listeners for keyboard events. */
	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
}

void server_new_pointer(Compositor::Server *server,
		struct wlr_input_device *device) {
	wlr_cursor_attach_input_device(server->cursor, device);
}

void server_new_input(struct wl_listener *listener, void *data) {
	/* This event is raised by the backend when a new input device becomes
	 * available. */
    Compositor::Server *server =
		wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = (struct wlr_input_device *)data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	default:
		break;
	}
	/* We need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. In TinyWL we always have a cursor, even if
	 * there are no pointer devices, so we always include that capability. */
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!server->keyboards.empty()) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

void seat_request_cursor(struct wl_listener *listener, void *data) {
    Compositor::Server *server = wl_container_of(
			listener, server, request_cursor);
	/* This event is raised by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = (struct wlr_seat_pointer_request_set_cursor_event *)data;
	struct wlr_seat_client *focused_client =
		server->seat->pointer_state.focused_client;
	/* This can be sent by any client, so we check to make sure this one is
	 * actually has pointer focus first. */
	if (focused_client == event->seat_client) {
		/* Once we've vetted the client, we can tell the cursor to use the
		 * provided surface as the cursor image. It will set the hardware cursor
		 * on the output that it's currently on and continue to do so as the
		 * cursor moves between outputs. */
		wlr_cursor_set_surface(server->cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
	}
}

void seat_request_set_selection(struct wl_listener *listener, void *data) {
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in tinywl we always honor
	 */
    Compositor::Server *server = wl_container_of(
			listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = (struct wlr_seat_request_set_selection_event *)data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

Compositor::Toplevel *desktop_toplevel_at(
		Compositor::Server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	/* This returns the topmost node in the scene at the given layout coords.
	 * We only care about surface nodes as we are specifically looking for a
	 * surface in the surface tree of a tinywl_toplevel. */
	struct wlr_scene_node *node = wlr_scene_node_at(
		&server->scene->tree.node, lx, ly, sx, sy);
	if (node == nullptr || node->type != WLR_SCENE_NODE_BUFFER) {
		return nullptr;
	}
	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(scene_buffer);
	if (!scene_surface) {
		return nullptr;
	}

	*surface = scene_surface->surface;
	/* Find the node corresponding to the tinywl_toplevel at the root of this
	 * surface tree, it is the only one for which we set the data field. */
	struct wlr_scene_tree *tree = node->parent;
	while (tree != nullptr && tree->node.data == nullptr) {
		tree = tree->node.parent;
	}
	return (Compositor::Toplevel *)tree->node.data;
}

void reset_cursor_mode(Compositor::Server *server) {
	/* Reset the cursor mode to passthrough. */
	server->cursor_mode = Compositor::CursorMode::Passthrough;
	server->grabbed_toplevel = nullptr;
}

void process_cursor_motion(Compositor::Server *server, uint32_t time) {
	if (server->cursor_mode == Compositor::CursorMode::Move) {
		//process_cursor_move(server);
		return;
	} else if (server->cursor_mode == Compositor::CursorMode::Resize) {
		//process_cursor_resize(server);
		return;
	}

	double sx, sy;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *surface = nullptr;
    Compositor::Toplevel *toplevel = desktop_toplevel_at(server,
			server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (!toplevel) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
	if (surface) {
		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(seat);
	}
}

void server_cursor_motion(struct wl_listener *listener, void *data) {
    Compositor::Server *server =
		wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = (struct wlr_pointer_motion_event *)data;
	wlr_cursor_move(server->cursor, &event->pointer->base,
			event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

void server_cursor_motion_absolute(
		struct wl_listener *listener, void *data) {
    Compositor::Server *server =
		wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event =
        (struct wlr_pointer_motion_absolute_event *)data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x,
		event->y);
	process_cursor_motion(server, event->time_msec);
}

void server_cursor_button(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits a button
	 * event. */
    Compositor::Server *server =
		wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = (struct wlr_pointer_button_event *)data;
	/* Notify the client with pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(server->seat,
			event->time_msec, event->button, event->state);
	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		/* If you released any buttons, we exit interactive move/resize mode. */
		reset_cursor_mode(server);
	} else {
		/* Focus that client if the button was _pressed_ */
		double sx, sy;
		struct wlr_surface *surface = nullptr;
        Compositor::Toplevel *toplevel = desktop_toplevel_at(server,
				server->cursor->x, server->cursor->y, &surface, &sx, &sy);
		server->comp->focus(toplevel->id);
	}
}

void server_cursor_axis(struct wl_listener *listener, void *data) {
    Compositor::Server *server =
		wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = (struct wlr_pointer_axis_event *)data;
	wlr_seat_pointer_notify_axis(server->seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source, event->relative_direction);
}

void server_cursor_frame(struct wl_listener *listener, void *) {
    Compositor::Server *server =
		wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

void output_frame(struct wl_listener *listener, void *) {
    Compositor::Output *output = wl_container_of(listener, output, frame);
	struct wlr_scene *scene = output->server->scene;

	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
		scene, output->wlr_output);

	wlr_scene_output_commit(scene_output, nullptr);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

void output_request_state(struct wl_listener *listener, void *data) {
    Compositor::Output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event =
        (struct wlr_output_event_request_state *)data;
	wlr_output_commit_state(output->wlr_output, event->state);
}

void output_destroy(struct wl_listener *listener, void *) {
    Compositor::Output *output = wl_container_of(listener, output, destroy);

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
    for (size_t i = 0; i < output->server->outputs.size(); i++) {
        if (output->server->outputs[i].id == output->id)
            output->server->outputs.erase(output->server->outputs.begin() + 1);
    }
}

void server_new_output(struct wl_listener *listener, void *data) {
    Compositor::Server *server =
		wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = (struct wlr_output *)data;

	wlr_output_init_render(wlr_output, server->allocator, server->renderer);

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != nullptr) {
		wlr_output_state_set_mode(&state, mode);
	}

	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);


    server->outputs.push_back({0});
    Compositor::Output *output = &server->outputs[server->outputs.size()-1];
	output->wlr_output = wlr_output;
	output->server = server;
    output->id = genId();

	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);

	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	struct wlr_output_layout_output *l_output = wlr_output_layout_add_auto(server->output_layout,
		wlr_output);
	struct wlr_scene_output *scene_output = wlr_scene_output_create(server->scene, wlr_output);
	wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);
}

void xdg_toplevel_map(struct wl_listener *listener, void *) {
    Compositor::Toplevel *toplevel = wl_container_of(listener, toplevel, map);

	toplevel->server->comp->focus(toplevel->id);
}

void xdg_toplevel_unmap(struct wl_listener *listener, void *) {
    Compositor::Toplevel *toplevel = wl_container_of(listener, toplevel, unmap);

	if (toplevel == toplevel->server->grabbed_toplevel) {
		reset_cursor_mode(toplevel->server);
	}

    for (size_t i = 0; i < toplevel->server->toplevels.size(); i++) {
        if (toplevel->server->toplevels[i].id == toplevel->id)
            toplevel->server->toplevels.erase(toplevel->server->toplevels.begin() + i);
    }
}

void xdg_toplevel_commit(struct wl_listener *listener, void *) {
    Compositor::Toplevel *toplevel = wl_container_of(listener, toplevel, commit);

	if (toplevel->xdg_toplevel->base->initial_commit) {
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
	}
}

void xdg_toplevel_destroy(struct wl_listener *listener, void *) {
    Compositor::Toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);

    for (size_t i = 0; i < toplevel->server->toplevels.size(); i++) {
        if (toplevel->server->toplevels[i].id == toplevel->id)
            toplevel->server->toplevels.erase(toplevel->server->toplevels.begin() + i);
    }
}

void begin_interactive(Compositor::Toplevel *toplevel,
		Compositor::CursorMode mode, uint32_t edges) {
	/* This function sets up an interactive move or resize operation, where the
	 * compositor stops propegating pointer events to clients and instead
	 * consumes them itself, to move or resize windows. */
    Compositor::Server *server = toplevel->server;

	server->grabbed_toplevel = toplevel;
	server->cursor_mode = mode;

	if (mode == Compositor::CursorMode::Move) {
		server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
		server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
	} else {
		struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;

		double border_x = (toplevel->scene_tree->node.x + geo_box->x) +
			((edges & WLR_EDGE_RIGHT) ? geo_box->width : 0);
		double border_y = (toplevel->scene_tree->node.y + geo_box->y) +
			((edges & WLR_EDGE_BOTTOM) ? geo_box->height : 0);
		server->grab_x = server->cursor->x - border_x;
		server->grab_y = server->cursor->y - border_y;

		server->grab_geobox = *geo_box;
		server->grab_geobox.x += toplevel->scene_tree->node.x;
		server->grab_geobox.y += toplevel->scene_tree->node.y;

		server->resize_edges = edges;
	}
}

void xdg_toplevel_request_move(
		struct wl_listener *listener, void *) {
    Compositor::Toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
	begin_interactive(toplevel, Compositor::CursorMode::Move, 0);
}

void xdg_toplevel_request_resize(
		struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_resize_event *event =
        (struct wlr_xdg_toplevel_resize_event *)data;
    Compositor::Toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
	begin_interactive(toplevel, Compositor::CursorMode::Resize, event->edges);
}

void xdg_toplevel_request_maximize(
		struct wl_listener *listener, void *) {
    Compositor::Toplevel *toplevel =
		wl_container_of(listener, toplevel, request_maximize);
	if (toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
	}
}

void xdg_toplevel_request_fullscreen(
		struct wl_listener *listener, void *) {
    Compositor::Toplevel *toplevel =
		wl_container_of(listener, toplevel, request_fullscreen);
	if (toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
	}
}

void xdg_popup_commit(struct wl_listener *listener, void *) {
    Compositor::Popup *popup = wl_container_of(listener, popup, commit);

	if (popup->xdg_popup->base->initial_commit) {
		wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	}
}


void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	struct Compositor::Server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = (struct wlr_xdg_toplevel *)data;

    server->toplevels.push_back({0});
    Compositor::Toplevel *toplevel = &server->toplevels[server->toplevels.size()-1];
	toplevel->server = server;
	toplevel->xdg_toplevel = xdg_toplevel;
	toplevel->scene_tree =
		wlr_scene_xdg_surface_create(&toplevel->server->scene->tree, xdg_toplevel->base);
	toplevel->scene_tree->node.data = toplevel;
	xdg_toplevel->base->data = toplevel->scene_tree;

	toplevel->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
	toplevel->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

	toplevel->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

	toplevel->request_move.notify = xdg_toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
	toplevel->request_resize.notify = xdg_toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);
	toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);
	toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);
}

void xdg_popup_destroy(struct wl_listener *listener, void *) {
    Compositor::Popup *popup = wl_container_of(listener, popup, destroy);

	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);

    for (size_t i = 0; i < popup->server->popups.size(); i++) {
        if (popup->server->popups[i].id == popup->id) {
            popup->server->popups.erase(popup->server->popups.begin() + i);
        }
    }
}

void server_new_xdg_popup(struct wl_listener *listener, void *data) {
	Compositor::Server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_popup *xdg_popup = (struct wlr_xdg_popup *)data;

    server->popups.push_back({0});
	Compositor::Popup *popup = &server->popups[server->popups.size()-1];
	popup->xdg_popup = xdg_popup;
    popup->server = server;
    popup->id = genId();

	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	assert(parent != nullptr);
	struct wlr_scene_tree *parent_tree = (struct wlr_scene_tree *)parent->data;
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

void Compositor::process_cursor_move() {
    Compositor::Toplevel *toplevel = server.grabbed_toplevel;
	wlr_scene_node_set_position(&toplevel->scene_tree->node,
		server.cursor->x - server.grab_x,
		server.cursor->y - server.grab_y);
}

void Compositor::process_cursor_resize() {
    Compositor::Toplevel *toplevel = server.grabbed_toplevel;
	double border_x = server.cursor->x - server.grab_x;
	double border_y = server.cursor->y - server.grab_y;
	int new_left = server.grab_geobox.x;
	int new_right = server.grab_geobox.x + server.grab_geobox.width;
	int new_top = server.grab_geobox.y;
	int new_bottom = server.grab_geobox.y + server.grab_geobox.height;

	if (server.resize_edges & WLR_EDGE_TOP) {
		new_top = border_y;
		if (new_top >= new_bottom) {
			new_top = new_bottom - 1;
		}
	} else if (server.resize_edges & WLR_EDGE_BOTTOM) {
		new_bottom = border_y;
		if (new_bottom <= new_top) {
			new_bottom = new_top + 1;
		}
	}
	if (server.resize_edges & WLR_EDGE_LEFT) {
		new_left = border_x;
		if (new_left >= new_right) {
			new_left = new_right - 1;
		}
	} else if (server.resize_edges & WLR_EDGE_RIGHT) {
		new_right = border_x;
		if (new_right <= new_left) {
			new_right = new_left + 1;
		}
	}

	struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;
	wlr_scene_node_set_position(&toplevel->scene_tree->node,
		new_left - geo_box->x, new_top - geo_box->y);

	int new_width = new_right - new_left;
	int new_height = new_bottom - new_top;
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
}

Compositor::Toplevel *Compositor::desktop_toplevel_at(
		double lx, double ly, struct wlr_surface **surface,
        double *sx, double *sy) {
	struct wlr_scene_node *node = wlr_scene_node_at(
		&server.scene->tree.node, lx, ly, sx, sy);
	if (node == nullptr || node->type != WLR_SCENE_NODE_BUFFER) {
		return nullptr;
	}
	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(scene_buffer);
	if (!scene_surface) {
		return nullptr;
	}

	*surface = scene_surface->surface;
	struct wlr_scene_tree *tree = node->parent;
	while (tree != nullptr && tree->node.data == nullptr) {
		tree = tree->node.parent;
	}
	return (Compositor::Toplevel *)tree->node.data;
}

void Compositor::process_cursor_motion(uint32_t time) {
	if (server.cursor_mode == Compositor::CursorMode::Move) {
		process_cursor_move();
		return;
	} else if (server.cursor_mode == Compositor::CursorMode::Resize) {
		process_cursor_resize();
		return;
	}

	double sx, sy;
	struct wlr_seat *seat = server.seat;
	struct wlr_surface *surface = nullptr;
    Compositor::Toplevel *toplevel = desktop_toplevel_at(server.cursor->x,
            server.cursor->y, &surface, &sx, &sy);
	if (!toplevel) {
		wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "default");
	}
	if (surface) {
		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(seat);
	}
}

Compositor::Compositor() {
    wlr_log_init(WLR_DEBUG, nullptr);
    server.comp = this;
    server.wl_display = wl_display_create();
    server.backend = wlr_backend_autocreate(
        wl_display_get_event_loop(server.wl_display),
        nullptr
    );
    if (server.backend == nullptr) {
        throw std::runtime_error("Failed to autocreate wlr backend");
    }

    server.renderer = wlr_renderer_autocreate(server.backend);

	wlr_renderer_init_wl_display(server.renderer, server.wl_display);

	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	if (server.allocator == nullptr) {
		throw std::runtime_error("failed to create wlr_allocator");
	}

	wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);

	server.output_layout = wlr_output_layout_create(server.wl_display);

	server.new_output.notify = server_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.scene = wlr_scene_create();
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

	server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
	server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
	server.new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

	server.cursor_mgr = wlr_xcursor_manager_create(nullptr, 24);

	server.cursor_mode = Compositor::CursorMode::Passthrough;
	server.cursor_motion.notify = server_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
	server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute,
			&server.cursor_motion_absolute);
	server.cursor_button.notify = server_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);
	server.cursor_axis.notify = server_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
	server.cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

	server.new_input.notify = server_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);
	server.seat = wlr_seat_create(server.wl_display, "seat0");
	server.request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor,
			&server.request_cursor);
	server.request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server.seat->events.request_set_selection,
			&server.request_set_selection);
}

// This looks messy as fuck because it is but I want to manage the event loop so sorry
struct wl_priv_signal {
	struct wl_list listener_list;
	struct wl_list emit_list;
};

struct wl_display {
	struct wl_event_loop *loop;
	bool run;

	uint32_t next_global_name;
	uint32_t serial;

	struct wl_list registry_resource_list;
	struct wl_list global_list;
	struct wl_list socket_list;
	struct wl_list client_list;
	struct wl_list protocol_loggers;

	struct wl_priv_signal destroy_signal;
	struct wl_priv_signal create_client_signal;

	struct wl_array additional_shm_formats;

	wl_display_global_filter_func_t global_filter;
	void *global_filter_data;

	int terminate_efd;
	struct wl_event_source *term_source;

	size_t max_buffer_size;
};

void Compositor::run() {
	const char *socket = wl_display_add_socket_auto(server.wl_display);
	assert(socket);

	/* Start the backend. This will enumerate outputs and inputs, become the DRM
	 * master, etc */
	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
        wlr_log(WLR_ERROR, "Failed to start backend");
		return;
	}

	setenv("WAYLAND_DISPLAY", socket, true);

	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s",
			socket);

	server.wl_display->run = true;

	while (server.wl_display->run) {
		wl_display_flush_clients(server.wl_display);
		if (wl_event_loop_dispatch(server.wl_display->loop, -1) < 0) {
			break;
		}
	}
}

Compositor::~Compositor() {
	wl_display_destroy_clients(server.wl_display);

	wl_list_remove(&server.new_xdg_toplevel.link);
	wl_list_remove(&server.new_xdg_popup.link);

	wl_list_remove(&server.cursor_motion.link);
	wl_list_remove(&server.cursor_motion_absolute.link);
	wl_list_remove(&server.cursor_button.link);
	wl_list_remove(&server.cursor_axis.link);
	wl_list_remove(&server.cursor_frame.link);

	wl_list_remove(&server.new_input.link);
	wl_list_remove(&server.request_cursor.link);
	wl_list_remove(&server.request_set_selection.link);

	wl_list_remove(&server.new_output.link);

	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.wl_display);
}

uint32_t genId() noexcept {
    return QRandomGenerator::global()->generate();
}
