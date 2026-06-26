#include "compositor.h"
#include <QObject>
#include "debug.h"
#include <cstdlib>
#include <wayland-server-core.h>
#include "util.h"
#include "wlroots.h"
#include "output.h"

#define ADD_TOKS(a, b) a##b
#define getComp(name, listenerName) \
    wl_container_of(listener, name, ADD_TOKS(listenerName, Listener))

void handle_newOutput(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, newOutput);
    wlr_output *output = (wlr_output *)data;
    wlr_output_init_renderer(output, self->allocator, self->renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode *mode = wlr_output_preferred_mode(output);
    if (mode != nullptr)
        wlr_output_state_set_mode(&state, mode);

    wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);

    self->outputs.append(new Output(output));
    struct wlr_output_layout_output *lout =
        wlr_output_layout_add_auto(self->outputLayout, output);
    struct wlr_scene_output *rout = wlr_scene_output_create(self->scene, output);
    wlr_scene_output_layout_add_output(self->sceneLayout, lout, rout);
}

void handle_newXdgToplevelNotify(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, newOutput);
    struct wlr_xdg_toplevel *xtoplevel = (struct wlr_xdg_toplevel *)data;
    Toplevel *toplevel = new Toplevel(self, xtoplevel, wlr_scene_xdg_surface_create(
        &self->scene->tree, xtoplevel->base
    ));
    self->toplevels.append(toplevel);
}

void handle_newXdgPopupNotify(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, newOutput);
    struct wlr_xdg_popup *xpopup = (struct wlr_xdg_popup *)data;
    self->popups.append(new Popup(self, xpopup));
}

void handle_cursorMotion(wl_listener *listener, void *data);
void handle_cursorMotionAbsolute(wl_listener *listener, void *data);
void handle_cursorButton(wl_listener *listener, void *data);
void handle_cursorAxis(wl_listener *listener, void *data);
void handle_cursorFrame(wl_listener *listener, void *data);
void handle_requestCursor(wl_listener *listener, void *data);
void handle_setSelection(wl_listener *listener, void *data);

void handle_newInput(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, newOutput);
    struct wlr_input_device *device = (wlr_input_device *)data;
    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            wlr_log(WLR_INFO, "New keyboard %s", device->name);
            self->keyboards.append(new Keyboard(device));
        case WLR_INPUT_DEVICE_POINTER:
            wlr_log(WLR_INFO, "New mouse %s", device->name);
            self->mice.append(new Mouse(device));
        default:
            wlr_log(WLR_ERROR, "Unsupported device %s", device->name);
    }
}

void handle_map(wl_listener *listener, void *data);
void handle_unmap(wl_listener *listener, void *data);
void handle_commit(wl_listener *listener, void *data);
void handle_destroy(wl_listener *listener, void *data);

Toplevel::Toplevel(
    Compositor *server_,
    struct wlr_xdg_toplevel *toplevel_,
    struct wlr_scene_tree *sceneTree_
)
{
    toplevel = toplevel_;
    sceneTree = sceneTree_;
    server = server_;
    sceneTree->node.data = this;
    sceneTree->node.data = this;
    toplevel->base->data = (void *)sceneTree;
    signal(map, &toplevel->base->surface->events.map, handle_map);
    signal(unmap, &toplevel->base->surface->events.unmap, handle_unmap);
    signal(commit, &toplevel->base->surface->events.commit, handle_commit);
    signal(destroy, &toplevel->events.destroy, handle_destroy);
}

void handle_commit(wl_listener *listener, void *data);
void handle_destroy(wl_listener *listener, void *data);

void Popup::activate()
{
    parent = wlr_xdg_surface_try_from_wlr_surface(popup->parent);
    if (parent == NULL) {
        wlr_log(WLR_ERROR, "Popup got orphaned, skipping");
        emit closed();
    }
    struct wlr_scene_tree *parent_tree = (struct wlr_scene_tree *)parent->data;
    popup->base->data = wlr_scene_xdg_surface_create(parent_tree, popup->base);
    signal(commit, &popup->base->surface->events.commit, handle_commit);
    signal(destroy, &popup->base->surface->events.destroy, handle_destroy);
}
void Popup::closed()
{}

Popup::Popup(
    Compositor *server_,
    struct wlr_xdg_popup *popup_
)
{
    server = server_;
    popup = popup_;
}

Compositor::Compositor(const Astick &app)
{
    connect(&app, &Astick::aboutToRun, this, &Compositor::run);
    display = wl_display_create();
    loop = wl_display_get_event_loop(display);
    backend = wlr_backend_autocreate(loop, nullptr);
    if (backend == nullptr) {
        wlr_log(WLR_ERROR, "Failed to create backend");
        return;
    }
    renderer = wlr_renderer_autocreate(backend);
	if (renderer == NULL) {
		wlr_log(WLR_ERROR, "failed to create renderer");
        return;
	}
    wlr_renderer_init_wl_display(renderer, display);
    allocator = wlr_allocator_autocreate(backend, renderer);
    if (allocator == nullptr) {
        wlr_log(WLR_ERROR, "failed to create allocator");
        return;
    }

    wlr_compositor_create(display, 5, renderer);
    wlr_subcompositor_create(display);
    wlr_data_device_manager_create(display);

    outputLayout = wlr_output_layout_create(display);
    scene = wlr_scene_create();
    sceneLayout = wlr_scene_attach_output_layout(scene, outputLayout);

    xdgShell = wlr_xdg_shell_create(display, 3);
    cursor = wlr_cursor_create();
    seat = wlr_seat_create(display, "seat0");

    socket = QString(wl_display_add_socket_auto(display));
    wlr_log(WLR_INFO, "Successfully initialized");

    signal(newOutputListener, &backend->events.new_output, handle_newOutput);
    signal(newXdgToplevelNotifyListener, &xdgShell->events.new_toplevel, handle_newXdgToplevelNotify);
    signal(newXdgPopupNotifyListener, &xdgShell->events.new_popup, handle_newXdgPopupNotify);
    signal(cursorMotionListener, &cursor->events.motion, handle_cursorMotion);
    signal(cursorMotionAbsoluteListener, &cursor->events.motion_absolute, handle_cursorMotionAbsolute);
    signal(cursorButtonListener, &cursor->events.button, handle_cursorButton);
    signal(cursorAxisListener, &cursor->events.axis, handle_cursorAxis);
    signal(cursorFrameListener, &cursor->events.frame, handle_cursorFrame);
    signal(requestCursorListener, &seat->events.request_set_cursor, handle_requestCursor);
    signal(setSelectionListener, &seat->events.request_set_selection, handle_setSelection);
    signal(newInputListener, &backend->events.new_input, handle_newInput);
}

Compositor::~Compositor() {
    if (initialized) {
        wl_display_destroy_clients(display);
        for (Output *out : outputs) {
            delete out;
        }
    }
    if (backend != nullptr)
        wlr_backend_destroy(backend);
    if (renderer != nullptr)
        wlr_renderer_destroy(renderer);
    if (allocator != nullptr)
        wlr_allocator_destroy(allocator);
    wlr_cursor_destroy(cursor);
    wl_display_destroy(display);
    initialized = false;
}

void Compositor::run()
{
    if (!initialized) {
        wlr_log(WLR_ERROR, "Astick not initialized, will not run");
        return;
    }

    setenv("WAYLAND_DISPLAY", socket, true);
    wlr_log(WLR_INFO, "Running Astick on socket \"%s\"", socket.toUtf8().data());
}
