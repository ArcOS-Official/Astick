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
    emit self->outputAdded((wlr_output *)data);
}

void handle_newXdgToplevelNotify(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, newXdgToplevelNotify);
    emit self->toplevelAdded((struct wlr_xdg_toplevel *)data);
}

void handle_newXdgPopupNotify(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, newXdgPopupNotify);
    emit self->popupAdded((struct wlr_xdg_popup *)data);
}

void handle_cursorMotion(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, cursorMotion);
    emit self->cursorMoved((struct wlr_pointer_motion_event *)data);
}

void handle_cursorMotionAbsolute(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, cursorMotionAbsolute);
    emit self->cursorMovedAbsolute((struct wlr_pointer_motion_absolute_event *)data);
}

void handle_cursorButton(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, cursorButton);
    emit self->cursorButton((struct wlr_pointer_button_event *)data);
}

void handle_cursorAxis(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, cursorAxis);
    emit self->cursorAxis((struct wlr_pointer_axis_event *)data);
}

void handle_cursorFrame(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, cursorFrame);
    emit self->cursorFrame();
}

void handle_requestCursor(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, requestCursor);
    emit self->requestSetCursor((struct wlr_seat_pointer_request_set_cursor_event *)data);
}

void handle_setSelection(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, setSelection);
    emit self->requestSetSelection((struct wlr_seat_request_set_selection_event *)data);
}

void handle_newInput(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, newInput);
    emit self->inputAdded((struct wlr_input_device *)data);
}

// --- Toplevel callbacks ---

void handle_map(wl_listener *listener, void *)
{
    Toplevel *self = wl_container_of(listener, self, map);
    emit self->mapped();
}

void handle_unmap(wl_listener *listener, void *)
{
    Toplevel *self = wl_container_of(listener, self, unmap);
    emit self->unmapped();
}

void handle_commit(wl_listener *listener, void *)
{
    Toplevel *self = wl_container_of(listener, self, commit);
    if (self->toplevel->base->initial_commit) {
        wlr_xdg_toplevel_set_size(self->toplevel, 0, 0);
    }
    emit self->committed();
}

void handle_destroy(wl_listener *listener, void *)
{
    Toplevel *self = wl_container_of(listener, self, destroy);
    wl_list_remove(&self->map.link);
    wl_list_remove(&self->unmap.link);
    wl_list_remove(&self->commit.link);
    wl_list_remove(&self->destroy.link);
    wl_list_remove(&self->request_move.link);
    wl_list_remove(&self->request_resize.link);
    wl_list_remove(&self->request_maximize.link);
    wl_list_remove(&self->request_fullscreen.link);
    emit self->destroyed();
    delete self;
}

void handle_request_move(wl_listener *listener, void *)
{
    Toplevel *self = wl_container_of(listener, self, request_move);
    emit self->moveRequested();
}

void handle_request_resize(wl_listener *listener, void *data)
{
    Toplevel *self = wl_container_of(listener, self, request_resize);
    auto *event = (struct wlr_xdg_toplevel_resize_event *)data;
    emit self->resizeRequested(event->edges);
}

void handle_request_maximize(wl_listener *listener, void *)
{
    Toplevel *self = wl_container_of(listener, self, request_maximize);
    if (self->toplevel->base->initialized) {
        wlr_xdg_surface_schedule_configure(self->toplevel->base);
    }
    emit self->maximizeRequested();
}

void handle_request_fullscreen(wl_listener *listener, void *)
{
    Toplevel *self = wl_container_of(listener, self, request_fullscreen);
    if (self->toplevel->base->initialized) {
        wlr_xdg_surface_schedule_configure(self->toplevel->base);
    }
    emit self->fullscreenRequested();
}

// --- Popup callbacks ---

void handle_popup_commit(wl_listener *listener, void *)
{
    Popup *self = wl_container_of(listener, self, commit);
    if (self->popup->base->initial_commit) {
        wlr_xdg_surface_schedule_configure(self->popup->base);
    }
    emit self->committed();
}

void handle_popup_destroy(wl_listener *listener, void *)
{
    Popup *self = wl_container_of(listener, self, destroy);
    wl_list_remove(&self->commit.link);
    wl_list_remove(&self->destroy.link);
    emit self->destroyed();
    delete self;
}

// --- Toplevel implementation ---

Toplevel::Toplevel(
    Compositor *server_,
    struct wlr_xdg_toplevel *toplevel_,
    struct wlr_scene_tree *sceneTree_
)
{
    server = server_;
    toplevel = toplevel_;
    sceneTree = sceneTree_;
    sceneTree->node.data = this;
    toplevel->base->data = (void *)sceneTree;

    signal(map, &toplevel->base->surface->events.map, handle_map);
    signal(unmap, &toplevel->base->surface->events.unmap, handle_unmap);
    signal(commit, &toplevel->base->surface->events.commit, handle_commit);
    signal(destroy, &toplevel->events.destroy, handle_destroy);
    signal(request_move, &toplevel->events.request_move, handle_request_move);
    signal(request_resize, &toplevel->events.request_resize, handle_request_resize);
    signal(request_maximize, &toplevel->events.request_maximize, handle_request_maximize);
    signal(request_fullscreen, &toplevel->events.request_fullscreen, handle_request_fullscreen);
}

// --- Popup implementation ---

Popup::Popup(
    Compositor *server_,
    struct wlr_xdg_popup *popup_
)
{
    server = server_;
    popup = popup_;

    struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(popup->parent);
    struct wlr_scene_tree *parent_tree = (struct wlr_scene_tree *)parent->data;
    popup->base->data = wlr_scene_xdg_surface_create(parent_tree, popup->base);

    signal(commit, &popup->base->surface->events.commit, handle_popup_commit);
    signal(destroy, &popup->base->surface->events.destroy, handle_popup_destroy);
}

// --- Compositor private slots ---

void Compositor::onOutputAdded(struct wlr_output *output)
{
    wlr_output_init_render(output, allocator, renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode *mode = wlr_output_preferred_mode(output);
    if (mode != nullptr)
        wlr_output_state_set_mode(&state, mode);

    wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);

    Output *out = new Output(output, renderer, allocator, scene);
    outputs.append(out);

    struct wlr_output_layout_output *lout =
        wlr_output_layout_add_auto(outputLayout, output);
    struct wlr_scene_output *rout = wlr_scene_output_create(scene, output);
    wlr_scene_output_layout_add_output(sceneLayout, lout, rout);
}

void Compositor::onToplevelAdded(struct wlr_xdg_toplevel *xtoplevel)
{
    Toplevel *toplevel = new Toplevel(this, xtoplevel, wlr_scene_xdg_surface_create(
        &scene->tree, xtoplevel->base
    ));
    toplevels.append(toplevel);

    connect(toplevel, &Toplevel::mapped, this, [this, toplevel]() {
        onToplevelMapped(toplevel);
    });
    connect(toplevel, &Toplevel::unmapped, this, [this, toplevel]() {
        onToplevelUnmapped(toplevel);
    });
    connect(toplevel, &Toplevel::moveRequested, this, [this, toplevel]() {
        beginInteractive(toplevel, CURSOR_MOVE, 0);
    });
    connect(toplevel, &Toplevel::resizeRequested, this, [this, toplevel](uint32_t edges) {
        beginInteractive(toplevel, CURSOR_RESIZE, edges);
    });
}

void Compositor::onPopupAdded(struct wlr_xdg_popup *xpopup)
{
    popups.append(new Popup(this, xpopup));
}

void Compositor::onCursorMoved(struct wlr_pointer_motion_event *event)
{
    wlr_cursor_move(cursor, &event->pointer->base, event->delta_x, event->delta_y);
    processCursorMotion(event->time_msec);
}

void Compositor::onCursorMovedAbsolute(struct wlr_pointer_motion_absolute_event *event)
{
    wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);
    processCursorMotion(event->time_msec);
}

void Compositor::onCursorButton(struct wlr_pointer_button_event *event)
{
    wlr_seat_pointer_notify_button(seat, event->time_msec, event->button, event->state);
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        resetCursorMode();
    } else {
        double sx, sy;
        struct wlr_surface *surface = nullptr;
        Toplevel *toplevel = toplevelAt(cursor->x, cursor->y, &surface, &sx, &sy);
        focusToplevel(toplevel);
    }
}

void Compositor::onCursorAxis(struct wlr_pointer_axis_event *event)
{
    wlr_seat_pointer_notify_axis(seat, event->time_msec, event->orientation,
        event->delta, event->delta_discrete, event->source, event->relative_direction);
}

void Compositor::onCursorFrame()
{
    wlr_seat_pointer_notify_frame(seat);
}

void Compositor::onRequestSetCursor(struct wlr_seat_pointer_request_set_cursor_event *event)
{
    struct wlr_seat_client *focused_client = seat->pointer_state.focused_client;
    if (focused_client == event->seat_client) {
        wlr_cursor_set_surface(cursor, event->surface, event->hotspot_x, event->hotspot_y);
    }
}

void Compositor::onRequestSetSelection(struct wlr_seat_request_set_selection_event *event)
{
    wlr_seat_set_selection(seat, event->source, event->serial);
}

void Compositor::onInputAdded(struct wlr_input_device *device)
{
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        addKeyboard(device);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        addMouse(device);
        break;
    default:
        break;
    }
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!keyboards.isEmpty()) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(seat, caps);
}

void Compositor::onToplevelMapped(Toplevel *toplevel)
{
    focusToplevel(toplevel);
    arrangeToplevels();
    emit toplevelMapped(toplevel);
}

void Compositor::onToplevelUnmapped(Toplevel *toplevel)
{
    if (toplevel == grabbedToplevel) {
        resetCursorMode();
    }
    toplevels.removeOne(toplevel);
    arrangeToplevels();
    emit toplevelUnmapped(toplevel);
}

// --- Focus management ---

void Compositor::focusToplevel(Toplevel *toplevel)
{
    if (toplevel == nullptr) return;

    struct wlr_surface *surface = toplevel->get()->base->surface;
    struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;

    if (prev_surface == surface) return;

    if (prev_surface) {
        struct wlr_xdg_toplevel *prev = wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev != nullptr) {
            wlr_xdg_toplevel_set_activated(prev, false);
        }
    }

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    wlr_scene_node_raise_to_top(&toplevel->getSceneTree()->node);
    toplevels.removeOne(toplevel);
    toplevels.prepend(toplevel);
    wlr_xdg_toplevel_set_activated(toplevel->get(), true);

    if (keyboard != nullptr) {
        wlr_seat_keyboard_notify_enter(seat, surface,
            keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
}

Toplevel *Compositor::toplevelAt(double lx, double ly,
    struct wlr_surface **surface, double *sx, double *sy)
{
    struct wlr_scene_node *node = wlr_scene_node_at(
        &scene->tree.node, lx, ly, sx, sy);
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
    return (Toplevel *)(tree ? tree->node.data : nullptr);
}

// --- Cursor interaction ---

void Compositor::processCursorMotion(uint32_t time)
{
    if (cursorMode == CURSOR_MOVE) {
        processCursorMove();
        return;
    } else if (cursorMode == CURSOR_RESIZE) {
        processCursorResize();
        return;
    }

    double sx, sy;
    struct wlr_surface *surface = nullptr;
    Toplevel *toplevel = toplevelAt(cursor->x, cursor->y, &surface, &sx, &sy);
    if (!toplevel) {
        wlr_cursor_set_xcursor(cursor, cursorMgr, "default");
    }
    if (surface) {
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(seat);
    }
}

void Compositor::processCursorMove()
{
    Toplevel *toplevel = grabbedToplevel;
    wlr_scene_node_set_position(&toplevel->getSceneTree()->node,
        cursor->x - grabX, cursor->y - grabY);
}

void Compositor::processCursorResize()
{
    Toplevel *toplevel = grabbedToplevel;
    double border_x = cursor->x - grabX;
    double border_y = cursor->y - grabY;

    int new_left = grabGeobox.x;
    int new_right = grabGeobox.x + grabGeobox.width;
    int new_top = grabGeobox.y;
    int new_bottom = grabGeobox.y + grabGeobox.height;

    if (resizeEdges & WLR_EDGE_TOP) {
        new_top = border_y;
        if (new_top >= new_bottom) new_top = new_bottom - 1;
    } else if (resizeEdges & WLR_EDGE_BOTTOM) {
        new_bottom = border_y;
        if (new_bottom <= new_top) new_bottom = new_top + 1;
    }
    if (resizeEdges & WLR_EDGE_LEFT) {
        new_left = border_x;
        if (new_left >= new_right) new_left = new_right - 1;
    } else if (resizeEdges & WLR_EDGE_RIGHT) {
        new_right = border_x;
        if (new_right <= new_left) new_right = new_left + 1;
    }

    struct wlr_box *geo_box = &toplevel->get()->base->geometry;
    wlr_scene_node_set_position(&toplevel->getSceneTree()->node,
        new_left - geo_box->x, new_top - geo_box->y);

    int new_width = new_right - new_left;
    int new_height = new_bottom - new_top;
    wlr_xdg_toplevel_set_size(toplevel->get(), new_width, new_height);
}

void Compositor::resetCursorMode()
{
    cursorMode = CURSOR_PASSTHROUGH;
    grabbedToplevel = nullptr;
}

void Compositor::beginInteractive(Toplevel *toplevel, CursorMode mode, uint32_t edges)
{
    grabbedToplevel = toplevel;
    cursorMode = mode;

    if (mode == CURSOR_MOVE) {
        grabX = cursor->x - toplevel->getSceneTree()->node.x;
        grabY = cursor->y - toplevel->getSceneTree()->node.y;
    } else {
        struct wlr_box *geo_box = &toplevel->get()->base->geometry;

        double border_x = (toplevel->getSceneTree()->node.x + geo_box->x) +
            ((edges & WLR_EDGE_RIGHT) ? geo_box->width : 0);
        double border_y = (toplevel->getSceneTree()->node.y + geo_box->y) +
            ((edges & WLR_EDGE_BOTTOM) ? geo_box->height : 0);
        grabX = cursor->x - border_x;
        grabY = cursor->y - border_y;

        grabGeobox = *geo_box;
        grabGeobox.x += toplevel->getSceneTree()->node.x;
        grabGeobox.y += toplevel->getSceneTree()->node.y;

        resizeEdges = edges;
    }
}

// --- Layout ---

void Compositor::arrangeToplevels()
{
    if (outputs.isEmpty()) return;

    struct wlr_output *wlr_output = outputs.first()->get();
    int width = wlr_output->width;
    int height = wlr_output->height;
    int count = toplevels.size();
    if (count == 0) return;

    int cols = std::ceil(std::sqrt(count));
    int rows = std::ceil((double)count / cols);

    int cell_w = width / cols;
    int cell_h = height / rows;

    for (int i = 0; i < count; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = col * cell_w;
        int y = row * cell_h;
        wlr_scene_node_set_position(&toplevels[i]->getSceneTree()->node, x, y);
        wlr_xdg_toplevel_set_size(toplevels[i]->get(), cell_w, cell_h);
    }
}

// --- Input device helpers ---

void Compositor::addKeyboard(struct wlr_input_device *device)
{
    Keyboard *kb = new Keyboard(device, seat);
    keyboards.append(kb);
    connect(kb, &Keyboard::keyPressed, this, [this, kb](struct wlr_keyboard_key_event *event) {
        uint32_t keycode = event->keycode + 8;
        const xkb_keysym_t *syms;
        int nsyms = xkb_state_key_get_syms(
            kb->getKeyboard()->xkb_state, keycode, &syms);

        bool handled = false;
        uint32_t modifiers = wlr_keyboard_get_modifiers(kb->getKeyboard());
        if ((modifiers & WLR_MODIFIER_ALT) &&
                event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
            for (int i = 0; i < nsyms; i++) {
                switch (syms[i]) {
                case XKB_KEY_Escape:
                    wl_display_terminate(display);
                    handled = true;
                    break;
                case XKB_KEY_F1:
                    if (toplevels.size() >= 2) {
                        focusToplevel(toplevels.last());
                    }
                    handled = true;
                    break;
                default:
                    break;
                }
            }
        }

        if (!handled) {
            wlr_seat_set_keyboard(seat, kb->getKeyboard());
            wlr_seat_keyboard_notify_key(seat, event->time_msec,
                event->keycode, event->state);
        }
    });
}

void Compositor::addMouse(struct wlr_input_device *device)
{
    Mouse *mouse = new Mouse(device);
    mice.append(mouse);
    wlr_cursor_attach_input_device(cursor, device);
}

// --- Compositor lifecycle ---

Compositor::Compositor(const Astick &app)
{
    connect(&app, &Astick::aboutToRun, this, &Compositor::run);

    connect(this, &Compositor::outputAdded, this, &Compositor::onOutputAdded);
    connect(this, &Compositor::toplevelAdded, this, &Compositor::onToplevelAdded);
    connect(this, &Compositor::popupAdded, this, &Compositor::onPopupAdded);
    connect(this, &Compositor::cursorMoved, this, &Compositor::onCursorMoved);
    connect(this, &Compositor::cursorMovedAbsolute, this, &Compositor::onCursorMovedAbsolute);
    connect(this, &Compositor::cursorButton, this, &Compositor::onCursorButton);
    connect(this, &Compositor::cursorAxis, this, &Compositor::onCursorAxis);
    connect(this, &Compositor::cursorFrame, this, &Compositor::onCursorFrame);
    connect(this, &Compositor::requestSetCursor, this, &Compositor::onRequestSetCursor);
    connect(this, &Compositor::requestSetSelection, this, &Compositor::onRequestSetSelection);
    connect(this, &Compositor::inputAdded, this, &Compositor::onInputAdded);

    display = wl_display_create();
    loop = wl_display_get_event_loop(display);
    backend = wlr_backend_autocreate(loop, nullptr);
    if (backend == nullptr) {
        wlr_log(WLR_ERROR, "Failed to create backend");
        return;
    }
    renderer = wlr_renderer_autocreate(backend);
    if (renderer == nullptr) {
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
    wlr_cursor_attach_output_layout(cursor, outputLayout);
    cursorMgr = wlr_xcursor_manager_create(nullptr, 24);
    wlr_xcursor_manager_load(cursorMgr, 1);

    seat = wlr_seat_create(display, "seat0");

    socket = QString(wl_display_add_socket_auto(display));
    wlr_log(WLR_INFO, "Successfully initialized on socket %s", socket.toUtf8().data());
    initialized = true;

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

Compositor::~Compositor()
{
    if (initialized) {
        wl_display_destroy_clients(display);

        wl_list_remove(&newOutputListener.link);
        wl_list_remove(&newXdgToplevelNotifyListener.link);
        wl_list_remove(&newXdgPopupNotifyListener.link);
        wl_list_remove(&cursorMotionListener.link);
        wl_list_remove(&cursorMotionAbsoluteListener.link);
        wl_list_remove(&cursorButtonListener.link);
        wl_list_remove(&cursorAxisListener.link);
        wl_list_remove(&cursorFrameListener.link);
        wl_list_remove(&requestCursorListener.link);
        wl_list_remove(&setSelectionListener.link);
        wl_list_remove(&newInputListener.link);

        for (Output *out : outputs) delete out;
        for (Toplevel *t : toplevels) delete t;
        for (Popup *p : popups) delete p;
        for (Keyboard *k : keyboards) delete k;
        for (Mouse *m : mice) delete m;
    }
    if (cursorMgr) wlr_xcursor_manager_destroy(cursorMgr);
    if (scene) wlr_scene_node_destroy(&scene->tree.node);
    if (cursor) wlr_cursor_destroy(cursor);
    if (allocator) wlr_allocator_destroy(allocator);
    if (renderer) wlr_renderer_destroy(renderer);
    if (backend) wlr_backend_destroy(backend);
    if (display) wl_display_destroy(display);
    initialized = false;
}

void Compositor::run()
{
    if (!initialized) {
        wlr_log(WLR_ERROR, "Astick not initialized, will not run");
        return;
    }

    if (!wlr_backend_start(backend)) {
        wlr_log(WLR_ERROR, "Failed to start backend");
        return;
    }

    setenv("WAYLAND_DISPLAY", socket.toUtf8().data(), true);
    wlr_log(WLR_INFO, "Running Astick on socket \"%s\"", socket.toUtf8().data());
    wl_display_run(display);
}

void Compositor::closePopup()
{
    if (!popups.isEmpty()) {
        Popup *popup = popups.takeLast();
        wlr_log(WLR_INFO, "Closing popup");
        emit popup->destroyed();
        delete popup;
    }
}
